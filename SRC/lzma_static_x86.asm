; -*- tab-width: 8 -*-
; Copyright (c) 2022, Ilya Kurdyukov
; All rights reserved.
;
; Micro LZMA decoder for x86 (static)
;
; This software is distributed under the terms of the
; Creative Commons Attribution 3.0 License (CC-BY 3.0)
; http://creativecommons.org/licenses/by/3.0/
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
; OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
; THE SOFTWARE.

; build: nasm -f win32 -O9 lzma_static_x86.asm -o lzma_static_x86.obj

; 本文件是 32 位 x86 NASM 汇编，输出为 Windows COFF 对象并与 C 静态链接。
; 解码器放在独立 .lzma 段。C 直接调用解码入口并通过 cdecl 参数栈传入
; 指针和 lc/lp；prepare 只修补 pb/lp/lc 对应的指令立即数。代码段是共享的，
; 因此当前原位修补方案不支持并发解码。

BITS 32
section .lzma code align=16

; C 通过段起止标签确定临时改权限和刷新指令缓存的范围；C 不需要知道各个
; 修补点的位置。链接器会把此段标为可读、可执行。
global _lzma_static_x86_section_start
global _lzma_static_x86_section_end
global _lzma_static_x86_prepare
global _lzma_static_x86_run

_lzma_static_x86_section_start:

; -----------------------------------------------------------------------------
; 准备入口：cdecl prepare(lc, lp, pb)
; 32 位 cdecl 参数从 [EBP+8] 起，每个 unsigned 占 4 字节：
;   [EBP+ 8] lc           字面量上下文属性
;   [EBP+12] lp           字面量位置属性
;   [EBP+16] pb           位置状态属性
; 这里只修补属性立即数；输入、输出和工作区指针由 run 的 cdecl 参数直接传入。
; EAX/ECX/EDX 是调用者可破坏寄存器；EBP 在返回时恢复。
_lzma_static_x86_prepare:
	push	ebp
	mov	ebp, esp

	; 属性转成核心使用的常量：pb_mask=(1<<pb)-1、lp_mask=(1<<lp)-1；
	; lc 原值用于生成字面量概率上下文的移位量。
	mov	ecx, [ebp+16]
	mov	eax, 1
	shl	eax, cl
	dec	eax
	mov	byte [_rel_pb - 1], al

	mov	ecx, [ebp+12]
	mov	eax, 1
	shl	eax, cl
	dec	eax
	mov	byte [_rel_lp - 1], al

	mov	eax, [ebp+8]
	mov	byte [_rel_lc - 1], al

	pop	ebp
	ret

; -----------------------------------------------------------------------------
; 解码入口：cdecl run(range_src, dst, workspace, dst_end, lc, lp)
; 保存三个非易失寄存器并建立 EBP 帧后，参数槽为：
;   [EBP+20] range_src   压缩数据游标，值为原始 src+5
;   [EBP+24] dst         当前输出指针（解码过程中递增）
;   [EBP+28] workspace   概率表工作区
;   [EBP+32] dst_end     输出停止地址
;   [EBP+36] lc          字面量上下文属性
;   [EBP+40] lp          字面量位置属性
; 输入/输出/工作区指针直接使用 C 调用时的参数槽，不再复制到自定义栈槽。
_lzma_static_x86_run:
_exec_start:
	; 保存解码核心会改写的非易失寄存器，满足 Windows x86 调用约定。
	push	edi
	push	esi
	push	ebx
; LOC 宏以 EBP 为基准向负偏移分配 4 字节工作变量。核心栈帧变量布局：
;   [EBP-4]  Code       range decoder 当前编码值
;   [EBP-8]  Range      range decoder 当前区间宽度
;   [EBP-12..-24] _rep0.._rep3 最近使用的四个匹配距离
;   [EBP-28] _state     LZMA 状态机状态
; 这些变量在当前调用的栈上；workspace 另存概率模型表。LOC 的可选参数
; 可指定变量占用字节数及访问类型，也可用 none 省略显式类型；本文件使用
; 默认的 4 字节 dword 变量和 EBP 基址。
%assign loc_pos 0
%macro LOC 1-3 4, dword
%assign loc_pos loc_pos+%2
%ifidn %3, none
%xdefine %1 [ebp-loc_pos]
%else
%xdefine %1 %3 [ebp-loc_pos]
%endif
%endmacro
LOC Code
%assign loc_code loc_pos
LOC Range
%assign loc_range loc_pos
LOC _rep0
LOC _rep1
LOC _rep2
LOC _rep3
%assign loc_rep loc_pos
LOC _state

; 解码过程中 EDI 保持 _rc_bit1 的入口地址，CALL _rc_bit 等价于 CALL EDI。
%define _rc_bit edi
; C 传入的指针参数槽直接作为工作变量。Temp 指向 workspace；Dest 是会随输出
; 递增的当前写入指针；Src 是会随 range normalization 前进的输入指针；
; DestEnd 是输出停止地址。后面直接读写参数槽来更新 Src 和 Dest。
%define Src dword [ebp+20]
%define Dest dword [ebp+24]
%define Temp dword [ebp+28]
%define DestEnd dword [ebp+32]

; 建立解码入口的 EBP 栈帧。栈上保存 Code、Range、四个 rep 距离及状态槽；
; Code 从 range_src 前面的初始化字节直接读取。概率表项数在此按 lc+lp 计算，
; 概率模型表位于 C 传入的 workspace。
; 每个 uint16_t 概率项初始化为 1024（0x400），即自适应概率的中间值。

_start:	enter	0, 0
	; range_src=src+5，向前回退 4 字节正好指向 src[1..4]。读取后按
	; range decoder 需要的高位优先格式反转字节序，压入 Code 局部变量。
	mov	eax, Src
	mov	eax, [eax-4]
	bswap	eax
	push	eax		; Code 初值。
	push	-1		; Range 初值为 0xFFFFFFFF。
	xor	eax, eax
	inc	eax
	; 每个距离槽以 1 开始；这里保存的是“实际距离 + 1”的编码形式，
	; 因而代表初始实际距离 0。
	push	eax
	push	eax
	push	eax
	push	eax
	; 按概率布局计算项数：2048 + (768 << (lc + lp))。
	; REP STOSW 以 uint16_t 为单位，将整张概率表初始化为 0x400。
	mov	ecx, [ebp+36]
	add	ecx, [ebp+40]
	mov	edx, 768
	shl	edx, cl
	add	edx, 2048
	mov	ecx, edx
	mov	edi, Temp
	shl	eax, 10
	rep	stosw
	push	ecx		; REP STOSW 后 ECX=0，初始 LZMA state 为 0。
	; BH 的初值在后续位运算前会被覆盖，因此这里取 4 不影响解码结果。
	xchg	ebx, eax	; BL=0 作为初始前一字节；BH 的值不参与有效上下文。
	call	_loop1

	; 用 CALL/POP 技巧取得 _rc_bit1 的运行时地址并保存到 EDI；解码主体
	; 后续通过 CALL EDI 调用 range 位解码器。这样保留了紧凑的相对布局。
_rc_bit1:
	; 输入 ESI 指向一个 uint16_t 概率项。先规范化 Range/Code，再计算
	; bound=(Range>>11)*probability。比较 Code 和 bound 解出一个位，同时
	; 更新区间和概率；位结果通过 CF 返回给调用处。
	push	edx
	push	esi
	call	_rc_norm
	shl	esi, 1
	add	esi, Temp
	movzx	eax, word [esi]
	mov	edx, Range
	shr	edx, 11
	imul	edx, eax	; bound
	sub	Range, edx
	sub	Code, edx
	jae	.1
	mov	Range, edx
	add	Code, edx
	cdq
	sub	eax, 2048-31
.1:	shr	eax, 5		; 将概率更新量右移到非负范围。
	sub	[esi], ax
	neg	edx
	pop	esi
	pop	edx
	ret

_rc_norm:
	; Range 的最高字节为 0 时，range decoder 做 8 位归一化：Range、Code
	; 左移一个字节，并从 Src 读取下一个压缩字节补入 Code。否则不读输入。
	cmp	byte [ebp-loc_range+3], 0
	jne	.1
	shl	Range, 8
	shl	Code, 8
	mov	eax, Src
	mov	al, [eax]
	inc	Src
	mov	[ebp-loc_code], al
.1:	ret

; 从 CALL 留在栈上的返回地址取出 _rc_bit1 的地址；之后 EDI 作为位解码入口。
_loop1:	pop	_rc_bit
	; 主循环先检查输出指针。发布版达到 dst_end 即停止，不要求码流带 end marker。
_loop:	mov	eax, Dest
	cmp	eax, DestEnd
	jae	_end
	; ECX 暂存当前输出位置。BH 的立即数由准备入口修补为 lp_mask，形成
	; 字面量位置上下文；ECX 的立即数修补为 pb_mask，形成 posState。
	mov	ecx, Dest
	mov	bh, 0x55
_rel_lp:
	pop	esi		; _state
	push	esi
	and	bh, cl
	and	ecx, 0x55	; posState
_rel_pb:
	shl	esi, 5		; 每个状态有 16 个 uint16_t 项，即 32 字节。

	; 计算 IsMatch 概率项地址：状态块偏移 + posState 项偏移 + 表头偏移。
	lea	esi, [esi+ecx*2+64]
	; 解码 IsMatch[state][posState]：CF=0 表示字面量，CF=1 表示匹配。
	call	_rc_bit
	cdq
	pop	eax
	jc	_case_rep
	; 字面量路径。lc 决定字面量上下文中前一个字节的位移量；准备入口
	; 修补下条 SHL 的立即数。后续根据历史字节和已解出的高位选概率项。
	shl	ebx, 0x55
_rel_lc:
	mov	bl, 0
	lea	ecx, [ebx+ebx*2+2048]
_case_lit:
	; 逐位解出一个字面量字节。循环中 BL 保存正在生成的字节，ESI/ECX
	; 组合概率模型下标；完成 8 位后跳到公共输出写入处，写出这一字节。
	lea	ebx, [edx+1]
	; 用紧凑转换表实现字面量状态更新：旧 state<4 时归零；否则旧 state<10
	; 时减 3，旧 state>=10 时减 6。AL 是当前状态，更新结果压回 _state。
.4:	add	al, -3
	sbb	dl, dl
	and	al, dl
	cmp	al, 7
	jae	.4
	push	eax		; _state
%if 0	; 该替代实现少 2 字节，但执行较慢。
	; 此方案会从 Dest 前一个字节读取历史值。
	add	al, -4
	sbb	bh, bh
%else
	cmp	al, 7-3
	jb	.2
	mov	bh, 1	 ; 标记历史字节读取偏移。
%endif
	mov	eax, Dest
	sub	eax, _rep0
	; DL/DH 暂存当前匹配字节与上下文，BL 从最高位 1 开始逐位构造字节。
	xor	dl, [eax]
.1:	xor	dh, bl
	and	bh, dh
.2:	shl	edx, 1
	mov	esi, ebx
	and	esi, edx
	add	esi, ebx
	add	esi, ecx
	call	_rc_bit
	adc	bl, bl
	jnc	.1
	cdq	; 字面量只输出 1 字节，EDX 转成复制循环所需的长度计数形式。
	jmp	_copy.2

_case_rep:
; 当前符号是匹配。IsRep 决定重用历史距离还是解一个新距离；重用距离
; 时从 rep0..rep3 选择并更新距离队列。普通重复匹配随后解长度，rep0 短
; 匹配则直接进入单字节复制路径。
	mov	ebx, esi
	lea	esi, [edx+eax*4+16]	; IsRep 概率模型的状态相关起始偏移。
	add	al, -7
	sbb	al, al
	and	al, 3
	push	eax		; _state
	call	_rc_bit
	jc	.2
	pop	eax		; 暂存的 state。
	pop	ebx		; r3。
	pop	ebx		; r2。
	pop	esi		; r1。
	push	_rep0		; 新 r1 = 旧 r0。
	push	esi		; 新 r2 = 旧 r1。
	push	ebx		; 新 r3 = 旧 r2。
	push	eax		; _state
	; 重复匹配状态更新：旧 state<7 时转为 0，否则转为 3。
	mov	dl, 819/9	; 选择重复匹配长度模型 LenCoder。
	jmp	_case_len

.2:	; IsRepG0 为 0 时继续判断 rep0；为 1 时转去选择 rep1..rep3。
	inc	esi
	call	_rc_bit
	jc	.3
	lea	esi, [ebx+1]	; IsRep0Long 概率项。
	call	_rc_bit
	jc	.5
	; IsRep0Long 为 0 时使用长度 1，并按 state<7 ? 9 : 11 更新状态。
	or	_state, 9
	; 该分支的 EDX=0，表示长度计数为 1。
	jmp	_copy.1

.3:	; 选择 rep1/rep2/rep3 时，将该距离项交换到 rep0，并移动中间项。
	mov	dl, 3
	mov	ebx, _rep0
.6:	inc	esi
	dec	edx
	xchg	[ebp-loc_rep+edx*4], ebx
	je	.4
	call	_rc_bit
	jc	.6
.4:	mov	_rep0, ebx
.5:	; 普通重复匹配按 state<7 ? 8 : 11 更新状态。
	or	_state, 8
	mov	dl, 1332/9	; 选择重复匹配长度模型 RepLenCoder。
; 选择匹配长度概率子表并解码长度类别。短长度使用 3 位树，较长长度用
; 8 位树或额外高位；结果最终整理成 EDX=len-1，供后续输出复制循环使用。
_case_len:
	lea	esi, [edx*8+edx]
	cdq
	call	_rc_bit
	inc	esi
	lea	ebx, [esi+ecx*8]	; 位树基址；此紧凑表达式省去了单独加 1。
	mov	cl, 3
	jnc	.4
	inc	edx	; 切换到 8 位长度子树。
	call	_rc_bit
	jnc	.3
	; BitTree 表的首项不会使用，因此这里加 255 与加 256 等价，能省一字节。
	lea	ebx, [esi+127]
	mov	cl, 8
	add	edx, 16/8-(1<<8)/8	; 调整长度偏置，结果为 -29。
.3:	sub	ebx, -128	; 将长度子表基址加 128。
.4:	; BitTree
	push	1
	pop	esi
	push	esi
.5:	push	esi
	add	esi, ebx
	call	_rc_bit
	pop	esi
	adc	esi, esi
	loop	.5
	lea	ebx, [esi+edx*8+2-8-1]
	cmp	_state, 4
	pop	edx	; 恢复位树起始值/长度偏置暂存值。
	push	ebx	; 暂存解出的匹配长度，公共复制段稍后取出。
	jae	_copy
; 新距离先由 PosSlot 位树解出。较大 slot 的高位随后由概率模型、直接位
; 和对齐位共同组成完整距离。
_case_dist:
	add	_state, 7
	sub	ebx, 3+2-1
	sbb	eax, eax
	and	ebx, eax
	lea	ebx, [edx-1+ebx*8+(432+16-128)/8+(3+2)*8]	; PosSlot 模型基址。
	; 解 PosSlot 位树并计算对应的距离模型偏移。
	push	edx
.5:	lea	esi, [edx+ebx*8]
	call	_rc_bit
	adc	edx, edx
	mov	ecx, edx
	sub	ecx, 1<<6
	jb	.5
	pop	ebx	; 恢复位树起始值 1。
; slot<4 时距离已经完整；更大的距离从 SpecPos 概率模型解中间位，最高
; 位段使用不查概率表的 range 直接位，最后从对齐位树补齐低位。
_case_model:
	cmp	ecx, 4
	jb	.9
	mov	esi, ebx
	shr	ecx, 1
	rcl	ebx, cl
	dec	ecx
	not	dl	; 取反低字节，形成直接位解码使用的偏置。
	mov	dh, 2
	add	edx, ebx
	; 旧版显式计算 SpecPos 基址的表达式，保留作优化参考。
;	lea	edx, [edx+ebx+688+16+64-256*3]
	cmp	ecx, 6
	jb	.4
.1:	dec	ecx
	call	_rc_norm
	shr	Range, 1
	mov	edx, Range
	cmp	Code, edx
	jb	.3
	sub	Code, edx
	bts	ebx, ecx
.3:	cmp	ecx, 4
	jne	.1
	cdq		; 进入距离对齐低位解码阶段。
.4:
.5:	push	esi
	add	esi, edx
	call	_rc_bit
	pop	esi
	adc	esi, esi
	loop	.5
.6:	adc	ecx, ecx
	shr	esi, 1
	jne	.6
	add	ecx, ebx
.9:	inc	ecx
	mov	_rep0, ecx
	; 新距离解码结果加一后若回绕为 0，表示 LZMA end marker，结束解码。
	je	_end
_copy:	pop	edx
%ifdef LZMA_C_API_DEBUG
	; 调试版检查整个匹配长度是否超过剩余输出空间；发布版省略保护以减小代码。
	mov	ecx, edx
	inc	ecx
	mov	eax, DestEnd
	sub	eax, Dest
	cmp	ecx, eax
	ja	_end
%endif
.1:	mov	eax, Dest
	sub	eax, _rep0
	movzx	ebx, byte [eax]	; 从已经输出的历史位置取回待复制字节。
.2:	mov	eax, Dest
	mov	[eax], bl	; 将字面量或匹配字节写到当前输出位置。
	inc	Dest
	dec	edx		; 计数为 len-1；递减后仍非负就继续复制。
	jns	.1
	jmp	_loop

	; end marker、达到 dst_end 或调试版发现匹配越界都会到这里。先保存当前
	; 输出末地址作为返回值，再丢弃局部变量帧并恢复非易失寄存器；cdecl 调用者清栈。
_end:
	mov	eax, Dest
	leave
	pop	ebx
	pop	esi
	pop	edi
	ret
_exec_end:
_lzma_static_x86_section_end:
