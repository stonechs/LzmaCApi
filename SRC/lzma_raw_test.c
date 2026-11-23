#define _CRT_SECURE_NO_WARNINGS
#include "sevenzip_single.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *data;
    if (!file || fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        if (file) fclose(file);
        return NULL;
    }
    data = (uint8_t *)malloc((size_t)length ? (size_t)length : 1);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

int main(int argc, char **argv)
{
    size_t stream_size, expected_size;
    uint8_t *stream, *expected, *output;
    void *workspace;
    unsigned prop, lc, lp, pb;

    if (argc != 3) return 2;
    stream = read_file(argv[1], &stream_size);
    expected = read_file(argv[2], &expected_size);
    if (!stream || !expected || stream_size < 18 || stream[13] != 0)
        return 3;
    prop = stream[0];
    if (prop >= 225) return 4;
    lc = prop % 9;
    lp = (prop / 9) % 5;
    pb = prop / 45;
    if (lc + lp > 12 || pb > 4) return 4;

    output = (uint8_t *)malloc(expected_size ? expected_size : 1);
    workspace = malloc(6295552u);
    if (!output || !workspace) return 5;
#ifdef LZMA_C_API_DEBUG
    if (lzma_static_x86_run(stream + 13, output, workspace,
                            expected_size, lc, lp, pb) != output + expected_size)
        return 6;
#else
    lzma_static_x86_run(stream + 13, output, workspace,
                        expected_size, lc, lp, pb);
#endif
    if (memcmp(output, expected, expected_size) != 0) return 7;
    puts("raw LZMA1 decode passed");
    free(workspace);
    free(output);
    free(expected);
    free(stream);
    return 0;
}
