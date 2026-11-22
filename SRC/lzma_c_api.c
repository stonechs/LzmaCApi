#include "lzma_c_api.h"
#include <windows.h>
#include <string.h>

/* Generated from upstream static.x86.asm (CC BY 3.0). */
#include "static_x86_blob.h"

typedef uint8_t *(__cdecl *decoder_entry)(void);

LzmaCApiResult lzma_c_api_decode(const uint8_t *src, size_t src_size,
                                 uint8_t *dst, size_t dst_capacity,
                                 size_t output_size, size_t *dst_size,
                                 unsigned lc, unsigned lp, unsigned pb,
                                 void *workspace, size_t workspace_size)
{
    size_t probs_count, code_len, prefix_len = 18, suffix_len = 7;
    size_t blob_size, executable_size;
    uint8_t *exec_mem, *p, *code;
    uint32_t *meta;
    uint8_t *end;

#ifdef LZMA_C_API_DEBUG
    /*
     * 调试构建检查调用前提：range stream 至少 5 字节且首字节为 0；
     * lc/lp/pb 必须在支持范围内。注意 src_size 只在这里检查最小长度，
     * 解码核心读取时仍不进行边界检查。
     */
    if (!src || !dst_size || !workspace || src_size < 5 || src[0] != 0 || lc > 8 ||
        lp > 4 || lc + lp > 12 || pb > 4)
        return LZMA_C_API_INVALID_ARGUMENT;
#endif

    /* 输出长度在解码开始后置零；失败时不会留下旧的成功长度。 */
    *dst_size = 0;
#ifdef LZMA_C_API_DEBUG
    /* 精简构建由调用者保证 dst 有效且容量足够，避免核心写越界。 */
    if (output_size > dst_capacity || (output_size && !dst))
        return LZMA_C_API_OUTPUT_TOO_SMALL;
#endif

    /* 概率表布局由当前核心决定；每项为 uint16_t。 */
    probs_count = 2048u + (768u << (lc + lp));
#ifdef LZMA_C_API_DEBUG
    if (workspace_size < probs_count * sizeof(uint16_t))
        return LZMA_C_API_WORKSPACE_TOO_SMALL;
#else
    (void)src_size;
    (void)dst_capacity;
    (void)workspace_size;
#endif

    /*
     * blob 开头的 24 字节是生成时写入的元数据：解码代码长度，以及
     * Code 初值、概率项数量和 lc/lp/pb 参数在代码模板中的修补偏移。
     * 先验证元数据中的代码长度与 blob 总长度一致，再准备可执行副本。
     */
    meta = (uint32_t *)(void *)lzma_static_x86_blob;
    code_len = meta[0];
    blob_size = sizeof(lzma_static_x86_blob);
    if (blob_size != 24u + code_len) return LZMA_C_API_INVALID_STREAM;
    executable_size = prefix_len + code_len + suffix_len;

    /*
     * 创建本次调用专用的 x86 代码缓冲区。前缀保存被核心使用的寄存器，
     * 并按核心约定压入输入、输出和 workspace 指针；中间是解码器副本，
     * 后缀负责清理参数、恢复寄存器并返回。缓冲区需要可执行权限。
     */
    exec_mem = (uint8_t *)VirtualAlloc(NULL, executable_size,
                                       MEM_COMMIT | MEM_RESERVE,
                                       PAGE_EXECUTE_READWRITE);
    if (!exec_mem) return LZMA_C_API_ALLOCATION_FAILED;

    /* 构造入口前缀：保存寄存器，并把解码器所需的三个指针压入栈中。 */
    p = exec_mem;
    *p++ = 0x57; *p++ = 0x56; *p++ = 0x53; /* save EDI, ESI, EBX */
    *p++ = 0x68; *(uint32_t *)(void *)p = (uint32_t)(uintptr_t)(src + 5); p += 4;
    *p++ = 0x68; *(uint32_t *)(void *)p = (uint32_t)(uintptr_t)dst; p += 4;
    *p++ = 0x68; *(uint32_t *)(void *)p = (uint32_t)(uintptr_t)workspace; p += 4;

    /*
     * 复制只读模板，避免并发调用时互相改写全局模板；然后按本次属性
     * 修补概率项数量、pb/lp 掩码和 lc 值。meta 中的偏移由汇编源生成，
     * 因此这里不依赖这些立即数在机器码中的固定位置。
     */
    code = p;
    memcpy(code, lzma_static_x86_blob + 24, code_len);
    *(uint32_t *)(void *)(code + meta[2]) = (uint32_t)probs_count;
    code[meta[3]] = (uint8_t)((1u << pb) - 1u);
    code[meta[4]] = (uint8_t)((1u << lp) - 1u);
    code[meta[5]] = (uint8_t)lc;

    /* 追加入口后缀，与上面的寄存器保存和参数压栈顺序相配套。 */
    p = code + code_len;
    *p++ = 0x59; *p++ = 0x58; *p++ = 0x59;
    *p++ = 0x5f; *p++ = 0x5e; *p++ = 0x5b; *p++ = 0xc3;

    /* 检查前缀、模板和后缀长度是否与计算值相符；异常时释放内存。 */
    if ((size_t)(p - exec_mem) != executable_size) {
        VirtualFree(exec_mem, 0, MEM_RELEASE);
        return LZMA_C_API_INVALID_STREAM;
    }

    /*
     * range stream 的 src[0..4] 是五字节初始化段。前缀已把输入指针设为
     * src+5，所以把 src[1..4] 转成大端整数，写入模板的 Code 初始值位置；
     * 后续 range normalization 从 src+5 继续取字节。
     */
    *(uint32_t *)(void *)(code + meta[1]) =
        ((uint32_t)src[1] << 24) | ((uint32_t)src[2] << 16) |
        ((uint32_t)src[3] << 8) | src[4];

    /* 执行专用副本。核心返回最终输出指针；无论成功或失败都立即释放代码页。 */
    end = ((decoder_entry)(void *)exec_mem)();
    VirtualFree(exec_mem, 0, MEM_RELEASE);

    /*
     * 该核心以 end marker 作为停止条件。若 marker 提前出现或实际输出长度
     * 与外层容器给出的长度不同，视为码流无效；只有长度完全匹配才报告成功。
     */
    if (end < dst || (size_t)(end - dst) != output_size)
        return LZMA_C_API_INVALID_STREAM;
    *dst_size = (size_t)(end - dst);
    return LZMA_C_API_OK;
}
