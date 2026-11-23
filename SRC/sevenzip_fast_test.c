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
    size_t archive_size, expected_size, output_size = 0;
    uint8_t *archive, *expected, *output;
    SevenZipResult result;
    if (argc != 3) return 2;
    archive = read_file(argv[1], &archive_size);
    expected = read_file(argv[2], &expected_size);
    output = (uint8_t *)malloc(expected_size ? expected_size : 1);
    if (!archive || !expected || !output) return 3;
    result = sevenzip_extract_single_lzma1_fast(
        archive, archive_size, output, expected_size, &output_size);
    if (result != SEVENZIP_OK || output_size != expected_size ||
        memcmp(output, expected, expected_size) != 0) {
        fprintf(stderr, "fast extract failed: result=%d size=%lu expected=%lu\n",
                (int)result, (unsigned long)output_size,
                (unsigned long)expected_size);
        return 1;
    }
    puts("fast archive decode passed");
    free(output);
    free(expected);
    free(archive);
    return 0;
}
