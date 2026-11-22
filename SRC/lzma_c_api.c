#include "lzma_c_api.h"
#include <windows.h>
#include <string.h>

/* Generated from upstream static.x86.asm (CC BY 3.0). */
#ifdef LZMA_C_API_DEBUG
#include "static_x86_blob_debug.h"
#else
#include "static_x86_blob.h"
#endif

typedef uint8_t *(__cdecl *decoder_entry)(void);

LzmaCApiResult lzma_c_api_decode(const uint8_t *src, size_t src_size,
                                 uint8_t *dst, size_t dst_capacity,
                                 size_t output_size, size_t *dst_size,
                                 unsigned lc, unsigned lp, unsigned pb,
                                 void *workspace, size_t workspace_size)
{
    size_t probs_count, code_len;
    size_t executable_size;
#ifdef LZMA_C_API_DEBUG
    size_t blob_size;
#endif
    uint8_t *exec_mem, *code;
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
     * blob 开头的 40 字节是生成时写入的元数据：完整入口代码长度，以及
     * Code 初值、概率项数量、lc/lp/pb 和四个指针立即数的修补偏移。
     * 元数据长度一致性只在调试构建中检查。
     */
    meta = (uint32_t *)(void *)lzma_static_x86_blob;
    code_len = meta[0];
#ifdef LZMA_C_API_DEBUG
    blob_size = sizeof(lzma_static_x86_blob);
    if (blob_size != 40u + code_len) return LZMA_C_API_INVALID_STREAM;
#endif
    executable_size = code_len;

    /*
     * 创建本次调用专用的可执行代码缓冲区。完整入口、解码主体和返回
     * 清理代码都由汇编模板提供；本函数只复制模板并填入本次调用参数。
     */
    exec_mem = (uint8_t *)VirtualAlloc(NULL, executable_size,
                                       MEM_COMMIT | MEM_RESERVE,
                                       PAGE_EXECUTE_READWRITE);
    if (!exec_mem) return LZMA_C_API_ALLOCATION_FAILED;

    /*
     * 复制只读模板，避免并发调用时互相改写全局模板。偏移均由汇编源
     * 生成并记录在元数据中。分别填入输入/输出/workspace 指针，以及
     * 概率项数量和本次 LZMA 属性值。
     */
    code = exec_mem;
    memcpy(code, lzma_static_x86_blob + 40, code_len);
    *(uint32_t *)(void *)(code + meta[2]) = (uint32_t)probs_count;
    code[meta[3]] = (uint8_t)((1u << pb) - 1u);
    code[meta[4]] = (uint8_t)((1u << lp) - 1u);
    code[meta[5]] = (uint8_t)lc;
    *(uint32_t *)(void *)(code + meta[6]) = (uint32_t)(uintptr_t)(src + 5);
    *(uint32_t *)(void *)(code + meta[7]) = (uint32_t)(uintptr_t)dst;
    *(uint32_t *)(void *)(code + meta[8]) = (uint32_t)(uintptr_t)workspace;
    *(uint32_t *)(void *)(code + meta[9]) =
        (uint32_t)((uintptr_t)dst + (uintptr_t)output_size);

    /*
     * range stream 的 src[0..4] 是五字节初始化段。汇编入口把输入指针设为
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
     * 调试构建检查实际输出长度是否等于调用者给出的长度；精简构建省略
     * 此检查，并通过 dst_size 返回汇编核心实际产生的字节数。
     */
#ifdef LZMA_C_API_DEBUG
    if (end < dst || (size_t)(end - dst) != output_size)
        return LZMA_C_API_INVALID_STREAM;
#endif
    *dst_size = (size_t)(end - dst);
    return LZMA_C_API_OK;
}
