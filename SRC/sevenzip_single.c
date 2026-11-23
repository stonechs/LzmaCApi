#include "sevenzip_single.h"
#include "lzma_c_api.h"

#include <stdlib.h>
#include <string.h>

#define SEVENZIP_SIGNATURE_SIZE 6u
#define SEVENZIP_START_SIZE 32u
#define SEVENZIP_MAX_DECODED 0x01000000u

/*
 * 单文件 7z 容器适配层。
 * 本模块只处理普通 Header、单文件、单 packed stream 和 LZMA1 coder；实际
 * range 解码交给本项目的 lzma_c_api_decode。调试构建逐字段检查 Reader 和
 * 数据边界；发布构建假设输入完整且符合上述限制，省略边界检查以缩小代码。
 */
typedef struct Reader {
    const uint8_t *data;
    size_t size;
    size_t pos;
    int failed;
} Reader;

typedef struct Folder {
    size_t packed_offset;       /* 压缩数据在整个 7z 文件中的绝对偏移 */
    size_t packed_size;         /* 压缩数据长度 */
    size_t unpacked_size;       /* folder 解压后的长度 */
    uint8_t properties[5];      /* LZMA1 的属性字节及字典大小 */
    int packed_crc_defined;     /* PackInfo 是否给出压缩数据 CRC */
    uint32_t packed_crc;
    int folder_crc_defined;     /* UnpackInfo 是否给出 folder 解压 CRC */
    uint32_t folder_crc;
    int stream_crc_defined;     /* 单文件解压数据是否有可用 CRC */
    uint32_t stream_crc;
} Folder;

/* 读取 7z 使用的小端固定宽度整数。 */
static uint32_t read_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_u64le(const uint8_t *p)
{
    return (uint64_t)read_u32le(p) | ((uint64_t)read_u32le(p + 4) << 32);
}

/*
 * 标准 CRC-32（IEEE 802.3）。仅调试构建验证容器校验值；发布构建不编入
 * 该计算函数，避免占用代码空间和运行时间。
 */
#ifdef LZMA_C_API_DEBUG
static uint32_t crc32_bytes(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    for (i = 0; i < size; ++i) {
        unsigned bit;
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
#endif

/* 取出 n 字节；仅调试版检查范围并设置 failed，发布版直接推进游标。 */
static int reader_take(Reader *r, size_t n, const uint8_t **out)
{
#ifdef LZMA_C_API_DEBUG
    if (r->failed || r->pos > r->size || n > r->size - r->pos) {
        r->failed = 1;
        return 0;
    }
#endif
    if (out) *out = r->data + r->pos;
    r->pos += n;
    return 1;
}

/*
 * 读取失败只由调试构建处理；发布构建假设结构有效，只推进游标，不生成
 * 失败分支。reader_size 同理：发布版本不做长度转换/读取失败检查。
 */
#ifdef LZMA_C_API_DEBUG
#define READER_TAKE_OR_RETURN(r, n, out, result) \
    do { if (!reader_take((r), (n), (out))) return (result); } while (0)
#define READER_SIZE_OK(r, value) reader_size((r), (value))
#else
#define READER_TAKE_OR_RETURN(r, n, out, result) \
    ((void)reader_take((r), (n), (out)))
#define READER_SIZE_OK(r, value) (reader_size((r), (value)), 1)
#endif

/* 读取一个字节；调试版越界时返回 0，并通过 failed 区分真实的 0 字节。 */
static uint8_t reader_byte(Reader *r)
{
    const uint8_t *p;
    READER_TAKE_OR_RETURN(r, 1, &p, 0);
    return p[0];
}

/*
 * 读取 7z 的 UINT64 可变长整数。首字节前导 1 的数量决定后续附加字节数；
 * 首字节剩余低位与附加字节按小端位序拼成最终数值。
 */
static uint64_t reader_number(Reader *r)
{
    uint8_t first = reader_byte(r), mask = 0x80;
    uint64_t value = 0;
    unsigned i;
    for (i = 0; i < 8; ++i, mask >>= 1) {
        if (!(first & mask)) {
            value |= (uint64_t)(first & (uint8_t)(mask - 1)) << (8 * i);
            return value;
        }
        value |= (uint64_t)reader_byte(r) << (8 * i);
    }
    return value;
}

/* 读取固定 4 字节小端整数；reader_take 的边界校验仅在调试版执行。 */
static int reader_u32(Reader *r, uint32_t *value)
{
    const uint8_t *p;
    READER_TAKE_OR_RETURN(r, 4, &p, 0);
    *value = read_u32le(p);
    return 1;
}

/* 读取变长整数；调试版检查它能否由 size_t 表示，发布版按有效归档转换。 */
static int reader_size(Reader *r, size_t *value)
{
    uint64_t n = reader_number(r);
#ifdef LZMA_C_API_DEBUG
    if (r->failed || n > (uint64_t)SIZE_MAX) {
        r->failed = 1;
        return 0;
    }
#endif
    *value = (size_t)n;
    return 1;
}

/*
 * 解析单个对象的 CRC 表。这里即使是发布构建也要消费字段，以便继续解析
 * 后续结构；CRC 是否实际计算和比较由 LZMA_C_API_DEBUG 控制。
 */
static int read_digest_vector(Reader *r, size_t count,
                              int *defined, uint32_t *crc)
{
    uint8_t all = reader_byte(r);
#ifdef LZMA_C_API_DEBUG
    if (r->failed || all > 1) return 0;
#else
    if (all > 1) return 0;
#endif
    *defined = 0;
    if (count != 1) return 0;
    if (all) {
        *defined = 1;
        return reader_u32(r, crc);
    }
    if (reader_byte(r) & 0x80) {
        *defined = 1;
        return reader_u32(r, crc);
    }
#ifdef LZMA_C_API_DEBUG
    return !r->failed;
#else
    return 1;
#endif
}

/*
 * 解析一个 StreamsInfo：仅接受一个 packed stream、一个 folder、一个 coder，
 * 且 coder 必须是带 5 字节属性的 LZMA1。函数同时记录文件偏移、长度和 CRC。
 */
static int parse_streams(Reader *r, Folder *f)
{
    size_t pack_pos, folder_count, unpack_size, props_size;
    size_t substream_count = 1;
    uint8_t tag, coder_flags;
    const uint8_t *p;

    if (reader_byte(r) != 6 || !READER_SIZE_OK(r, &pack_pos) ||
        reader_byte(r) != 1 || reader_byte(r) != 9 ||
        !READER_SIZE_OK(r, &f->packed_size)) return 0;

    tag = reader_byte(r);
    if (tag == 10) {
        if (!read_digest_vector(r, 1, &f->packed_crc_defined, &f->packed_crc)) return 0;
        tag = reader_byte(r);
    }
    if (tag != 0 || reader_byte(r) != 7 || reader_byte(r) != 11 ||
        !READER_SIZE_OK(r, &folder_count) || folder_count != 1 || reader_byte(r) != 0 ||
        reader_byte(r) != 1) return 0;

    coder_flags = reader_byte(r);
    if (
#ifdef LZMA_C_API_DEBUG
        r->failed ||
#endif
        (coder_flags & 0xD0) || (coder_flags & 0x0F) != 3 ||
        !(coder_flags & 0x20)) return 0;
    READER_TAKE_OR_RETURN(r, 3, &p, 0);
    if (p[0] != 3 || p[1] != 1 || p[2] != 1 ||
        !READER_SIZE_OK(r, &props_size) || props_size != sizeof(f->properties)) return 0;
    READER_TAKE_OR_RETURN(r, props_size, &p, 0);
    memcpy(f->properties, p, sizeof(f->properties));

    if (reader_byte(r) != 12 || !READER_SIZE_OK(r, &unpack_size)) return 0;
    f->unpacked_size = unpack_size;
    tag = reader_byte(r);
    if (tag == 10) {
        if (!read_digest_vector(r, 1, &f->folder_crc_defined, &f->folder_crc)) return 0;
        tag = reader_byte(r);
    }
    if (tag != 0) return 0;

    tag = reader_byte(r);
    if (tag == 8) {
        tag = reader_byte(r);
        if (tag == 13) {
            if (!READER_SIZE_OK(r, &substream_count) || substream_count != 1) return 0;
            tag = reader_byte(r);
        }
        if (tag == 9) {
            /* With one substream, the size list has zero entries. */
            if (substream_count != 1) return 0;
            tag = reader_byte(r);
        }
        if (tag == 10) {
            if (!read_digest_vector(r, substream_count,
                                    &f->stream_crc_defined, &f->stream_crc)) return 0;
            tag = reader_byte(r);
        }
        if (!f->stream_crc_defined && f->folder_crc_defined && substream_count == 1) {
            f->stream_crc_defined = 1;
            f->stream_crc = f->folder_crc;
        }
        if (tag != 0) return 0;
        tag = reader_byte(r);
    } else if (f->folder_crc_defined) {
        f->stream_crc_defined = 1;
        f->stream_crc = f->folder_crc;
    }
    if (tag != 0) return 0;
#ifdef LZMA_C_API_DEBUG
    if (pack_pos > SIZE_MAX - SEVENZIP_START_SIZE) return 0;
#endif
    f->packed_offset = SEVENZIP_START_SIZE + pack_pos;
#ifdef LZMA_C_API_DEBUG
    return !r->failed;
#else
    return 1;
#endif
}

/* 将 LZMA1 属性字节拆成核心需要的 lc、lp、pb；仅调试版拒绝非法组合。 */
static int decode_properties(const uint8_t *props, unsigned *lc,
                             unsigned *lp, unsigned *pb)
{
    unsigned value = props[0];
#ifdef LZMA_C_API_DEBUG
    if (value >= 225) return 0;
#endif
    *lc = value % 9;
    *lp = (value / 9) % 5;
    *pb = value / 45;
#ifdef LZMA_C_API_DEBUG
    return *lc <= 8 && *lp <= 4 && *lc + *lp <= 12 && *pb <= 4;
#else
    return 1;
#endif
}

/*
 * 解压一个已解析的 folder。workspace 包含统一状态前缀和概率表；
 * 调试版先限制压缩区间与输出容量，再调用已有 C API。发布版由调用方保证边界。
 */
static SevenZipResult decode_folder(const uint8_t *archive, size_t archive_size,
                                    size_t packed_limit, const Folder *f, uint8_t *output,
                                    size_t output_capacity, size_t *output_size)
{
    unsigned lc, lp, pb;
    size_t workspace_size;
    void *workspace;
    LzmaCApiResult rc;
    size_t decoded_size = 0;
#ifdef LZMA_C_API_DEBUG
    if (f->packed_offset > packed_limit ||
        f->packed_size > packed_limit - f->packed_offset ||
        f->packed_offset > archive_size ||
        f->packed_size > archive_size - f->packed_offset ||
        f->packed_size < 5 || f->unpacked_size > SEVENZIP_MAX_DECODED)
        return SEVENZIP_INVALID_ARCHIVE;
    if (f->packed_crc_defined &&
        crc32_bytes(archive + f->packed_offset, f->packed_size) != f->packed_crc)
        return SEVENZIP_CRC_ERROR;
    if (f->unpacked_size > output_capacity || (f->unpacked_size && !output))
        return SEVENZIP_OUTPUT_TOO_SMALL;
#else
    (void)archive_size;
    (void)packed_limit;
    (void)output_capacity;
#endif
    if (!decode_properties(f->properties, &lc, &lp, &pb)) return SEVENZIP_UNSUPPORTED;
    workspace_size = LZMA_C_API_CONTEXT_SIZE +
                     (2048u + (768u << (lc + lp))) * sizeof(uint16_t);
    workspace = malloc(workspace_size);
    if (!workspace) return SEVENZIP_MEMORY_ERROR;
    rc = lzma_c_api_decode(archive + f->packed_offset, f->packed_size,
                           output, output_capacity, f->unpacked_size, &decoded_size,
                           lc, lp, pb, workspace, workspace_size);
    free(workspace);
    if (rc != LZMA_C_API_OK) return SEVENZIP_DECODE_ERROR;
#ifdef LZMA_C_API_DEBUG
    if (decoded_size != f->unpacked_size) return SEVENZIP_DECODE_ERROR;
#else
    (void)decoded_size;
#endif
#ifdef LZMA_C_API_DEBUG
    if (f->stream_crc_defined &&
        crc32_bytes(output, f->unpacked_size) != f->stream_crc)
        return SEVENZIP_CRC_ERROR;
#endif
    *output_size = f->unpacked_size;
    return SEVENZIP_OK;
}

/*
 * 解析普通 Header，仅接受一个有数据流的文件。文件名和时间等非必要元数据
 * 按长度跳过；文件大小（若给出）和 CRC 字段按单文件布局读取。
 */
static SevenZipResult parse_header(const uint8_t *data, size_t size,
                                   Folder *data_folder)
{
    Reader r = { data, size, 0, 0 };
    uint8_t tag = reader_byte(&r);
    int streams_seen = 0, empty_stream = 0, anti_item = 0;
    if (tag != 1) return SEVENZIP_INVALID_ARCHIVE;
    tag = reader_byte(&r);
    if (tag == 2) {
        while ((tag = reader_byte(&r)) != 0) {
            size_t n;
            if (!READER_SIZE_OK(&r, &n)) return SEVENZIP_INVALID_ARCHIVE;
            READER_TAKE_OR_RETURN(&r, n, NULL, SEVENZIP_INVALID_ARCHIVE);
        }
        tag = reader_byte(&r);
    }
    if (tag == 4) {
        if (!parse_streams(&r, data_folder)) return SEVENZIP_UNSUPPORTED;
        streams_seen = 1;
        tag = reader_byte(&r);
    }
    if (tag != 5) return SEVENZIP_UNSUPPORTED;
    {
        size_t file_count, prop_size;
        if (!READER_SIZE_OK(&r, &file_count) || file_count != 1)
            return SEVENZIP_UNSUPPORTED;
        while ((tag = reader_byte(&r)) != 0) {
            const uint8_t *p;
            size_t n;
            if (!READER_SIZE_OK(&r, &n)) return SEVENZIP_INVALID_ARCHIVE;
            READER_TAKE_OR_RETURN(&r, n, &p, SEVENZIP_INVALID_ARCHIVE);
            prop_size = n;
            if (tag == 14) {
                if (prop_size != 1) return SEVENZIP_INVALID_ARCHIVE;
                empty_stream = (p[0] & 0x80) != 0;
            } else if (tag == 9) {
                if (prop_size != 8 || read_u64le(p) != data_folder->unpacked_size)
                    return SEVENZIP_INVALID_ARCHIVE;
            } else if (tag == 10) {
                if (prop_size < 1) return SEVENZIP_INVALID_ARCHIVE;
                if (p[0] > 1) return SEVENZIP_INVALID_ARCHIVE;
                if (p[0]) {
                    if (prop_size != 5) return SEVENZIP_INVALID_ARCHIVE;
                    data_folder->stream_crc_defined = 1;
                    data_folder->stream_crc = read_u32le(p + 1);
                } else {
                    if (prop_size != 2 && prop_size != 6)
                        return SEVENZIP_INVALID_ARCHIVE;
                    if (p[1] & 0x80) {
                        if (prop_size != 6) return SEVENZIP_INVALID_ARCHIVE;
                        data_folder->stream_crc_defined = 1;
                        data_folder->stream_crc = read_u32le(p + 2);
                    } else if (prop_size != 2) {
                        return SEVENZIP_INVALID_ARCHIVE;
                    }
                }
            } else if (tag == 16) {
                if (prop_size != 1) return SEVENZIP_INVALID_ARCHIVE;
                anti_item = (p[0] & 0x80) != 0;
            }
        }
    }
    if (reader_byte(&r) != 0 || empty_stream || anti_item || !streams_seen)
        return SEVENZIP_UNSUPPORTED;
#ifdef LZMA_C_API_DEBUG
    if (r.failed || r.pos != r.size) return SEVENZIP_UNSUPPORTED;
#endif
    return SEVENZIP_OK;
}

/*
 * 对外入口：定位普通 Header，解析唯一文件的 LZMA1 folder 并写入调用方缓冲区。
 * 调试宏控制签名、CRC、归档长度和输出容量的检查；发布版要求调用方保证有效。
 */
SevenZipResult sevenzip_extract_single_lzma1(
    const uint8_t *archive, size_t archive_size,
    uint8_t *output, size_t output_capacity, size_t *output_size)
{
#ifdef LZMA_C_API_DEBUG
    static const uint8_t signature[SEVENZIP_SIGNATURE_SIZE] =
        { 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C };
#endif
    size_t next_offset, next_size;
    uint64_t offset64, size64;
    const uint8_t *next_header;
    Folder folder;

#ifdef LZMA_C_API_DEBUG
    if (!archive || !output_size) return SEVENZIP_INVALID_ARGUMENT;
#else
    (void)archive_size;
    (void)output_capacity;
#endif
    *output_size = 0;
#ifdef LZMA_C_API_DEBUG
    if (archive_size < SEVENZIP_START_SIZE || archive[6] != 0)
        return SEVENZIP_INVALID_ARCHIVE;
#else
    if (archive[6] != 0) return SEVENZIP_INVALID_ARCHIVE;
#endif
#ifdef LZMA_C_API_DEBUG
    if (memcmp(archive, signature, sizeof(signature)) != 0)
        return SEVENZIP_INVALID_ARCHIVE;
    if (crc32_bytes(archive + 12, 20) != read_u32le(archive + 8))
        return SEVENZIP_CRC_ERROR;
#endif
    offset64 = read_u64le(archive + 12);
    size64 = read_u64le(archive + 20);
#ifdef LZMA_C_API_DEBUG
    if (offset64 > SIZE_MAX - SEVENZIP_START_SIZE || size64 > SIZE_MAX)
        return SEVENZIP_INVALID_ARCHIVE;
#endif
    next_offset = SEVENZIP_START_SIZE + (size_t)offset64;
    next_size = (size_t)size64;
#ifdef LZMA_C_API_DEBUG
    if (next_offset > archive_size || next_size > archive_size - next_offset)
        return SEVENZIP_INVALID_ARCHIVE;
#endif
    next_header = archive + next_offset;
#ifdef LZMA_C_API_DEBUG
    if (crc32_bytes(next_header, next_size) != read_u32le(archive + 28))
        return SEVENZIP_CRC_ERROR;
#endif

    memset(&folder, 0, sizeof(folder));
    {
        SevenZipResult result = parse_header(next_header, next_size, &folder);
        if (result != SEVENZIP_OK) return result;
    }
    return decode_folder(archive, archive_size, next_offset, &folder, output,
                         output_capacity, output_size);
}
