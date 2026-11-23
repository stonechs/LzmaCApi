#ifndef LZMA_X86_STATE_H
#define LZMA_X86_STATE_H

#include <stdint.h>

/* LZMA1 汇编约定：固定 24 字节状态前缀，概率表紧随其后。 */
#define LZMA_X86_CONTEXT_SIZE 24u

typedef struct LzmaX86Context {
    uint32_t state;
    uint32_t rep0;
    uint32_t rep1;
    uint32_t rep2;
    uint32_t rep3;
    uint8_t previous;
    uint8_t reserved[3];
} LzmaX86Context;

typedef char LzmaX86Context_size_must_be_24_bytes[
    sizeof(LzmaX86Context) == LZMA_X86_CONTEXT_SIZE ? 1 : -1];

static inline uint16_t *lzma_x86_probabilities(LzmaX86Context *context)
{
    return (uint16_t *)((uint8_t *)context + sizeof(*context));
}

static inline void lzma_x86_context_reset(LzmaX86Context *context)
{
    context->state = 0;
    context->rep0 = 1;
    context->rep1 = 1;
    context->rep2 = 1;
    context->rep3 = 1;
    context->previous = 0;
    context->reserved[0] = 0;
    context->reserved[1] = 0;
    context->reserved[2] = 0;
}

#endif
