#include "lzma_c_api.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    FILE *f;
    long n;
    uint8_t *src, *dst;
    size_t out_size = 0, ws_size;
    void *ws;
    LzmaCApiResult rc;
    if (argc != 3) return 2;
    f = fopen(argv[1], "rb"); if (!f) return 3;
    fseek(f, 0, SEEK_END); n = ftell(f); rewind(f);
    src = (uint8_t *)malloc((size_t)n);
    if (!src || fread(src, 1, (size_t)n, f) != (size_t)n) return 4;
    fclose(f);
    f = fopen(argv[2], "rb"); if (!f) return 5;
    fseek(f, 0, SEEK_END); long expected = ftell(f); rewind(f);
    dst = (uint8_t *)malloc((size_t)expected); if (!dst) return 6;
    ws_size = (2048u + (768u << (3 + 0))) * sizeof(uint16_t);
    ws = malloc(ws_size); if (!ws) return 7;
    rc = lzma_c_api_decode(src, (size_t)n, dst, (size_t)expected,
                           (size_t)expected, &out_size, 3, 0, 2, ws, ws_size);
    if (rc != LZMA_C_API_OK || out_size != (size_t)expected) return 8;
    f = fopen(argv[2], "rb"); if (!f) return 9;
    for (size_t i = 0; i < out_size; ++i) {
        int c = fgetc(f); if (c == EOF || dst[i] != (uint8_t)c) return 10;
    }
    fclose(f);
    printf("decoded %lu bytes; workspace %lu bytes\n",
           (unsigned long)out_size, (unsigned long)ws_size);
    return 0;
}
