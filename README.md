# LzmaCApi V1.1

LzmaCApi 是一个面向体积敏感场景的 32 位 Windows x86 解码库。它提供两个入口：一个按固定布局从 7z 文件中提取单个 LZMA1 文件；一个直接解码裸 LZMA1 range-coded 数据。C 代码负责读取容器字段和分配内存，核心解码器使用 NASM x86 汇编。C 构建使用 Clang `-Oz`，以最小代码体积为目标。

该库按受限输入格式裁剪，适合固件、ROM 工具及已知归档布局的程序集成。它不面向通用归档浏览或不可信文件处理。

## 接口概览

公共头文件为 `SRC/sevenzip_single.h`，库中有两个可链接入口：

- `sevenzip_extract_single_lzma1_fast`：接收完整 7z 文件，读取约定的固定容器布局，再调用汇编解码器。
- `lzma_static_x86_run`：接收裸 LZMA1 range-coded 数据、解压属性和输出长度，不解析容器。

发布库中 `lzma_static_x86_run` 返回 `void`。调试库在定义 `LZMA_C_API_DEBUG` 时返回输出末地址，以便调试检查。调用方声明和所链接的库必须匹配。

## 从 7z 文件提取单个文件

调用方先把完整归档读入内存，并准备足够大的、与归档不重叠的输出缓冲区。`output_capacity` 必须至少等于文件的解压长度；这个长度需要由调用方预先知道或从可信的外部元数据获得。

```c
#include "sevenzip_single.h"

SevenZipResult result;
size_t output_size = 0;

result = sevenzip_extract_single_lzma1_fast(
    archive, archive_size,
    output, output_capacity,
    &output_size);

if (result == SEVENZIP_OK) {
    /* output[0..output_size) 是解压后的文件内容；不会自动添加 NUL。 */
}
```

`archive` 和 `output` 指向调用方拥有的内存；`archive_size`、`output_capacity` 以字节为单位；成功时 `output_size` 接收 7z 头部记录的解压长度。归档入口内部申请并释放固定 6 MiB workspace。

## 解码裸 LZMA1 数据

裸接口用于调用方已经从容器中取得压缩数据和属性的场景。`src` 必须指向 range decoder 的 5 字节初始化前缀：第 1 字节为 `0`，接下来的 4 字节为初始 Code，后面紧跟压缩数据。`lc`、`lp`、`pb` 由对应 LZMA1 属性提供。

```c
#include "sevenzip_single.h"
#include <stdlib.h>

size_t output_size = known_unpacked_size;
void *workspace = malloc(6295552u);
if (!workspace) {
    /* 处理分配失败 */
}

#ifdef LZMA_C_API_DEBUG
uint8_t *end = lzma_static_x86_run(
    range_src, output, workspace, output_size, lc, lp, pb);
/* 调试库中 end 应为 output + output_size。 */
#else
lzma_static_x86_run(
    range_src, output, workspace, output_size, lc, lp, pb);
#endif

free(workspace);
```

workspace 大小固定为 `2 * (2048 + (768 << 12)) = 6,295,552` 字节；汇编入口会用 `rep stosw` 将概率表每项初始化为 1024，并在调用栈上初始化 LZMA 状态和重复距离。调用方不需要再初始化 workspace。

若输入是 LZMA-Alone 文件，调用方需要先解析其 13 字节头部：1 字节属性、4 字节字典大小、8 字节解压长度。把属性拆成 `lc = prop % 9`、`lp = (prop / 9) % 5`、`pb = prop / 45`，再将 `src` 指向头部后的 range-coded 数据。LZMA-Alone 头中的未知长度值不能直接用于本接口；本接口需要确切的 `output_size`。

## 输入和内存约束

### 7z 容器入口

`sevenzip_extract_single_lzma1_fast` 只支持同时满足以下条件的归档：

- 归档完整、可信，且位于连续可读的内存区域。
- 使用普通未压缩 Header；Header 后按当前解析器假设紧跟 MainStreamsInfo。
- 只有一个非空文件、一个 Folder、一个 packed stream 和一个 LZMA1 coder。
- coder 方法 ID 长度为 3 字节，LZMA1 属性长度为 5 字节。
- 7z 变长整数表示的偏移和长度均小于 100 MiB；每个变长整数的地址之后至少有 4 个可读字节，因为快速读取器会一次读取 4 字节。
- 调用方提供正确的 `archive_size`、有效的 `output_size` 指针，以及足以容纳解压数据的输出缓冲区。

此路径不检查 7z 签名、Start Header CRC、Next Header CRC、Pack CRC 或文件 CRC。它按固定偏移读取字段，不验证一般 7z 结构。

### 裸 LZMA1 入口

调用 `lzma_static_x86_run` 时，调用方必须保证：

- `src` 至少包含完整的 5 字节 range 初始化前缀和后续码流；本接口没有输入长度参数，解码器无法检查输入尾部。
- `output` 至少有 `output_size` 字节的可写空间；本接口没有输出容量参数。
- `lc <= 8`、`lp <= 4`、`lc + lp <= 12`、`pb <= 4`，属性必须和码流一致。
- `src`、`output`、`workspace` 三个区域互不重叠；workspace 至少 6,295,552 字节并按 `uint16_t` 对齐。
- 输入码流有效，且 `output_size` 是准确的解压长度。汇编以 `output + output_size` 为停止地址，不要求通过 end marker 决定输出长度。

两个入口都针对 32 位 x86。每次解码都会分配或要求 6 MiB workspace，并将其全部初始化；并发调用需要各自独立的 workspace 和输出缓冲区。

发布库为追求体积，不做完整参数、输入边界或输出容量验证。使用错误属性、损坏或截断的码流、错误输出长度或不足的输出缓冲区，可能导致越界读写或错误结果。调试库增加部分容器边界、属性、输出容量、匹配长度及最终长度检查；它仍不验证 CRC 或所有 7z 结构。

## 返回值

7z 容器入口返回 `SevenZipResult`：

- `SEVENZIP_OK`：解码调用完成，`output_size` 获得头部记录长度。
- `SEVENZIP_MEMORY_ERROR`：无法申请 workspace。
- 调试库可能返回 `SEVENZIP_INVALID_ARCHIVE`、`SEVENZIP_UNSUPPORTED`、`SEVENZIP_OUTPUT_TOO_SMALL` 或 `SEVENZIP_DECODE_ERROR`，用于报告对应的部分检查失败。
- `SEVENZIP_INVALID_ARGUMENT` 和 `SEVENZIP_CRC_ERROR` 当前 fast 实现不返回；CRC 检查没有实现。

发布入口假设输入已满足约束，`SEVENZIP_OK` 不能作为归档 CRC 正确或内容可信的证明。裸汇编入口的发布版本返回 `void`，解码错误检测由调用方负责。

## 构建

构建目标是 i686 Windows COFF，使用 Clang-cl、NASM、LLD 和 Windows 兼容 C 运行库。Clang 使用 `-Oz`，NASM 使用 `-O9`。

```powershell
.\script\build_fast.ps1
```

产物位于 `build/`：

- `lzmacapi_x86_fast.lib`：发布版静态库。
- `lzmacapi_x86_fast_debug.lib`：定义 `LZMA_C_API_DEBUG` 构建的调试库。

将 `SRC/` 加入 include 路径并链接对应静态库。直接使用裸接口时，调用方需包含 `sevenzip_single.h`。目标程序应使用 32 位 x86 ABI。

## 回归验证

```powershell
.\script\test_fast_lzma1.ps1
.\script\test_raw_lzma1.ps1
.\script\test_forTest_archives.ps1
.\script\stress_sevenzip_50.ps1
```

`test_raw_lzma1.ps1` 需要 Python 3 的标准 `lzma` 模块生成临时 LZMA-Alone 样本；其他回归脚本使用 Clang 和 7-Zip。测试脚本在 `dist/builds/` 保存测试 EXE、对应 Map 和 `build_info.txt`；输入、解压结果及 CSV 报告写入 `work/`。V1.1 发布验证包括 7z 与裸 LZMA1 冒烟、116 个 LZMA1 归档和 50 个生成的压力归档，发布版和调试版均通过；3 个 LZMA2 归档按支持范围跳过。

## 目录

- `SRC/`：C 容器解析器、公共头文件、x86 汇编解码器和测试程序。
- `script/`：Clang `-Oz` 构建、回归和压力测试脚本。
- `forTest/`：本地归档回归集，不纳入 Git。
- `tools/`：本地第三方工具，不纳入 Git。
- `build/`、`work/`：构建中间物和临时工作区，不纳入 Git。
- `dist/`：版本快照、构建产物和 Map，不纳入 Git。
