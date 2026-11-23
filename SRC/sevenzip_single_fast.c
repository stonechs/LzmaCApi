#include "sevenzip_single.h"
#include "lzma_x86_state.h"

#include <stdlib.h>
#include <string.h>

/* 快速解析器只处理普通 7z Header，起始签名头固定为 32 字节。 */
#define SEVENZIP_START_SIZE 32u
/* Debug 构建额外限制解压结果，避免测试时分配过大的输出缓冲区。 */
#define SEVENZIP_MAX_DECODED 0x01000000u
/* 7z LZMA1/LZMA2 方法标识在本地结构中的简化编号。 */
#define FAST_METHOD_LZMA1 1u
#ifdef LZMA_C_API_LZMA2
#define FAST_METHOD_LZMA2 2u
/* LZMA2 路径只接受 lc+lp<=4，因此按最大概率表大小一次分配。 */
#define LZMA2_MAX_PROB_COUNT (2048u + (768u << 4))
#endif

/*
 * 静态汇编解码入口。Debug 版返回实际输出末地址供 C 校验；发布版不取返回值。
 * 两种构建使用相同的七参数 cdecl ABI。
 */
#ifdef LZMA_C_API_DEBUG
extern uint8_t *__cdecl lzma_static_x86_run(
    const uint8_t *src, uint8_t *dst, void *context, size_t output_size,
    unsigned lc, unsigned lp, unsigned pb);
#else
extern void __cdecl lzma_static_x86_run(
    const uint8_t *src, uint8_t *dst, void *context, size_t output_size,
    unsigned lc, unsigned lp, unsigned pb);
#endif

/*
 * 从 7z Header 中提取、交给 folder 解码器的最小字段集合。
 * packed_offset/packed_size 定位压缩输入，unpacked_size 指定输出长度；
 * properties 保存 LZMA1 的五字节属性或 LZMA2 的一字节字典属性。
 */
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
    /* workspace 前缀是 LzmaX86Context，概率项紧随该状态结构之后。 */
    uint16_t *probabilities =
        lzma_x86_probabilities((LzmaX86Context *)workspace);
    size_t i;
    /* LZMA range decoder 的每项概率都以 1024 作为初值。 */
    for (i = 0; i < probability_count; ++i)
        probabilities[i] = 1024;
}

/* 读取 7z 起始签名头中的小端 64 位偏移/长度；memcpy 避免未对齐访问。 */
static uint64_t read_u64le(const uint8_t *p)
{
    uint64_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

/*
 * 读取受限范围内的 7z UINT64 变长整数。
 * 首字节从最高位开始的连续 1 的数量表示后续附加字节数；首字节剩余低位
 * 放在结果高端，附加字节按小端顺序放入低端。本快速路径限制长度/偏移小于
 * 100 MiB，最多使用 4 字节编码，并要求字段地址后至少有 4 个可读字节，
 * 因而一次读取 32 位即可覆盖全部受支持情况。发布版不检查这些前提。
 */
static uint32_t read_number_fast(const uint8_t **cursor)
{
    uint32_t raw;
    unsigned first;
    memcpy(&raw, *cursor, sizeof(raw));
    first = raw & 0xFFu;
    /* 0xxxxxxx：首字节单独就是数值。 */
    if (!(first & 0x80u)) {
        ++*cursor;
        return first;
    }
    /* 10xxxxxx：首字节低 6 位提供高位，后一个字节提供低 8 位。 */
    if (!(first & 0x40u)) {
        *cursor += 2;
        return ((raw >> 8) & 0xFFu) | ((first & 0x3Fu) << 8);
    }
    /* 110xxxxx：首字节低 5 位加两个小端附加字节。 */
    if (!(first & 0x20u)) {
        *cursor += 3;
        return ((raw >> 8) & 0xFFFFu) | ((first & 0x1Fu) << 16);
    }
    /* 1110xxxx：首字节低 4 位加三个小端附加字节。 */
    *cursor += 4;
    return ((raw >> 8) & 0xFFFFFFu) | ((first & 0x0Fu) << 24);
}

/*
 * 将 7z 中的 LZMA 属性字节拆成 lc、lp、pb：
 * value = (pb * 5 + lp) * 9 + lc。Debug 版检查属性范围及核心支持的组合。
 */
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
/* LZMA2 块头的 16 位长度是大端序；读取后同步推进游标。 */
static uint16_t read_u16be_fast(const uint8_t **cursor)
{
    uint16_t value = (uint16_t)((uint16_t)(*cursor)[0] << 8 | (*cursor)[1]);
    *cursor += 2;
    return value;
}

/*
 * 逐块处理 LZMA2 数据流。C 层解析控制字节、各块长度和重置规则；压缩块的
 * LZMA range stream 仍交给共享的 x86 汇编核心。未压缩块由 C 直接复制。
 *
 * dictionary_property：7z 的字典属性；本实现的历史窗口就是当前输出缓冲区，
 * 因而不另分配字典。output_size 是容器声明的总输出长度，context 同时保存
 * 汇编状态和概率表。Debug 版执行块边界/总长度检查，发布版依赖输入符合格式。
 */
static SevenZipResult decode_lzma2_stream(
    const uint8_t *src, size_t src_size, uint8_t dictionary_property,
    uint8_t *output, size_t output_size, size_t output_capacity,
    LzmaX86Context *context)
{
    const uint8_t *cursor = src;
    const uint8_t *input_end = src + src_size;
    size_t produced = 0;
    /* 最近一次字典重置后，输出缓冲区中有效历史数据的起点。 */
    size_t dictionary_base = 0;
    unsigned lc = 0, lp = 0, pb = 0;
    /* 下一压缩块允许的最低控制字节；初始时必须重置字典/属性/状态。 */
    unsigned need_reset = 0xE0;
    /* lc/lp/pb 只有在带属性重置的压缩块中出现，之后可以沿用。 */
    int properties_ready = 0;

#ifdef LZMA_C_API_DEBUG
    if (dictionary_property > 40 || output_size > output_capacity ||
        (output_size && !output))
        return SEVENZIP_INVALID_ARCHIVE;
#else
    (void)output_capacity;
#endif
    (void)dictionary_property; /* 字典窗口约束来自有效码流；输出本身就是历史窗口。 */
    /* 每个新的 LZMA2 流都从初始 LZMA 状态开始，后续块按控制字节选择是否重置。 */
    lzma_x86_context_reset(context);

    for (;;) {
        unsigned control;
        size_t chunk_output;

#ifdef LZMA_C_API_DEBUG
        if (cursor >= input_end) return SEVENZIP_DECODE_ERROR;
#endif
        control = *cursor++;
        /* 0 是流结束标记。 */
        if (control == 0) break;

        if (control < 0x80) {
            /* 1 重置字典；2 延续字典。其后的两字节给出未压缩块长度减一。 */
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
        /* 压缩块控制值还必须满足前一块建立的状态/属性/字典重置要求。 */
        if (control < need_reset || (size_t)(input_end - cursor) < 4)
            return SEVENZIP_DECODE_ERROR;
#endif
        need_reset = 0;
        /* 高 5 位来自 control，低 16 位来自接下来的两个大端字节；存储值均减一。 */
        chunk_output = ((size_t)(control & 0x1Fu) << 16) +
                       (size_t)read_u16be_fast(&cursor) + 1;
        {
            size_t chunk_packed = (size_t)read_u16be_fast(&cursor) + 1;
            uint8_t property;
            size_t probability_count;
            uint8_t *chunk_end;

            if (control >= 0xC0) {
                /* C0 及以上携带新的 LZMA 属性字节。 */
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
            /* E0 及以上表示新字典起点，历史距离不能越过此处。 */
            if (control >= 0xE0)
                dictionary_base = produced;
            if (control >= 0xA0) {
#ifdef LZMA_C_API_DEBUG
                if (!properties_ready) return SEVENZIP_DECODE_ERROR;
#endif
                /* A0 及以上重置 LZMA 状态；概率模型也须恢复到初始值。 */
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
            context->previous = produced > dictionary_base ? output[produced - 1] : 0;
            context->position_bias = (uint32_t)(produced - dictionary_base) -
                                     (uint32_t)(uintptr_t)(output + produced);
#ifdef LZMA_C_API_DEBUG
            /* 汇编核心采用绝对输出地址；偏置把它转换为当前字典的逻辑位置。 */
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
    /* 成功结束必须同时消费完整 packed stream，并产出容器声明的长度。 */
    if (produced != output_size || cursor != input_end)
        return SEVENZIP_DECODE_ERROR;
#endif
    return produced == output_size ? SEVENZIP_OK : SEVENZIP_DECODE_ERROR;
}
#endif

/*
 * 为已提取的单个 folder 准备 workspace，并选择 LZMA1 或 LZMA2 解码路径。
 * 这里直接调用汇编核心，不经过 raw API wrapper；workspace 按 context 前缀加
 * 概率表分配。LZMA2 宏关闭时，仅接受 LZMA1。
 */
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
    /* 仅调试构建验证 packed 区间、输入长度和输出容量；发布构建依赖调用方保证。 */
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
        /* LZMA2 属性只含字典大小；概率表按支持范围的最大 lc+lp 预留。 */
        if (folder->properties_size != 1 || folder->properties[0] > 40)
            return SEVENZIP_UNSUPPORTED;
        probability_bytes = LZMA2_MAX_PROB_COUNT * sizeof(uint16_t);
    } else
#endif
    {
        /* LZMA1 属性决定准确的概率表项数，避免为普通路径分配最大表。 */
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
        /* LZMA1 单块从初始概率模型开始；模型由 C 初始化，汇编专注于解码。 */
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
        /* 发布版不取实际末地址，成功后按容器声明的长度报告输出大小。 */
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
    /* 两种解码路径共用此处释放；成功输出长度来自 folder 的声明值。 */
    free(workspace);
    *output_size = folder->unpacked_size;
    return SEVENZIP_OK;
}

/*
 * 快速路径：按固定布局提取 LZMA 解码字段，然后由本文件中的 decode_folder
 * 调用统一汇编入口。LZMA2 块控制解析仅在 LZMA_C_API_LZMA2 宏下编译。
 * 此入口不验证 7z 结构字段或 CRC。
 *
 * 解析过程依赖普通 Header 位于起始头记录的 next-header 位置，以及单文件、单
 * folder、单 coder、单 packed stream 等已知布局，因此通过游标固定偏移跳过无需
 * 读取的标签，只提取压缩数据偏移/长度、编码方法、属性和解压长度。默认构建只接受
 * LZMA1；启用 LZMA_C_API_LZMA2 后也接受单 LZMA2 coder。该策略不适用于通用 7z 归档。
 * 不支持以 EncodedHeader（0x17）开头的压缩 Header；文件数据使用 LZMA2 与压缩
 * Header 是两件独立的事，启用 LZMA_C_API_LZMA2 不会增加 Header 解压功能。
 *
 * 参数：archive 是完整 7z 文件的只读地址，archive_size 是字节数；output
 * 是调用方提供的可写解压缓冲区，output_capacity 是其容量；output_size 指向
 * 有效的 size_t，成功时接收输出长度。发布构建不验证参数、输入边界和输出容量。
 *
 * 输入约束：可信且完整、使用普通 Header 的单文件归档；MainStreamsInfo 紧跟 Header，
 * 仅有一个非空文件、一个 packed stream 和一个 coder。默认构建的 coder 是 LZMA1；
 * LZMA_C_API_LZMA2 构建还接受单 LZMA2 coder。所有 7z 长度及偏移小于 100 MiB，
 * 每个变长整数位置至少有 4 个可读字节；调用方保证输出容量足够。
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
    /* 7z 起始签名头中的 next-header 相对偏移和长度都是小端 64 位整数。 */
    next_offset = SEVENZIP_START_SIZE + (size_t)read_u64le(archive + 12);
    next_size = (size_t)read_u64le(archive + 20);
    cursor = archive + next_offset;

    /* 按固定结构跨过 Header/StreamsInfo 标签，读取 PackPos 和唯一 packed stream 的大小。 */
    cursor += 3;
    folder.packed_offset = SEVENZIP_START_SIZE + read_number_fast(&cursor);
    cursor += 2;
    folder.packed_size = read_number_fast(&cursor);

    /* Pack CRC 字段可选；无论单项还是全定义掩码，本快速路径都只跳过、不校验。 */
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
    /* 单 folder、单 coder 的固定前缀；再按 coder 标志读取方法 ID 和属性。 */
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
    /* 默认版按已知 LZMA1 布局跳过 UnpackInfo/folder/coder 与方法 ID。 */
    cursor += 9;
    ++cursor; /* 属性长度字段固定为 5。 */
    folder.method = FAST_METHOD_LZMA1;
    folder.properties_size = 5;
    folder.properties[0] = *cursor;
    cursor += sizeof(folder.properties);
#endif
    ++cursor; /* UnpackSize 属性标签；后面的变长整数是唯一输出流长度。 */
    folder.unpacked_size = read_number_fast(&cursor);
    return decode_folder(archive, archive_size, next_offset, &folder, output,
                         output_capacity, output_size);
}
