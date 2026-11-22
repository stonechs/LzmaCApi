#include "lzma_c_api.h"
#include <windows.h>

/* Implemented by the statically linked NASM code section. */
extern uint8_t lzma_static_x86_section_start[];
extern uint8_t lzma_static_x86_section_end[];
extern void __cdecl lzma_static_x86_prepare(
    const uint8_t *src, uint8_t *dst, void *workspace, uint8_t *dst_end,
    unsigned lc, unsigned lp, unsigned pb);
extern uint8_t *__cdecl lzma_static_x86_run(void);

LzmaCApiResult lzma_c_api_decode(const uint8_t *src, size_t src_size,
                                 uint8_t *dst, size_t dst_capacity,
                                 size_t output_size, size_t *dst_size,
                                 unsigned lc, unsigned lp, unsigned pb,
                                 void *workspace, size_t workspace_size)
{
    uint8_t *end;
    SIZE_T code_size;
    DWORD old_protect;
    DWORD ignored_protect;
    BOOL flush_ok;
    BOOL restore_ok;

#ifdef LZMA_C_API_DEBUG
    /* 调试构建检查输入和属性前提；解码核心本身仍不检查输入边界。 */
    if (!src || !dst_size || !workspace || src_size < 5 || src[0] != 0 || lc > 8 ||
        lp > 4 || lc + lp > 12 || pb > 4)
        return LZMA_C_API_INVALID_ARGUMENT;
#endif

    *dst_size = 0;
#ifdef LZMA_C_API_DEBUG
    if (output_size > dst_capacity || (output_size && !dst))
        return LZMA_C_API_OUTPUT_TOO_SMALL;
    if (workspace_size < (2048u + (768u << (lc + lp))) * sizeof(uint16_t))
        return LZMA_C_API_WORKSPACE_TOO_SMALL;
#else
    (void)src_size;
    (void)dst_capacity;
    (void)workspace_size;
#endif

    /*
     * 汇编位于独立静态链接段。准备入口将参数写入解码代码的立即数，故
     * 先临时开放该段的写权限；准备后刷新指令缓存并恢复原有页面权限。
     */
    code_size = (SIZE_T)((uintptr_t)lzma_static_x86_section_end -
                         (uintptr_t)lzma_static_x86_section_start);
    if (!VirtualProtect(lzma_static_x86_section_start, code_size,
                        PAGE_EXECUTE_READWRITE, &old_protect))
        return LZMA_C_API_CODE_SETUP_FAILED;

    lzma_static_x86_prepare(
        src, dst, workspace,
        (uint8_t *)((uintptr_t)dst + (uintptr_t)output_size), lc, lp, pb);
    flush_ok = FlushInstructionCache(GetCurrentProcess(),
                                     lzma_static_x86_section_start, code_size);
    restore_ok = VirtualProtect(lzma_static_x86_section_start, code_size,
                                old_protect, &ignored_protect);
    if (!flush_ok || !restore_ok)
        return LZMA_C_API_CODE_SETUP_FAILED;

    /* 该原位修补实现一次只允许一个解码调用。 */
    end = lzma_static_x86_run();

#ifdef LZMA_C_API_DEBUG
    if (end < dst || (size_t)(end - dst) != output_size)
        return LZMA_C_API_INVALID_STREAM;
#endif
    *dst_size = (size_t)(end - dst);
    return LZMA_C_API_OK;
}
