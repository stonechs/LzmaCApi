#include "sevenzip_single.h"
#include "lzma_c_api.h"

#include <stdlib.h>
#include <string.h>

#define SEVENZIP_SIGNATURE_SIZE 6u
#define SEVENZIP_START_SIZE 32u
#define SEVENZIP_MAX_DECODED 0x01000000u

typedef struct Reader {
    const uint8_t *data;
    size_t size;
    size_t pos;
    int failed;
} Reader;

typedef struct Folder {
    size_t packed_offset;
    size_t packed_size;
    size_t unpacked_size;
    uint8_t properties[5];
    int packed_crc_defined;
    uint32_t packed_crc;
    int folder_crc_defined;
    uint32_t folder_crc;
    int stream_crc_defined;
    uint32_t stream_crc;
} Folder;

static uint32_t read_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_u64le(const uint8_t *p)
{
    return (uint64_t)read_u32le(p) | ((uint64_t)read_u32le(p + 4) << 32);
}

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

static int reader_take(Reader *r, size_t n, const uint8_t **out)
{
    if (r->failed || n > r->size - r->pos) {
        r->failed = 1;
        return 0;
    }
    if (out) *out = r->data + r->pos;
    r->pos += n;
    return 1;
}

static uint8_t reader_byte(Reader *r)
{
    const uint8_t *p;
    if (!reader_take(r, 1, &p)) return 0;
    return p[0];
}

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

static int reader_u32(Reader *r, uint32_t *value)
{
    const uint8_t *p;
    if (!reader_take(r, 4, &p)) return 0;
    *value = read_u32le(p);
    return 1;
}

static int reader_size(Reader *r, size_t *value)
{
    uint64_t n = reader_number(r);
    if (r->failed || n > (uint64_t)SIZE_MAX) {
        r->failed = 1;
        return 0;
    }
    *value = (size_t)n;
    return 1;
}

static int read_digest_vector(Reader *r, size_t count,
                              int *defined, uint32_t *crc)
{
    uint8_t all = reader_byte(r);
    size_t i;
    if (r->failed || all > 1) return 0;
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
    return !r->failed;
}

static int parse_streams(Reader *r, Folder *f)
{
    size_t pack_pos, folder_count, unpack_size, props_size;
    size_t substream_count = 1;
    uint8_t tag, coder_flags;
    const uint8_t *p;

    if (reader_byte(r) != 6 || !reader_size(r, &pack_pos) ||
        reader_byte(r) != 1 || reader_byte(r) != 9 ||
        !reader_size(r, &f->packed_size)) return 0;

    tag = reader_byte(r);
    if (tag == 10) {
        if (!read_digest_vector(r, 1, &f->packed_crc_defined, &f->packed_crc)) return 0;
        tag = reader_byte(r);
    }
    if (tag != 0 || reader_byte(r) != 7 || reader_byte(r) != 11 ||
        !reader_size(r, &folder_count) || folder_count != 1 || reader_byte(r) != 0 ||
        reader_byte(r) != 1) return 0;

    coder_flags = reader_byte(r);
    if (r->failed || (coder_flags & 0xD0) || (coder_flags & 0x0F) != 3 ||
        !(coder_flags & 0x20)) return 0;
    if (!reader_take(r, 3, &p) || p[0] != 3 || p[1] != 1 || p[2] != 1 ||
        !reader_size(r, &props_size) || props_size != sizeof(f->properties) ||
        !reader_take(r, props_size, &p)) return 0;
    memcpy(f->properties, p, sizeof(f->properties));

    if (reader_byte(r) != 12 || !reader_size(r, &unpack_size)) return 0;
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
            if (!reader_size(r, &substream_count) || substream_count != 1) return 0;
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
    if (tag != 0 || pack_pos > SIZE_MAX - SEVENZIP_START_SIZE) return 0;
    f->packed_offset = SEVENZIP_START_SIZE + pack_pos;
    return !r->failed;
}

static int decode_properties(const uint8_t *props, unsigned *lc,
                             unsigned *lp, unsigned *pb)
{
    unsigned value = props[0];
    if (value >= 225) return 0;
    *lc = value % 9;
    *lp = (value / 9) % 5;
    *pb = value / 45;
    return *lc <= 8 && *lp <= 4 && *lc + *lp <= 12 && *pb <= 4;
}

static SevenZipResult decode_folder(const uint8_t *archive, size_t archive_size,
                                    size_t packed_limit, const Folder *f, uint8_t *output,
                                    size_t output_capacity, size_t *output_size)
{
    unsigned lc, lp, pb;
    size_t workspace_size;
    void *workspace;
    LzmaCApiResult rc;
    size_t decoded_size = 0;
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
    if (!decode_properties(f->properties, &lc, &lp, &pb)) return SEVENZIP_UNSUPPORTED;
    workspace_size = (2048u + (768u << (lc + lp))) * sizeof(uint16_t);
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
    if (f->stream_crc_defined &&
        crc32_bytes(output, f->unpacked_size) != f->stream_crc)
        return SEVENZIP_CRC_ERROR;
    *output_size = f->unpacked_size;
    return SEVENZIP_OK;
}

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
            if (!reader_size(&r, &n) || !reader_take(&r, n, NULL))
                return SEVENZIP_INVALID_ARCHIVE;
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
        if (!reader_size(&r, &file_count) || file_count != 1)
            return SEVENZIP_UNSUPPORTED;
        while ((tag = reader_byte(&r)) != 0) {
            const uint8_t *p;
            size_t n;
            if (!reader_size(&r, &n) || !reader_take(&r, n, &p))
                return SEVENZIP_INVALID_ARCHIVE;
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
    if (reader_byte(&r) != 0 || r.failed || r.pos != r.size ||
        empty_stream || anti_item || !streams_seen)
        return SEVENZIP_UNSUPPORTED;
    return SEVENZIP_OK;
}

SevenZipResult sevenzip_extract_single_lzma1(
    const uint8_t *archive, size_t archive_size,
    uint8_t *output, size_t output_capacity, size_t *output_size)
{
    static const uint8_t signature[SEVENZIP_SIGNATURE_SIZE] =
        { 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C };
    size_t next_offset, next_size;
    uint64_t offset64, size64;
    const uint8_t *next_header;
    Folder folder;
    SevenZipResult result;
    uint8_t *decoded_header = NULL;
    size_t decoded_header_size = 0;

    if (!archive || !output_size) return SEVENZIP_INVALID_ARGUMENT;
    *output_size = 0;
    if (archive_size < SEVENZIP_START_SIZE ||
        memcmp(archive, signature, sizeof(signature)) != 0 || archive[6] != 0)
        return SEVENZIP_INVALID_ARCHIVE;
    if (crc32_bytes(archive + 12, 20) != read_u32le(archive + 8))
        return SEVENZIP_CRC_ERROR;
    offset64 = read_u64le(archive + 12);
    size64 = read_u64le(archive + 20);
    if (offset64 > SIZE_MAX - SEVENZIP_START_SIZE || size64 > SIZE_MAX)
        return SEVENZIP_INVALID_ARCHIVE;
    next_offset = SEVENZIP_START_SIZE + (size_t)offset64;
    next_size = (size_t)size64;
    if (next_offset > archive_size || next_size > archive_size - next_offset)
        return SEVENZIP_INVALID_ARCHIVE;
    next_header = archive + next_offset;
    if (crc32_bytes(next_header, next_size) != read_u32le(archive + 28))
        return SEVENZIP_CRC_ERROR;

    memset(&folder, 0, sizeof(folder));
    if (next_size && next_header[0] == 23) {
        Reader r = { next_header, next_size, 1, 0 };
        Folder encoded;
        memset(&encoded, 0, sizeof(encoded));
        if (!parse_streams(&r, &encoded) || r.pos != r.size)
            return SEVENZIP_UNSUPPORTED;
        if (encoded.unpacked_size > SEVENZIP_MAX_DECODED)
            return SEVENZIP_INVALID_ARCHIVE;
        decoded_header = (uint8_t *)malloc(encoded.unpacked_size ? encoded.unpacked_size : 1);
        if (!decoded_header) return SEVENZIP_MEMORY_ERROR;
        result = decode_folder(archive, archive_size, next_offset, &encoded, decoded_header,
                               encoded.unpacked_size, &decoded_header_size);
        if (result != SEVENZIP_OK) {
            free(decoded_header);
            return result;
        }
        result = parse_header(decoded_header, decoded_header_size, &folder);
        free(decoded_header);
        if (result != SEVENZIP_OK) return result;
    } else {
        result = parse_header(next_header, next_size, &folder);
        if (result != SEVENZIP_OK) return result;
    }
    return decode_folder(archive, archive_size, next_offset, &folder, output,
                         output_capacity, output_size);
}
