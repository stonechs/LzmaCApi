# LzmaCApi Fast LZMA1

V1.0.3 只保留快速 7z 入口和静态 x86 LZMA1 解码核心，并使用 Clang `-Oz` 优先压低代码体积。它适合调用方能保证输入满足固定布局、希望减小集成体积的场景。

## 目录

- `SRC/`：fast 7z 解析器、LZMA1 汇编解码器、工作区状态定义和测试入口。
- `script/`：fast 静态库构建、ROM 回归和 LZMA1 压力测试脚本。
- `forTest/`：本地 LZMA1 归档回归集，不纳入 Git。
- `tools/`：本地工具，不纳入 Git。
- `build/`、`work/`：编译和测试中间物，不纳入 Git。
- `dist/`：版本快照、可执行文件及其 map，不纳入 Git。

## 支持范围

唯一公开归档接口是 `sevenzip_extract_single_lzma1_fast`。它只处理完整可信的 7z 文件，且要求普通 Header 后紧跟 MainStreamsInfo，包含一个非空文件、一个 packed stream 和一个 LZMA1 coder。输入还需满足代码中的固定字段布局；不支持 LZMA2、压缩 Header、加密、滤镜、多文件、多流或分卷。它不验证 7z CRC，也不适用于不可信或损坏的输入。

快速 UINT64 读取器的约束：7z 偏移和长度小于 100 MiB，且每个变长整数位置后至少有 4 个可读字节。调用方必须提供足够大的输出缓冲区。

## C 到汇编接口

C 端固定分配最大概率表 workspace。表项数按 `lc + lp = 12` 的上限取 `2048 + (768 << 12)`，共 3,147,776 个 `uint16_t`，占 6,295,552 字节（6 MiB）。汇编入口用 `rep stosw` 将每项初始化为 1024，并在调用栈上初始化 LZMA 状态和重复距离；不按每个文件的 `lc/lp` 计算表大小，也不传递 workspace 大小。cdecl 接口为：

```c
run(src, dst, workspace, output_size, lc, lp, pb);
```

汇编从 `workspace` 直接读取概率表，并以 `dst + output_size` 为停止地址。fast 入口每次分配并释放 6 MiB workspace，调用者只需准备完整归档和输出缓冲区。

## 构建和验证

需要 32 位 Windows 目标的 Clang/LLVM、NASM、Windows SDK/MSVC 兼容运行库，以及 7-Zip：

```powershell
.\script\build_fast.ps1
.\script\test_fast_lzma1.ps1
.\script\test_forTest_archives.ps1
.\script\stress_sevenzip_50.ps1
```

测试会在 `dist/builds/` 下为测试 EXE 保存对应的 `.map` 和 `build_info.txt`；解码结果及中间文件写入 `work/`。

## 当前版本

V1.0 是从 V0.6.2 继续的 LZMA1-only 基线。V1.0.1 移除了普通解析器和 raw C API 路径；V1.0.2 固定分配最大概率表 workspace，并由汇编 `rep stosw` 初始化表项和栈上解码状态；V1.0.3 将 C 构建优化切换为 Clang `-Oz`，以最小代码体积为目标。构建和回归状态见 [`docs/project_status.md`](docs/project_status.md)。
