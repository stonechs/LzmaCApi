#include "sevenzip_single.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *data;
    if (!file || fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
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
    result = sevenzip_extract_single_lzma1(archive, archive_size, output,
                                           expected_size, &output_size);
    if (result != SEVENZIP_OK || output_size != expected_size ||
        memcmp(output, expected, expected_size) != 0) {
        fprintf(stderr, "extract failed: result=%d size=%lu expected=%lu\n",
                (int)result, (unsigned long)output_size,
                (unsigned long)expected_size);
        return 1;
    }

#ifdef LZMA_C_API_DEBUG
    /* 调试版检查签名、StartHeaderCRC 和 NextHeaderCRC。 */
    archive[0] ^= 1;
    result = sevenzip_extract_single_lzma1(archive, archive_size, output,
                                           expected_size, &output_size);
    if (result != SEVENZIP_INVALID_ARCHIVE) {
        fprintf(stderr, "signature check failed: result=%d\n", (int)result);
        return 1;
    }
    archive[0] ^= 1;
    archive[8] ^= 1;
    result = sevenzip_extract_single_lzma1(archive, archive_size, output,
                                           expected_size, &output_size);
    if (result != SEVENZIP_CRC_ERROR) {
        fprintf(stderr, "start header CRC check failed: result=%d\n", (int)result);
        return 1;
    }
    archive[8] ^= 1;
    archive[28] ^= 1;
    result = sevenzip_extract_single_lzma1(archive, archive_size, output,
                                           expected_size, &output_size);
    if (result != SEVENZIP_CRC_ERROR) {
        fprintf(stderr, "next header CRC check failed: result=%d\n", (int)result);
        return 1;
    }
    archive[28] ^= 1;
#else
    /* 发布版不校验签名和 CRC；只确认这些检查未混入发布流程。 */
    archive[0] ^= 1;
    archive[8] ^= 1;
    archive[28] ^= 1;
    result = sevenzip_extract_single_lzma1(archive, archive_size, output,
                                           expected_size, &output_size);
    if (result != SEVENZIP_OK || output_size != expected_size ||
        memcmp(output, expected, expected_size) != 0) {
        fprintf(stderr, "release unexpectedly depends on signature/CRC: result=%d\n",
                (int)result);
        return 1;
    }
#endif

    if (expected_size) {
        result = sevenzip_extract_single_lzma1(archive, archive_size, output,
                                               expected_size - 1, &output_size);
        if (result != SEVENZIP_OUTPUT_TOO_SMALL) {
            fprintf(stderr, "output capacity check failed: result=%d\n", (int)result);
            return 1;
        }
    }
    puts("7z single-file extraction checks passed");
    free(output);
    free(expected);
    free(archive);
    return 0;
}
