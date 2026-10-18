"""Read-only resident image-page inventory of the running headless benchmark.
No process memory contents, credentials, playback data or URLs are read.
"""
import ctypes as c
from ctypes import wintypes as w
import json
import sys

kernel = c.WinDLL('kernel32', use_last_error=True)
psapi = c.WinDLL('psapi', use_last_error=True)
kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
kernel.OpenProcess.restype = w.HANDLE
kernel.CloseHandle.argtypes = [w.HANDLE]
psapi.EnumProcessModulesEx.argtypes = [w.HANDLE, c.POINTER(w.HMODULE), w.DWORD, c.POINTER(w.DWORD), w.DWORD]
psapi.GetModuleBaseNameW.argtypes = [w.HANDLE, w.HMODULE, w.LPWSTR, w.DWORD]

class ModuleInfo(c.Structure):
    _fields_ = [('base', c.c_void_p), ('size', w.DWORD), ('entry', c.c_void_p)]
class PageInfo(c.Structure):
    _fields_ = [('address', c.c_void_p), ('flags', c.c_size_t)]
psapi.GetModuleInformation.argtypes = [w.HANDLE, w.HMODULE, c.POINTER(ModuleInfo), w.DWORD]
psapi.QueryWorkingSetEx.argtypes = [w.HANDLE, c.c_void_p, w.DWORD]

handle = kernel.OpenProcess(0x410, False, int(sys.argv[1]))
assert handle
try:
    modules = (w.HMODULE * 1024)()
    needed = w.DWORD()
    assert psapi.EnumProcessModulesEx(handle, modules, c.sizeof(modules), c.byref(needed), 3)
    rows = []
    for module in modules[:needed.value // c.sizeof(w.HMODULE)]:
        info, name = ModuleInfo(), c.create_unicode_buffer(1024)
        assert psapi.GetModuleInformation(handle, module, c.byref(info), c.sizeof(info))
        assert psapi.GetModuleBaseNameW(handle, module, name, len(name))
        pages = (PageInfo * ((info.size + 4095) // 4096))()
        for index, page in enumerate(pages):
            page.address = info.base + index * 4096
        assert psapi.QueryWorkingSetEx(handle, pages, c.sizeof(pages))
        resident = sum(bool(page.flags & 1) for page in pages) * 4096
        rows.append(dict(module=name.value, resident_image_bytes=resident))
    rows.sort(key=lambda row: row['resident_image_bytes'], reverse=True)
    print(json.dumps(dict(scope='image pages only, not total process residency',
                          resident_image_bytes=sum(r['resident_image_bytes'] for r in rows), modules=rows), indent=2))
finally:
    assert kernel.CloseHandle(handle)
