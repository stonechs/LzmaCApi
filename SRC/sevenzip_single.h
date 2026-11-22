#ifndef SEVENZIP_SINGLE_H
#define SEVENZIP_SINGLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SevenZipResult {
    SEVENZIP_OK = 0,
    SEVENZIP_INVALID_ARGUMENT,
    SEVENZIP_INVALID_ARCHIVE,
    SEVENZIP_UNSUPPORTED,
    SEVENZIP_OUTPUT_TOO_SMALL,
    SEVENZIP_MEMORY_ERROR,
    SEVENZIP_DECODE_ERROR,
    SEVENZIP_CRC_ERROR
} SevenZipResult;

/*
 * 在内存中解析并解压只含一个非空文件的 7z 文档。
 * 当前仅支持单个 LZMA1 coder、单个 packed stream；不支持 LZMA2、加密、
 * 多文件、多流、滤镜链或分卷。可识别普通头部和 LZMA1 编码的压缩头部。
 * 目标缓冲区由调用方提供；内部会临时分配概率 workspace 和压缩头缓冲区。
 */
SevenZipResult sevenzip_extract_single_lzma1(
    const uint8_t *archive, size_t archive_size,
    uint8_t *output, size_t output_capacity, size_t *output_size);

#ifdef __cplusplus
}
#endif
#endif
