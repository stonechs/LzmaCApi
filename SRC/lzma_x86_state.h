#ifndef LZMA_X86_STATE_H
#define LZMA_X86_STATE_H

#ifdef LZMA_C_API_LZMA2
#include <stdint.h>

/* 由 C 持有并传给统一汇编入口的跨压缩块状态。 */
typedef struct LzmaX86State {
    uint32_t state;
    uint32_t rep0;
    uint32_t rep1;
    uint32_t rep2;
    uint32_t rep3;
    uint8_t previous;
    uint8_t reserved[3];
    uint32_t position_bias;
} LzmaX86State;

static void lzma_x86_state_reset(LzmaX86State *state)
{
    state->state = 0;
    state->rep0 = 1;
    state->rep1 = 1;
    state->rep2 = 1;
    state->rep3 = 1;
    state->previous = 0;
    state->reserved[0] = 0;
    state->reserved[1] = 0;
    state->reserved[2] = 0;
    state->position_bias = 0;
}
#endif

#endif
