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
 * 当前仅支持普通 Header、一个非空文件、单个 packed stream 和单个 LZMA1
 * coder；不支持压缩 Header、LZMA2、加密、滤镜、多文件、多流或分卷。
 * 目标缓冲区由调用方提供；内部会临时分配概率 workspace。
 * 定义 LZMA_C_API_DEBUG 时检查 Reader、归档数据边界、输出容量、签名和 CRC；
 * 发布构建省略这些有效性检查，调用方必须传入完整有效的归档和足够大的缓冲区。
 */
SevenZipResult sevenzip_extract_single_lzma1(
    const uint8_t *archive, size_t archive_size,
    uint8_t *output, size_t output_capacity, size_t *output_size);

#ifdef __cplusplus
}
#endif
#endif
