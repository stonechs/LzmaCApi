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
    LZMA_C_API_ALLOCATION_FAILED
} LzmaCApiResult;

/*
 * 使用说明
 * --------
 * 本接口解码原始 LZMA1 range stream，不接受 .lzma 文件头或 7z 容器头。
 * 调用者应从外层容器取得 lc、lp、pb 和解压后的精确字节数 output_size，
 * 并将压缩数据中 13 字节容器头之后的 range stream 传给本接口。range
 * stream 至少 5 字节，首字节必须为 0；当前静态解码核心要求码流包含
 * LZMA end marker。
 *
 * 基本调用步骤：
 *   1. 用 lzma_c_api_workspace_size(lc, lp) 查询概率表所需字节数；返回 0
 *      表示 lc/lp 超出支持范围。
 *   2. 分配至少该大小的 workspace，以及至少 output_size 字节的 dst。
 *   3. 调用 lzma_c_api_decode，并检查返回值是否为 LZMA_C_API_OK。
 *   4. 成功时，dst_size 给出实际解压字节数，应与容器声明的 output_size
 *      相同；完成后由调用者释放 dst 和 workspace。
 *
 * 示例（省略输入数据的读取及错误清理）：
 *   size_t ws_size = lzma_c_api_workspace_size(lc, lp);
 *   void *workspace = malloc(ws_size);
 *   uint8_t *dst = (uint8_t *)malloc(output_size);
 *   size_t dst_size = 0;
 *   LzmaCApiResult rc = lzma_c_api_decode(
 *       raw_stream, raw_stream_size, dst, output_size, output_size,
 *       &dst_size, lc, lp, pb, workspace, ws_size);
 *   if (rc == LZMA_C_API_OK) { use_output(dst, dst_size); }
 *   free(dst);
 *   free(workspace);
 *
 * 参数约束：lc <= 8、lp <= 4、lc + lp <= 12、pb <= 4。workspace 必须由
 * 调用者提供，大小至少为 lzma_c_api_workspace_size(lc, lp)；dst_capacity
 * 必须不小于 output_size。src、dst、workspace 和 dst_size 必须指向有效内存，
 * 输入、输出和 workspace 缓冲区不得重叠。output_size 为 0 时 dst 可以为
 * NULL。
 *
 * 返回值：
 *   LZMA_C_API_OK                  解码成功；dst_size 为实际输出长度。
 *   LZMA_C_API_INVALID_ARGUMENT    参数为空、属性越界、输入少于 5 字节，
 *                                  或 range initializer 首字节非 0。
 *   LZMA_C_API_INVALID_STREAM      内部解码模板异常，或解码长度与
 *                                  output_size 不一致。
 *   LZMA_C_API_OUTPUT_TOO_SMALL    output_size 超过 dst_capacity，或非空输出
 *                                  时 dst 为空。
 *   LZMA_C_API_WORKSPACE_TOO_SMALL workspace 小于所需大小。
 *   LZMA_C_API_ALLOCATION_FAILED   Windows 无法分配运行时解码代码所需内存。
 *
 * 安全与平台限制：虽然接口接收 src_size，当前解码核心不会在读取压缩数据
 * 时检查输入边界，也不能可靠防护损坏码流造成的越界读写。因此只能用于可信、
 * 完整且属性/长度正确的码流，不能用于不可信或可能损坏的输入。当前实现依赖
 * 32 位 Windows，并在运行时分配可执行内存。
 */
size_t lzma_c_api_workspace_size(unsigned lc, unsigned lp);
LzmaCApiResult lzma_c_api_decode(const uint8_t *src, size_t src_size,
                                 uint8_t *dst, size_t dst_capacity,
                                 size_t output_size, size_t *dst_size,
                                 unsigned lc, unsigned lp, unsigned pb,
                                 void *workspace, size_t workspace_size);

#ifdef __cplusplus
}
#endif
#endif
