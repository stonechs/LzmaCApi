# LzmaCApi

一个面向 32 位 Windows 的 C API 封装，调用 Ilya Kurdyukov 的 x86 静态 micro-LZMA 解码核心（CC BY 3.0）。C API 和解码汇编静态链接；调用时由汇编准备入口把本次参数修补进解码指令，再执行解码主体。

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

构建脚本使用 NASM、Clang/Clang-cl、LLD、Python 3 和 Windows SDK/MSVC 兼容运行库，并生成精简库 `build/lzmacapi_x86.lib` 和带参数校验的 `build/lzmacapi_x86_debug.lib`。脚本会从 `PATH` 或 `%LOCALAPPDATA%\Programs\LLVM` 查找 LLVM 工具。NASM 将汇编源码编译成 COFF 对象，构建不再生成机器码头文件。

## API 测试

构建脚本会生成 LZMA 码流和对应的预期输出，并让测试程序链接刚生成的静态库。当前覆盖以下属性组合：

| `lc` | `lp` | `pb` | 测试内容 |
| ---: | ---: | ---: | --- |
| 0 | 0 | 0 | 最小属性组合 |
| 3 | 0 | 2 | 常用属性组合及较大输出 |
| 2 | 2 | 4 | 非默认 `lp` 和最大 `pb` |

每组都会检查完整输出是否逐字节匹配。测试库定义 `LZMA_C_API_DEBUG`，另覆盖无效参数、输出容量不足和 workspace 不足的返回结果。正式 `build/lzmacapi_x86.lib` 不包含参数检查；调用端应按头文件中的公式自行计算 workspace 大小并确保所有参数有效。API 说明见 [`SRC/lzma_c_api.h`](SRC/lzma_c_api.h) 中的中文注释。

## 输入与安全限制

接口接收原始 LZMA1 range stream，不接收 `.lzma` 头或 7z 容器头。调用者需从外层格式取得 `lc`、`lp`、`pb` 及精确的解压长度；传入的码流至少包含 5 字节 range initializer，且首字节为 0。解码按指定的解压长度停止，不要求码流带 LZMA end marker。若 end marker 提前出现，`dst_size` 会小于目标长度；调试版报告码流无效，发布版由调用者检查 `dst_size`。

尽管 API 接收 `src_size`，解码核心读取压缩数据时不会检查输入边界；损坏或截断的输入可能导致越界读写。它也不能安全处理不可信输入。请只对可信、完整且属性和解压长度正确的码流调用。实现仅支持 32 位 Windows。

解码入口通过普通 x86 cdecl 调用直接接收 `src+5`、`dst`、`workspace`、`dst_end`、`lc` 和 `lp` 参数；汇编直接从调用栈读取这些值。初始 `Code` 在解码入口从 `src[1..4]` 读取，概率表项数在初始化时按 `lc+lp` 计算。准备入口只修补 `pb/lp/lc` 对应的指令立即数。

属性对应的汇编指令会在解码前被原位修改，因此同一进程不能并发调用此 API。C 层临时将独立 `.lzma` 段设为可读、可写、可执行，修补后刷新指令缓存并恢复原页面权限；这些系统操作失败时返回 `LZMA_C_API_CODE_SETUP_FAILED`。C 层不包含汇编立即数的位置或修补表。

## 当前验证

2026-11-22 使用 Clang 23.1.5（i686 Windows 目标，`/O1`）、NASM 3.01 和本机 Windows SDK/MSVC 兼容运行库构建。三组 API 解码测试及调试版参数错误测试均通过；发布版 smoke test 解码并逐字节比较了 86,192 字节输出。测试程序编译有 `fopen` 弃用提示，不影响构建和运行。

当前工作树版本中，C API 目标代码为精简版 211 字节、调试版 422 字节；独立汇编段为精简版 567 字节、调试版 580 字节。静态库分别为 4,300 和 4,556 字节。这些大小不等同于最终 EXE 的净增量。
