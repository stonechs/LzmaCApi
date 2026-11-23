#include "lzma_c_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_file(const char *path, uint8_t **data, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long length;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (length = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    *data = (uint8_t *)malloc(length ? (size_t)length : 1);
    if (!*data || fread(*data, 1, (size_t)length, f) != (size_t)length) {
        free(*data);
        fclose(f);
        return 0;
    }
    fclose(f);
    *size = (size_t)length;
    return 1;
}

static int expect_result(const char *name, LzmaCApiResult got,
                         LzmaCApiResult expected)
{
    if (got == expected) return 1;
    fprintf(stderr, "%s: expected result %d, got %d\n",
            name, (int)expected, (int)got);
    return 0;
}

static int test_negative_returns(const uint8_t *src, size_t src_size,
                                 uint8_t *dst, size_t dst_size,
                                 void *workspace, size_t workspace_size,
                                 unsigned lc, unsigned lp, unsigned pb)
{
    size_t reported = 99;
    uint8_t bad_init[5] = {1, 0, 0, 0, 0};
    LzmaCApiResult rc;
#define CHECK(label, expression, expected) \
    do { if (!expect_result(label, (expression), (expected))) return 0; } while (0)

    CHECK("null src", lzma_c_api_decode(NULL, src_size, dst, dst_size,
          dst_size, &reported, lc, lp, pb, workspace, workspace_size),
          LZMA_C_API_INVALID_ARGUMENT);
    CHECK("null dst_size", lzma_c_api_decode(src, src_size, dst, dst_size,
          dst_size, NULL, lc, lp, pb, workspace, workspace_size),
          LZMA_C_API_INVALID_ARGUMENT);
    CHECK("null workspace", lzma_c_api_decode(src, src_size, dst, dst_size,
          dst_size, &reported, lc, lp, pb, NULL, workspace_size),
          LZMA_C_API_INVALID_ARGUMENT);
    CHECK("short range initializer", lzma_c_api_decode(src, 4, dst, dst_size,
          dst_size, &reported, lc, lp, pb, workspace, workspace_size),
          LZMA_C_API_INVALID_ARGUMENT);
    CHECK("nonzero range initializer", lzma_c_api_decode(bad_init,
          sizeof(bad_init), dst, dst_size, dst_size, &reported, lc, lp, pb,
          workspace, workspace_size), LZMA_C_API_INVALID_ARGUMENT);
    CHECK("lc out of range", lzma_c_api_decode(src, src_size, dst, dst_size,
          dst_size, &reported, 9, 0, pb, workspace, workspace_size),
          LZMA_C_API_INVALID_ARGUMENT);
    CHECK("lp out of range", lzma_c_api_decode(src, src_size, dst, dst_size,
          dst_size, &reported, 0, 5, pb, workspace, workspace_size),
          LZMA_C_API_INVALID_ARGUMENT);
    CHECK("pb out of range", lzma_c_api_decode(src, src_size, dst, dst_size,
          dst_size, &reported, lc, lp, 5, workspace, workspace_size),
          LZMA_C_API_INVALID_ARGUMENT);

    reported = 99;
    rc = lzma_c_api_decode(src, src_size, dst, dst_size - 1, dst_size,
                           &reported, lc, lp, pb, workspace, workspace_size);
    CHECK("output too small", rc, LZMA_C_API_OUTPUT_TOO_SMALL);
    if (reported != 0) {
        fprintf(stderr, "output too small: dst_size was not cleared\n");
        return 0;
    }
    CHECK("null output for nonempty result", lzma_c_api_decode(src, src_size,
          NULL, dst_size, dst_size, &reported, lc, lp, pb, workspace,
          workspace_size), LZMA_C_API_OUTPUT_TOO_SMALL);

    reported = 99;
    rc = lzma_c_api_decode(src, src_size, dst, dst_size, dst_size,
                           &reported, lc, lp, pb, workspace,
                           workspace_size - 1);
    CHECK("workspace too small", rc, LZMA_C_API_WORKSPACE_TOO_SMALL);
    if (reported != 0) {
        fprintf(stderr, "workspace too small: dst_size was not cleared\n");
        return 0;
    }
#undef CHECK
    return 1;
}

int main(int argc, char **argv)
{
    uint8_t *src = NULL, *expected = NULL, *dst = NULL;
    size_t src_size = 0, expected_size = 0, workspace_size, decoded = 0;
    unsigned lc, lp, pb;
    void *workspace = NULL;
    int i, ok = 0;

    if (argc != 6) {
        fprintf(stderr, "usage: api_test <raw> <expected> <lc> <lp> <pb>\n");
        return 2;
    }
    lc = (unsigned)strtoul(argv[3], NULL, 10);
    lp = (unsigned)strtoul(argv[4], NULL, 10);
    pb = (unsigned)strtoul(argv[5], NULL, 10);
    if (!read_file(argv[1], &src, &src_size) ||
        !read_file(argv[2], &expected, &expected_size)) {
        fprintf(stderr, "could not read fixture files\n");
        goto done;
    }
    dst = (uint8_t *)malloc(expected_size ? expected_size : 1);
    workspace_size = LZMA_C_API_CONTEXT_SIZE +
                     (2048u + (768u << (lc + lp))) * sizeof(uint16_t);
    workspace = malloc(workspace_size);
    if (!dst || !workspace || workspace_size == 0) {
        fprintf(stderr, "could not allocate test buffers\n");
        goto done;
    }
    memset(dst, 0xa5, expected_size);
    if (lzma_c_api_decode(src, src_size, dst, expected_size, expected_size,
                          &decoded, lc, lp, pb, workspace,
                          workspace_size) != LZMA_C_API_OK) {
        fprintf(stderr, "valid decode failed for lc=%u lp=%u pb=%u\n",
                lc, lp, pb);
        goto done;
    }
    if (decoded != expected_size || memcmp(dst, expected, expected_size) != 0) {
        fprintf(stderr, "decoded output mismatch for lc=%u lp=%u pb=%u\n",
                lc, lp, pb);
        goto done;
    }
    if (!test_negative_returns(src, src_size, dst, expected_size, workspace,
                               workspace_size, lc, lp, pb))
        goto done;
    printf("PASS lc=%u lp=%u pb=%u input=%lu output=%lu\n", lc, lp, pb,
           (unsigned long)src_size, (unsigned long)expected_size);
    ok = 1;

done:
    free(workspace);
    free(dst);
    free(expected);
    free(src);
    return ok ? 0 : 1;
}
