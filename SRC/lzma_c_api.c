#include "lzma_c_api.h"
#include "lzma_x86_state.h"

static void initialize_probabilities(void *workspace, size_t count)
{
    uint16_t *p = lzma_x86_probabilities((LzmaX86Context *)workspace);
    size_t i;
    for (i = 0; i < count; ++i)
        p[i] = 1024;
}

/* Implemented by the statically linked NASM decoder. */
#ifdef LZMA_C_API_DEBUG
extern uint8_t *__cdecl lzma_static_x86_run(
    const uint8_t *src, uint8_t *dst, void *context, size_t output_size,
    unsigned lc, unsigned lp, unsigned pb);
#else
extern void __cdecl lzma_static_x86_run(
    const uint8_t *src, uint8_t *dst, void *context, size_t output_size,
    unsigned lc, unsigned lp, unsigned pb);
#endif

LzmaCApiResult lzma_c_api_decode(const uint8_t *src, size_t src_size,
                                 uint8_t *dst, size_t dst_capacity,
                                 size_t output_size, size_t *dst_size,
                                 unsigned lc, unsigned lp, unsigned pb,
                                 void *workspace, size_t workspace_size)
{
    size_t probability_count;
    LzmaX86Context *context = (LzmaX86Context *)workspace;
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
#else
    (void)src_size;
    (void)dst_capacity;
    (void)dst_size;
#endif

    /* 属性通过调试检查后只计算一次，容量校验和概率表初始化共用此值。 */
    probability_count = 2048u + (768u << (lc + lp));
#ifdef LZMA_C_API_DEBUG
    if (workspace_size < LZMA_C_API_CONTEXT_SIZE +
                         probability_count * sizeof(uint16_t))
        return LZMA_C_API_WORKSPACE_TOO_SMALL;
#endif

    /* 所有构建共用一个 context：状态在前，C 初始化的概率表在后。 */
    lzma_x86_context_reset(context);
    initialize_probabilities(workspace, probability_count);
#ifdef LZMA_C_API_LZMA2
    context->position_bias = 0u - (uint32_t)(uintptr_t)dst;
#endif
#ifdef LZMA_C_API_DEBUG
    end = lzma_static_x86_run(
        src, dst, context, output_size, lc, lp, pb);

    if (end < dst || (size_t)(end - dst) != output_size)
        return LZMA_C_API_INVALID_STREAM;
    *dst_size = (size_t)(end - dst);
#else
    lzma_static_x86_run(
        src, dst, context, output_size, lc, lp, pb);
#endif
    return LZMA_C_API_OK;
}
