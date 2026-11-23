#include "sevenzip_single.h"
#include "lzma_x86_state.h"

#include <stdlib.h>
#include <string.h>

#define SEVENZIP_START_SIZE 32u
#define SEVENZIP_MAX_DECODED 0x01000000u
#define FAST_METHOD_LZMA1 1u
#ifdef LZMA_C_API_LZMA2
#define FAST_METHOD_LZMA2 2u
#define LZMA2_MAX_PROB_COUNT (2048u + (768u << 4))
#endif

#ifdef LZMA_C_API_DEBUG
extern uint8_t *__cdecl lzma_static_x86_run(
    const uint8_t *src, uint8_t *dst, void *context, size_t output_size,
    unsigned lc, unsigned lp, unsigned pb);
#else
extern void __cdecl lzma_static_x86_run(
    const uint8_t *src, uint8_t *dst, void *context, size_t output_size,
    unsigned lc, unsigned lp, unsigned pb);
#endif

typedef struct FastFolder {
    size_t packed_offset;
    size_t packed_size;
    size_t unpacked_size;
    uint8_t method;
    uint8_t properties_size;
    uint8_t properties[5];
} FastFolder;

static void initialize_probabilities(void *workspace, size_t probability_count)
{
    uint16_t *probabilities =
        lzma_x86_probabilities((LzmaX86Context *)workspace);
    size_t i;
    for (i = 0; i < probability_count; ++i)
        probabilities[i] = 1024;
}

static uint64_t read_u64le(const uint8_t *p)
{
    uint64_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

/*
 * 7z 变长整数的精简读取器：一次读取 4 字节，根据首字节前导位数取值。
 * fast API 的约束是所有长度/偏移小于 100 MiB（至多编码 4 字节），且
 * 归档在该字段之后至少还有 4 个可读字节；因此可省去通用 UINT64 解码逻辑。
 */
static uint32_t read_number_fast(const uint8_t **cursor)
{
    uint32_t raw;
    unsigned first;
    memcpy(&raw, *cursor, sizeof(raw));
    first = raw & 0xFFu;
    if (!(first & 0x80u)) {
        ++*cursor;
        return first;
    }
    if (!(first & 0x40u)) {
        *cursor += 2;
        return ((raw >> 8) & 0xFFu) | ((first & 0x3Fu) << 8);
    }
    if (!(first & 0x20u)) {
        *cursor += 3;
        return ((raw >> 8) & 0xFFFFu) | ((first & 0x1Fu) << 16);
    }
    *cursor += 4;
    return ((raw >> 8) & 0xFFFFFFu) | ((first & 0x0Fu) << 24);
}

/* 将 LZMA1 属性字节拆成当前解码核心需要的 lc、lp、pb。 */
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

#ifdef LZMA_C_API_LZMA2
static uint16_t read_u16be_fast(const uint8_t **cursor)
{
    uint16_t value = (uint16_t)((uint16_t)(*cursor)[0] << 8 | (*cursor)[1]);
    *cursor += 2;
    return value;
}

/* LZMA2 控制层：C 管块长度、重置规则和跨块状态，汇编只解压单个压缩块。 */
static SevenZipResult decode_lzma2_stream(
    const uint8_t *src, size_t src_size, uint8_t dictionary_property,
    uint8_t *output, size_t output_size, size_t output_capacity,
    LzmaX86Context *context)
{
    const uint8_t *cursor = src;
    const uint8_t *input_end = src + src_size;
    size_t produced = 0;
    size_t dictionary_base = 0;
    unsigned lc = 0, lp = 0, pb = 0;
    unsigned need_reset = 0xE0;
    int properties_ready = 0;

#ifdef LZMA_C_API_DEBUG
    if (dictionary_property > 40 || output_size > output_capacity ||
        (output_size && !output))
        return SEVENZIP_INVALID_ARCHIVE;
#else
    (void)output_capacity;
#endif
    (void)dictionary_property; /* 字典窗口约束来自有效码流；输出本身就是历史窗口。 */
    lzma_x86_context_reset(context);

    for (;;) {
        unsigned control;
        size_t chunk_output;

#ifdef LZMA_C_API_DEBUG
        if (cursor >= input_end) return SEVENZIP_DECODE_ERROR;
#endif
        control = *cursor++;
        if (control == 0) break;

        if (control < 0x80) {
            if (control == 1) {
                dictionary_base = produced;
                need_reset = 0xC0;
            } else if (control == 2) {
#ifdef LZMA_C_API_DEBUG
                if (need_reset == 0xE0) return SEVENZIP_DECODE_ERROR;
#endif
            } else {
#ifdef LZMA_C_API_DEBUG
                return SEVENZIP_DECODE_ERROR;
#endif
            }
#ifdef LZMA_C_API_DEBUG
            if ((size_t)(input_end - cursor) < 2)
                return SEVENZIP_DECODE_ERROR;
#endif
            chunk_output = (size_t)read_u16be_fast(&cursor) + 1;
#ifdef LZMA_C_API_DEBUG
            if (chunk_output > output_size - produced ||
                (size_t)(input_end - cursor) < chunk_output)
                return SEVENZIP_DECODE_ERROR;
#endif
            memcpy(output + produced, cursor, chunk_output);
            cursor += chunk_output;
            produced += chunk_output;
            continue;
        }

#ifdef LZMA_C_API_DEBUG
        if (control < need_reset || (size_t)(input_end - cursor) < 4)
            return SEVENZIP_DECODE_ERROR;
#endif
        need_reset = 0;
        chunk_output = ((size_t)(control & 0x1Fu) << 16) +
                       (size_t)read_u16be_fast(&cursor) + 1;
        {
            size_t chunk_packed = (size_t)read_u16be_fast(&cursor) + 1;
            uint8_t property;
            size_t probability_count;
            uint8_t *chunk_end;

            if (control >= 0xC0) {
#ifdef LZMA_C_API_DEBUG
                if (cursor >= input_end) return SEVENZIP_DECODE_ERROR;
#endif
                property = *cursor++;
                if (!decode_properties(&property, &lc, &lp, &pb))
                    return SEVENZIP_UNSUPPORTED;
#ifdef LZMA_C_API_DEBUG
                if (lc + lp > 4) return SEVENZIP_UNSUPPORTED;
#endif
                properties_ready = 1;
            }
            if (control >= 0xE0)
                dictionary_base = produced;
            if (control >= 0xA0) {
#ifdef LZMA_C_API_DEBUG
                if (!properties_ready) return SEVENZIP_DECODE_ERROR;
#endif
                lzma_x86_context_reset(context);
                properties_ready = 1;
                probability_count = 2048u + (768u << (lc + lp));
                initialize_probabilities(context, probability_count);
            }
#ifdef LZMA_C_API_DEBUG
            if (!properties_ready || chunk_output > output_size - produced ||
                chunk_packed > (size_t)(input_end - cursor) || chunk_packed < 5)
                return SEVENZIP_DECODE_ERROR;
            if (cursor[0] != 0) return SEVENZIP_DECODE_ERROR;
#endif
            probability_count = 2048u + (768u << (lc + lp));
            context->previous = produced > dictionary_base ? output[produced - 1] : 0;
            context->position_bias = (uint32_t)(produced - dictionary_base) -
                                     (uint32_t)(uintptr_t)(output + produced);
#ifdef LZMA_C_API_DEBUG
            chunk_end = lzma_static_x86_run(
                cursor, output + produced, context, chunk_output, lc, lp, pb);
            if (chunk_end < output + produced ||
                (size_t)(chunk_end - (output + produced)) != chunk_output)
                return SEVENZIP_DECODE_ERROR;
#else
            (void)chunk_end;
            lzma_static_x86_run(
                cursor, output + produced, context, chunk_output, lc, lp, pb);
#endif
            cursor += chunk_packed;
            produced += chunk_output;
        }
    }

#ifdef LZMA_C_API_DEBUG
    if (produced != output_size || cursor != input_end)
        return SEVENZIP_DECODE_ERROR;
#endif
    return produced == output_size ? SEVENZIP_OK : SEVENZIP_DECODE_ERROR;
}
#endif

/* fast-only 静态库自己的 folder 解码实现，不依赖完整 7z 解析器。 */
static SevenZipResult decode_folder(const uint8_t *archive, size_t archive_size,
                                    size_t packed_limit, const FastFolder *folder,
                                    uint8_t *output, size_t output_capacity,
                                    size_t *output_size)
{
    unsigned lc, lp, pb;
    size_t probability_bytes, allocation_size;
    void *workspace;
    LzmaX86Context *context;
#ifdef LZMA_C_API_DEBUG
    uint8_t *end;
#endif
#ifdef LZMA_C_API_DEBUG
    if (folder->packed_offset > packed_limit ||
        folder->packed_size > packed_limit - folder->packed_offset ||
        folder->packed_offset > archive_size ||
        folder->packed_size > archive_size - folder->packed_offset ||
        folder->packed_size < (folder->method == FAST_METHOD_LZMA1 ? 5u : 1u) ||
        folder->unpacked_size > SEVENZIP_MAX_DECODED)
        return SEVENZIP_INVALID_ARCHIVE;
    if (folder->unpacked_size > output_capacity ||
        (folder->unpacked_size && !output))
        return SEVENZIP_OUTPUT_TOO_SMALL;
#else
    (void)archive_size;
    (void)packed_limit;
    (void)output_capacity;
#endif
#ifdef LZMA_C_API_LZMA2
    if (folder->method == FAST_METHOD_LZMA2) {
        if (folder->properties_size != 1 || folder->properties[0] > 40)
            return SEVENZIP_UNSUPPORTED;
        probability_bytes = LZMA2_MAX_PROB_COUNT * sizeof(uint16_t);
    } else
#endif
    {
        if (!decode_properties(folder->properties, &lc, &lp, &pb))
            return SEVENZIP_UNSUPPORTED;
        probability_bytes = (2048u + (768u << (lc + lp))) * sizeof(uint16_t);
    }
    allocation_size = LZMA_C_API_CONTEXT_SIZE + probability_bytes;
    workspace = malloc(allocation_size);
    if (!workspace) return SEVENZIP_MEMORY_ERROR;
    context = (LzmaX86Context *)workspace;
    lzma_x86_context_reset(context);
    if (folder->method == FAST_METHOD_LZMA1) {
        initialize_probabilities(context, probability_bytes / sizeof(uint16_t));
#ifdef LZMA_C_API_LZMA2
        context->position_bias = 0u - (uint32_t)(uintptr_t)output;
#endif
#ifdef LZMA_C_API_DEBUG
        if (archive[folder->packed_offset] != 0) {
            free(workspace);
            return SEVENZIP_DECODE_ERROR;
        }
        end = lzma_static_x86_run(
            archive + folder->packed_offset, output, context,
            folder->unpacked_size, lc, lp, pb);
        if (end < output || (size_t)(end - output) != folder->unpacked_size) {
            free(workspace);
            return SEVENZIP_DECODE_ERROR;
        }
#else
        lzma_static_x86_run(
            archive + folder->packed_offset, output, context,
            folder->unpacked_size, lc, lp, pb);
#endif
    } else {
#ifdef LZMA_C_API_LZMA2
        SevenZipResult result = decode_lzma2_stream(
            archive + folder->packed_offset, folder->packed_size,
            folder->properties[0], output, folder->unpacked_size,
            output_capacity, context);
        if (result != SEVENZIP_OK) {
            free(workspace);
            return result;
        }
#else
        free(workspace);
        return SEVENZIP_UNSUPPORTED;
#endif
    }
    free(workspace);
    *output_size = folder->unpacked_size;
    return SEVENZIP_OK;
}

/*
 * 快速路径：按固定布局提取 LZMA 解码字段，然后由本文件中的 decode_folder
 * 调用统一汇编入口。LZMA2 块控制解析仅在 LZMA_C_API_LZMA2 宏下编译。
 * 此入口不验证 7z 结构字段或 CRC。
 *
 * 参数：archive 是完整 7z 文件的只读地址，archive_size 是字节数；output
 * 是调用方提供的可写解压缓冲区，output_capacity 是其容量；output_size 指向
 * 有效的 size_t，成功时接收输出长度。发布构建不验证参数、输入边界和输出容量。
 *
 * 输入约束：可信且完整的普通 Header 单文件归档；MainStreamsInfo 紧跟 Header，
 * 仅有一个非空文件、一个 packed stream 和一个 LZMA1 coder。所有 7z 长度及偏移
 * 小于 100 MiB，且每个变长整数位置至少有 4 个可读字节。调用方保证输出容量足够。
 */
SevenZipResult sevenzip_extract_single_lzma1_fast(
    const uint8_t *archive, size_t archive_size,
    uint8_t *output, size_t output_capacity, size_t *output_size)
{
    size_t next_offset, next_size;
    const uint8_t *cursor;
    uint8_t tag, all_digests;
    FastFolder folder;

    *output_size = 0;
    next_offset = SEVENZIP_START_SIZE + (size_t)read_u64le(archive + 12);
    next_size = (size_t)read_u64le(archive + 20);
    cursor = archive + next_offset;

    /* Header、MainStreamsInfo、PackInfo、PackPos、packed stream size。 */
    cursor += 3;
    folder.packed_offset = SEVENZIP_START_SIZE + read_number_fast(&cursor);
    cursor += 2;
    folder.packed_size = read_number_fast(&cursor);

    /* Pack CRC 可选字段只跳过，不计算或比较。 */
    tag = *cursor++;
    if (tag == 10) {
        all_digests = *cursor++;
        if (all_digests) {
            cursor += 4;
        } else if (*cursor++ & 0x80u) {
            cursor += 4;
        }
        ++cursor;
    }

#ifdef LZMA_C_API_LZMA2
    /* 单 folder、单 coder 的固定前缀；之后按方法 ID 长度和属性长度读取。 */
    cursor += 5;
    {
        uint8_t coder_flags = *cursor++;
        unsigned method_size = coder_flags & 15u;
        const uint8_t *method = cursor;
        if ((coder_flags & 0xD0u) || !(coder_flags & 0x20u))
            return SEVENZIP_UNSUPPORTED;
        if (method_size == 3 && method[0] == 3 &&
            method[1] == 1 && method[2] == 1) {
            folder.method = FAST_METHOD_LZMA1;
        } else if (method_size == 1 && method[0] == 0x21) {
            folder.method = FAST_METHOD_LZMA2;
        } else {
            return SEVENZIP_UNSUPPORTED;
        }
        cursor += method_size;
        folder.properties_size = (uint8_t)read_number_fast(&cursor);
        if ((folder.method == FAST_METHOD_LZMA1 && folder.properties_size != 5) ||
            (folder.method == FAST_METHOD_LZMA2 && folder.properties_size != 1))
            return SEVENZIP_UNSUPPORTED;
        memcpy(folder.properties, cursor, folder.properties_size);
        cursor += folder.properties_size;
    }
#else
    /* 固定布局中的 UnpackInfo、folder、coder 和 LZMA1 方法 ID。 */
    cursor += 9;
    ++cursor; /* 属性长度字段固定为 5。 */
    folder.method = FAST_METHOD_LZMA1;
    folder.properties_size = 5;
    folder.properties[0] = *cursor;
    cursor += sizeof(folder.properties);
#endif
    ++cursor; /* UnpackSize tag。 */
    folder.unpacked_size = read_number_fast(&cursor);
    return decode_folder(archive, archive_size, next_offset, &folder, output,
                         output_capacity, output_size);
}
