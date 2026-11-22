# LzmaCApi

一个面向 32 位 Windows 的 C API 封装，调用 Ilya Kurdyukov 的 x86 静态 micro-LZMA 解码核心（CC BY 3.0）。封装在运行时生成并执行解码代码，构建出的静态库内嵌了解码器机器码模板。

## 目录结构

- `SRC/`：C API、x86 汇编源码、生成的机器码头文件及 API 测试程序。
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

构建脚本使用 NASM、Clang/Clang-cl、LLD、Python 3 和 Windows SDK/MSVC 兼容运行库，并生成 `build/lzmacapi_x86.lib`。脚本会从 `PATH` 或 `%LOCALAPPDATA%\Programs\LLVM` 查找 LLVM 工具。NASM 只在重新生成 `SRC/static_x86_blob.h` 时需要。

## API 测试

构建脚本会生成 LZMA 码流和对应的预期输出，并让测试程序链接刚生成的静态库。当前覆盖以下属性组合：

| `lc` | `lp` | `pb` | 测试内容 |
| ---: | ---: | ---: | --- |
| 0 | 0 | 0 | 最小属性组合 |
| 3 | 0 | 2 | 常用属性组合及较大输出 |
| 2 | 2 | 4 | 非默认 `lp` 和最大 `pb` |

每组都会检查完整输出是否逐字节匹配，并测试 workspace 大小查询、无效参数、输出容量不足和 workspace 不足时的返回结果。API 的参数、调用示例和返回值说明见 [`SRC/lzma_c_api.h`](SRC/lzma_c_api.h) 中的中文注释。

## 输入与安全限制

接口接收原始 LZMA1 range stream，不接收 `.lzma` 头或 7z 容器头。调用者需从外层格式取得 `lc`、`lp`、`pb` 及精确的解压长度；传入的码流至少包含 5 字节 range initializer，且首字节为 0。当前解码核心要求码流带 LZMA end marker。

尽管 API 接收 `src_size`，解码核心读取压缩数据时不会检查输入边界；损坏或截断的输入可能导致越界读写。它也不能安全处理不可信输入。请只对可信、完整且属性和解压长度正确的码流调用。实现仅支持 32 位 Windows，并会在运行时分配可执行内存。

## 当前验证

2026-11-21 使用 Clang 23.1.5（i686 Windows 目标）、NASM 3.01 和本机 Windows SDK/MSVC 兼容运行库构建。三组 API 解码测试及错误返回值测试均通过。测试程序编译有一条 `fopen` 弃用提示，不影响构建和运行。
