#include "lzma_c_api.h"
#include <windows.h>
#include <string.h>

/* Generated from upstream static.x86.asm (CC BY 3.0). */
#include "static_x86_blob.h"

typedef uint8_t *(__cdecl *decoder_entry)(void);

size_t lzma_c_api_workspace_size(unsigned lc, unsigned lp)
{
    if (lc > 8 || lp > 4 || lc + lp > 12) return 0;
    return (2048u + (768u << (lc + lp))) * sizeof(uint16_t);
}

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

    if (!src || !dst_size || !workspace || src_size < 5 || src[0] != 0 || lc > 8 ||
        lp > 4 || lc + lp > 12 || pb > 4)
        return LZMA_C_API_INVALID_ARGUMENT;
    *dst_size = 0;
    if (output_size > dst_capacity || (output_size && !dst))
        return LZMA_C_API_OUTPUT_TOO_SMALL;
    probs_count = 2048u + (768u << (lc + lp));
    if (workspace_size < probs_count * sizeof(uint16_t))
        return LZMA_C_API_WORKSPACE_TOO_SMALL;

    meta = (uint32_t *)(void *)lzma_static_x86_blob;
    code_len = meta[0];
    blob_size = sizeof(lzma_static_x86_blob);
    if (blob_size != 24u + code_len) return LZMA_C_API_INVALID_STREAM;
    executable_size = prefix_len + code_len + suffix_len;
    exec_mem = (uint8_t *)VirtualAlloc(NULL, executable_size,
                                       MEM_COMMIT | MEM_RESERVE,
                                       PAGE_EXECUTE_READWRITE);
    if (!exec_mem) return LZMA_C_API_ALLOCATION_FAILED;

    p = exec_mem;
    *p++ = 0x57; *p++ = 0x56; *p++ = 0x53; /* save EDI, ESI, EBX */
    *p++ = 0x68; *(uint32_t *)(void *)p = (uint32_t)(uintptr_t)(src + 5); p += 4;
    *p++ = 0x68; *(uint32_t *)(void *)p = (uint32_t)(uintptr_t)dst; p += 4;
    *p++ = 0x68; *(uint32_t *)(void *)p = (uint32_t)(uintptr_t)workspace; p += 4;
    code = p;
    memcpy(code, lzma_static_x86_blob + 24, code_len);
    *(uint32_t *)(void *)(code + meta[2]) = (uint32_t)probs_count;
    code[meta[3]] = (uint8_t)((1u << pb) - 1u);
    code[meta[4]] = (uint8_t)((1u << lp) - 1u);
    code[meta[5]] = (uint8_t)lc;
    p = code + code_len;
    *p++ = 0x59; *p++ = 0x58; *p++ = 0x59;
    *p++ = 0x5f; *p++ = 0x5e; *p++ = 0x5b; *p++ = 0xc3;
    if ((size_t)(p - exec_mem) != executable_size) {
        VirtualFree(exec_mem, 0, MEM_RELEASE);
        return LZMA_C_API_INVALID_STREAM;
    }
    /* Adapt raw range stream to the original static decoder's entry layout:
     * its immediate Code is initialized from src[1..4], then reads at +5. */
    *(uint32_t *)(void *)(code + meta[1]) =
        ((uint32_t)src[1] << 24) | ((uint32_t)src[2] << 16) |
        ((uint32_t)src[3] << 8) | src[4];
    end = ((decoder_entry)(void *)exec_mem)();
    VirtualFree(exec_mem, 0, MEM_RELEASE);
    if (end < dst || (size_t)(end - dst) != output_size)
        return LZMA_C_API_INVALID_STREAM;
    *dst_size = (size_t)(end - dst);
    return LZMA_C_API_OK;
}
