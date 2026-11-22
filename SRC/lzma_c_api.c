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
    size_t probs_count;
    uint8_t *exec_mem;
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
     * 创建本次调用专用的可执行代码缓冲区。完整入口、解码主体和返回
     * 清理代码都由汇编模板提供；本函数只复制模板并填入本次调用参数。
     */
    exec_mem = (uint8_t *)VirtualAlloc(NULL, sizeof(lzma_static_x86_blob),
                                       MEM_COMMIT | MEM_RESERVE,
                                       PAGE_EXECUTE_READWRITE);
    if (!exec_mem) return LZMA_C_API_ALLOCATION_FAILED;

    /*
     * 复制纯可执行模板，避免并发调用时互相改写全局模板。修补偏移由
     * 构建脚本从汇编元数据提取为编译期常量，不放入运行时模板。
     */
    memcpy(exec_mem, lzma_static_x86_blob, sizeof(lzma_static_x86_blob));
    *(uint32_t *)(void *)(exec_mem + LZMA_STATIC_X86_PATCH_PROBS) =
        (uint32_t)probs_count;
    exec_mem[LZMA_STATIC_X86_PATCH_PB] = (uint8_t)((1u << pb) - 1u);
    exec_mem[LZMA_STATIC_X86_PATCH_LP] = (uint8_t)((1u << lp) - 1u);
    exec_mem[LZMA_STATIC_X86_PATCH_LC] = (uint8_t)lc;
    *(uint32_t *)(void *)(exec_mem + LZMA_STATIC_X86_PATCH_SRC) =
        (uint32_t)(uintptr_t)(src + 5);
    *(uint32_t *)(void *)(exec_mem + LZMA_STATIC_X86_PATCH_DST) =
        (uint32_t)(uintptr_t)dst;
    *(uint32_t *)(void *)(exec_mem + LZMA_STATIC_X86_PATCH_WORKSPACE) =
        (uint32_t)(uintptr_t)workspace;
    *(uint32_t *)(void *)(exec_mem + LZMA_STATIC_X86_PATCH_DST_END) =
        (uint32_t)((uintptr_t)dst + (uintptr_t)output_size);

    /*
     * range stream 的 src[0..4] 是五字节初始化段。汇编入口把输入指针设为
     * src+5，所以把 src[1..4] 转成大端整数，写入模板的 Code 初始值位置；
     * 后续 range normalization 从 src+5 继续取字节。
     */
    *(uint32_t *)(void *)(exec_mem + LZMA_STATIC_X86_PATCH_CODE_INIT) =
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
