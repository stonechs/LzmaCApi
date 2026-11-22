#include "lzma_c_api.h"

/* Implemented by the statically linked NASM decoder. */
#ifdef LZMA_C_API_DEBUG
extern uint8_t *__cdecl lzma_static_x86_run(
    const uint8_t *src, uint8_t *dst, void *workspace, size_t output_size,
    size_t workspace_size, unsigned lc, unsigned lp, unsigned pb);
#else
extern void __cdecl lzma_static_x86_run(
    const uint8_t *src, uint8_t *dst, void *workspace, size_t output_size,
    size_t workspace_size, unsigned lc, unsigned lp, unsigned pb);
#endif

LzmaCApiResult lzma_c_api_decode(const uint8_t *src, size_t src_size,
                                 uint8_t *dst, size_t dst_capacity,
                                 size_t output_size, size_t *dst_size,
                                 unsigned lc, unsigned lp, unsigned pb,
                                 void *workspace, size_t workspace_size)
{
#ifdef LZMA_C_API_DEBUG
    uint8_t *end;
#endif

#ifdef LZMA_C_API_DEBUG
    /* 调试构建检查输入和属性前提；解码核心本身仍不检查输入边界。 */
    if (!src || !dst_size || !workspace || src_size < 5 || src[0] != 0 || lc > 8 ||
        lp > 4 || lc + lp > 12 || pb > 4)
        return LZMA_C_API_INVALID_ARGUMENT;
    *dst_size = 0;
    if (output_size > dst_capacity || (output_size && !dst))
        return LZMA_C_API_OUTPUT_TOO_SMALL;
    if (workspace_size < (2048u + (768u << (lc + lp))) * sizeof(uint16_t))
        return LZMA_C_API_WORKSPACE_TOO_SMALL;
#else
    (void)src_size;
    (void)dst_capacity;
    (void)dst_size;
#endif

    /* 汇编直接读取参数；src 偏移和 workspace 概率表初始化由解码器处理。 */
#ifdef LZMA_C_API_DEBUG
    end = lzma_static_x86_run(
        src, dst, workspace, output_size, workspace_size, lc, lp, pb);

    if (end < dst || (size_t)(end - dst) != output_size)
        return LZMA_C_API_INVALID_STREAM;
    *dst_size = (size_t)(end - dst);
#else
    lzma_static_x86_run(
        src, dst, workspace, output_size, workspace_size, lc, lp, pb);
#endif
    return LZMA_C_API_OK;
}
