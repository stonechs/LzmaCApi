#ifndef LZMA_C_API_H
#define LZMA_C_API_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum LzmaCApiResult {
    LZMA_C_API_OK = 0,
    LZMA_C_API_INVALID_ARGUMENT,
    LZMA_C_API_INVALID_STREAM,
    LZMA_C_API_OUTPUT_TOO_SMALL,
    LZMA_C_API_WORKSPACE_TOO_SMALL,
    LZMA_C_API_ALLOCATION_FAILED,
    LZMA_C_API_CODE_SETUP_FAILED
} LzmaCApiResult;

/*
 * 使用说明
 * --------
 * 本接口解码原始 LZMA1 range stream，不接受 .lzma 文件头或 7z 容器头。
 * 调用者应从外层容器取得 lc、lp、pb 和解压后的精确字节数 output_size，
 * 并将压缩数据中 13 字节容器头之后的 range stream 传给本接口。range
 * stream 至少 5 字节，首字节必须为 0。解码以 output_size 为结束条件，
 * 因此不要求码流带 LZMA end marker。若 end marker 提前出现，解码可能少于
 * output_size；调试构建会报告码流无效，精简构建要求调用者保证码流有效。
 *
 * 基本调用步骤：
 *   1. 按下式计算 workspace 字节数，并分配 workspace 和 dst：
 *        (2048 + (768 << (lc + lp))) * sizeof(uint16_t)
 *      公式属于当前解码核心的内存布局，不是 LZMA 协议常量。
 *   2. 调用 lzma_c_api_decode，并检查返回值是否为 LZMA_C_API_OK。
 *   3. 调试构建成功时，dst_size 给出实际解压字节数，应与 output_size 相同；
 *      精简构建不写 dst_size，调用者应按已知的 output_size 使用输出。
 *
 * 示例（省略输入数据的读取及错误清理）：
 *   size_t ws_size = (2048u + (768u << (lc + lp))) * sizeof(uint16_t);
 *   void *workspace = malloc(ws_size);
 *   uint8_t *dst = (uint8_t *)malloc(output_size);
 *   size_t dst_size = 0;
 *   LzmaCApiResult rc = lzma_c_api_decode(
 *       raw_stream, raw_stream_size, dst, output_size, output_size,
 *       &dst_size, lc, lp, pb, workspace, ws_size);
 *   if (rc == LZMA_C_API_OK) { use_output(dst, output_size); }
 *   free(dst);
 *   free(workspace);
 *
 * 参数约束：lc <= 8、lp <= 4、lc + lp <= 12、pb <= 4。workspace 必须由
 * 调用者提供并按上述公式分配；dst_capacity 必须不小于 output_size。
 * src、workspace 必须指向有效内存；调试构建还要求 dst_size 有效。
 * 精简构建忽略 dst_size，可传 NULL。
 * 输入、输出和 workspace 缓冲区不得重叠。output_size 为 0 时 dst 可以为
 * NULL。发布构建按调用者提供的长度停止；调试构建还会检查单个匹配是否
 * 会跨越目标输出末尾。
 * 定义 LZMA_C_API_DEBUG 时会检查这些参数；精简构建省略参数检查，调用者
 * 必须自行保证所有约束，否则可能崩溃或越界读写。
 *
 * 返回值：
 *   LZMA_C_API_OK                  解码完成；仅调试构建写入 dst_size。
 *   LZMA_C_API_INVALID_ARGUMENT    （仅调试构建）参数为空、属性越界、输入少于
 *                                  5 字节，或 range initializer 首字节非 0。
 *   LZMA_C_API_INVALID_STREAM      （仅调试构建）解码长度与 output_size 不一致。
 *   LZMA_C_API_OUTPUT_TOO_SMALL    （仅调试构建）输出容量不足或非空输出时 dst 为空。
 *   LZMA_C_API_WORKSPACE_TOO_SMALL （仅调试构建）workspace 小于所需大小。
 *   LZMA_C_API_ALLOCATION_FAILED   为兼容保留；静态汇编版本不再分配代码缓冲区。
 *   LZMA_C_API_CODE_SETUP_FAILED   为兼容保留；当前静态解码实现不会返回此值。
 *
 * C 接口根据 lc/lp 在调用前把 workspace 中的概率项初始化为 1024；
 * workspace_size 用于调试版容量检查，不作为汇编初始化计数。当前实现不会修改
 * 静态汇编代码，也不需要可写可执行内存。只要每次调用使用各自不重叠的输入、
 * 输出和 workspace 缓冲区，就可以并发调用。
 *
 * 安全与平台限制：虽然接口接收 src_size，当前解码核心不会在读取压缩数据
 * 时检查输入边界，也不能可靠防护损坏码流造成的越界读写。因此只能用于可信、
 * 完整且属性/长度正确的码流，不能用于不可信或可能损坏的输入。当前实现依赖
 * 32 位 Windows。
 */
LzmaCApiResult lzma_c_api_decode(const uint8_t *src, size_t src_size,
                                 uint8_t *dst, size_t dst_capacity,
                                 size_t output_size, size_t *dst_size,
                                 unsigned lc, unsigned lp, unsigned pb,
                                 void *workspace, size_t workspace_size);

#ifdef __cplusplus
}
#endif
#endif
