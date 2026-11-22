# LzmaCApi

一个面向 32 位 Windows 的 LZMA1 C 解码 API，并提供单文件 7z 容器解析层。解码汇编基于 Ilya Kurdyukov 的 x86 静态 micro-LZMA 核心（CC BY 3.0）。

## 目录结构

- `SRC/`：C API、x86 汇编源码及 API 测试程序。
- `script/`：构建脚本和 LZMA 测试数据生成脚本。
- `build/`：构建输出和测试夹具，已由 `.gitignore` 排除。
- `tools/`：第三方工具存放目录，已排除在 Git 之外。
- `work/`：临时工作目录，已排除在 Git 之外。
- `docs/`：项目状态记录。

## 构建

在 PowerShell 中运行：

```powershell
.\script\build.ps1
```

构建脚本使用 NASM、Clang/Clang-cl、LLD、Python 3 和 Windows SDK/MSVC 兼容运行库，并生成完整库 `build/lzmacapi_x86.lib` 和带参数校验的 `build/lzmacapi_x86_debug.lib`。脚本会从 `PATH` 或 `%LOCALAPPDATA%\Programs\LLVM` 查找 LLVM 工具。NASM 将汇编源码编译成 COFF 对象，构建不再生成机器码头文件。

若只需要快速 7z 入口，可单独运行：

```powershell
.\script\build_fast.ps1
```

它只编译 `sevenzip_single_fast.c`、LZMA C API 和汇编核心，不链接完整 7z Header 解析器，生成 `build/lzmacapi_x86_fast.lib` 和 `build/lzmacapi_x86_fast_debug.lib`。快速库也包含原始 LZMA C API。专用变长整数读取器要求所有 7z 偏移和长度小于 100 MiB，并且每个变长整数位置之后至少有 4 个可读字节。完整库也包含快速入口以保持既有 API 可用。

## 单文件 7z 容器 API

`SRC/sevenzip_single.h` 提供 `sevenzip_extract_single_lzma1`。调用方将完整 7z 文件读入内存并提供输出缓冲区；函数解析普通 7z Header，再调用本项目的 `lzma_c_api_decode` 解压文件数据。内部按 LZMA 属性分配 workspace。它只支持一个非空文件、一个 packed stream 和一个 LZMA1 coder；压缩 Header、LZMA2、加密、滤镜、多文件、多流和分卷均不支持。定义 `LZMA_C_API_DEBUG` 时会校验签名、CRC、Reader 读取范围、归档数据范围和输出容量；发布版省略这些检查，调用方必须提供完整有效的归档及足够大的输出缓冲区。[7z 格式定义](https://github.com/ip7z/7zip/blob/main/DOC/7zFormat.txt)

最小调用示例：

```c
size_t output_size = 0;
SevenZipResult rc = sevenzip_extract_single_lzma1(
    archive_bytes, archive_size,
    output_buffer, output_capacity, &output_size);
```

成功时 `output_size` 是实际输出长度；失败时参见 `SevenZipResult`。输出容量不足返回 `SEVENZIP_OUTPUT_TOO_SMALL`。

调试版还将解压输出限制为 16 MiB。C 容器层不调用 `archive.js` 的 JavaScript 解码模块。

7z 本身可以使用多种编码器和滤镜；该 API 是项目现有 LZMA1 汇编核心上的窄容器适配层，不是通用 7z 解包器。发布版假设结构、输入长度和输出容量都有效；调试版执行相应检查。解码核心仍不校验每次压缩数据读取是否越界，因此整个实现只应处理可信且完整的归档。

### 快速入口

`sevenzip_extract_single_lzma1_fast` 使用相同参数，跳过完整 Header 和 FilesInfo 验证，只提取解码必需的 packed 数据偏移/长度、5 字节 LZMA 属性和解压长度，然后直接调用 `decode_folder`：

```c
SevenZipResult rc = sevenzip_extract_single_lzma1_fast(
    archive_bytes, archive_size,
    output_buffer, output_capacity, &output_size);
```

它假设起始头及偏移都有效，普通 Header 的 `MainStreamsInfo` 紧跟 Header；PackInfo 后是单 folder、单 LZMA1 coder（3 字节 coder ID，5 字节属性）。不支持 ArchiveProperties、AdditionalStreamsInfo、压缩 Header、加密、滤镜、多文件、多流或分卷。可选 Pack CRC 只跳过，不验证；函数也不解析 SubStreamsInfo 和 FilesInfo。因此调用方必须保证完整可信的归档、正确的 LZMA1 字段和足够大的输出缓冲区。快速专用库还要求所有偏移和长度小于 100 MiB，并保证每个变长整数地址后至少有 4 个可读字节。结构不符合这些假设时，快速入口可能读错位置或越界。

生成真实 LZMA1 归档并测试容器层（需要安装 7-Zip）：

```powershell
.\script\build.ps1
.\script\test_sevenzip.ps1
.\script\test_forTest_archives.ps1
.\script\stress_sevenzip_50.ps1
```

## API 测试

构建脚本会生成 LZMA 码流和对应的预期输出，并让测试程序链接刚生成的静态库。当前覆盖以下属性组合：

| `lc` | `lp` | `pb` | 测试内容 |
| ---: | ---: | ---: | --- |
| 0 | 0 | 0 | 最小属性组合 |
| 3 | 0 | 2 | 常用属性组合及较大输出 |
| 2 | 2 | 4 | 非默认 `lp` 和最大 `pb` |

每组都会检查完整输出是否逐字节匹配。测试库定义 `LZMA_C_API_DEBUG`，另覆盖无效参数、输出容量不足和 workspace 不足的返回结果。正式 `build/lzmacapi_x86.lib` 不包含参数检查；调用端应按头文件中的公式自行计算 workspace 大小并确保所有参数有效。API 说明见 [`SRC/lzma_c_api.h`](SRC/lzma_c_api.h) 中的中文注释。

## 输入与安全限制

接口接收原始 LZMA1 range stream，不接收 `.lzma` 头或 7z 容器头。调用者需从外层格式取得 `lc`、`lp`、`pb` 及精确的解压长度；传入的码流至少包含 5 字节 range initializer，且首字节为 0。解码按指定的解压长度停止，不要求码流带 LZMA end marker。若 end marker 提前出现，调试版报告码流无效；发布版要求调用者提供完整且有效的码流。

尽管 API 接收 `src_size`，解码核心读取压缩数据时不会检查输入边界；损坏或截断的输入可能导致越界读写。它也不能安全处理不可信输入。请只对可信、完整且属性和解压长度正确的码流调用。实现仅支持 32 位 Windows。

解码入口通过普通 x86 cdecl 调用直接接收 `src`、`dst`、`workspace`、`output_size`、`workspace_size` 和 `lc/lp/pb`；汇编入口将 `dst + output_size` 算成输出停止地址，并将输入指针推进 5 字节，按 `workspace_size/2` 初始化概率项。`pb/lp` 掩码在入口计算并保存在栈上，`lc` 在字面量解码时作为变量移位量使用。

汇编代码不会在运行时被修改，C 层不需要修改代码页权限或刷新指令缓存。多个调用可以并发执行，但每次调用必须使用各自不重叠的输入、输出和 workspace 缓冲区。

## 当前验证

2026-11-22 使用 Clang 23.1.5（i686 Windows 目标，`/O1`）、NASM 3.01 和本机 Windows SDK/MSVC 兼容运行库构建。基础 API 测试和发布版 smoke test 通过。另用 7-Zip 26.04 生成 235 组原始 LZMA1 压力用例，覆盖 API 接受的属性组合和最长 1 MiB 码流。`forTest/` 中 12 个现有 7z 归档，以及新生成的 50 个 LZMA1 归档，都在 release/debug 构建下通过常规和快速接口解码，并与 7-Zip 解压结果逐字节一致。最新版本的 50 例结果保存在 `work/stress_sevenzip_50_20261122_125117_384/results.csv`；forTest 结果在 `work/forTest_decode_20261122_125112_340/results.csv`。测试程序编译有 `fopen` 弃用提示，不影响构建和运行。

调试版会填写 `dst_size` 并校验实际输出长度；发布版不读写 `dst_size`，可传 `NULL`，由汇编直接解码。项目构建脚本按 `/O1` 编译时，`lzma_c_api_decode` `.text` 为发布版 47 字节、调试版 235 字节；汇编段为 534/550 字节；包含快速接口的 7z 容器静态库为 7,320/9,332 字节。这些大小不等同于最终 EXE 的净增量。

另用 Clang 23.1.5 的 `-Oz -ffunction-sections` 单独编译 C 文件，并从 COFF 函数段统计代码字节：容器 C 代码合计 1,804/2,792 字节（release/debug），其中 `sevenzip_extract_single_lzma1_fast` 为 186/207 字节，完整入口为 1,202/1,783 字节；`decode_folder` 为 172/347 字节。`lzma_c_api_decode` 包装为 47/216 字节，NASM 解码核心为 534/550 字节，三部分合计 2,385/3,558 字节。快速函数直接跳过固定宽度字段，并只保存 LZMA 属性首字节。该统计不含运行库依赖、常量数据、对齐和最终链接裁剪，因此不同于最终 EXE 的增量。
