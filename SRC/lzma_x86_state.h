#ifndef LZMA_X86_STATE_H
#define LZMA_X86_STATE_H

#include <stdint.h>
#include "lzma_c_api.h"

/* 所有路径统一使用的工作区前缀；概率表紧随结构体之后。 */
typedef struct LzmaX86Context {
    uint32_t state;
    uint32_t rep0;
    uint32_t rep1;
    uint32_t rep2;
    uint32_t rep3;
    uint8_t previous;
    uint8_t reserved[3];
    uint32_t position_bias;
} LzmaX86Context;

typedef char LzmaX86Context_size_must_match_public_constant[
    sizeof(LzmaX86Context) == LZMA_C_API_CONTEXT_SIZE ? 1 : -1];

static uint16_t *lzma_x86_probabilities(LzmaX86Context *context)
{
    return (uint16_t *)((uint8_t *)context + sizeof(*context));
}

static void lzma_x86_context_reset(LzmaX86Context *context)
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
    context->position_bias = 0;
}

#endif
