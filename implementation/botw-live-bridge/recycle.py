"""Send files to the Windows recycle bin instead of deleting them (user rule)."""
import ctypes
import pathlib
from ctypes import wintypes


class _SHFILEOPSTRUCTW(ctypes.Structure):
    _fields_ = [('hwnd', wintypes.HWND), ('wFunc', wintypes.UINT), ('pFrom', wintypes.LPCWSTR),
                ('pTo', wintypes.LPCWSTR), ('fFlags', ctypes.c_ushort), ('fAnyOperationsAborted', wintypes.BOOL),
                ('hNameMappings', ctypes.c_void_p), ('lpszProgressTitle', wintypes.LPCWSTR)]


def recycle(*paths):
    """Recycle existing paths; raises if Windows reports a failure."""
    names = [str(pathlib.Path(p).resolve()) for p in paths if pathlib.Path(p).exists()]
    if not names:
        return
    # FO_DELETE=3; FOF_ALLOWUNDO|FOF_NOCONFIRMATION|FOF_SILENT|FOF_NOERRORUI.
    op = _SHFILEOPSTRUCTW(None, 3, '\0'.join(names) + '\0\0', None, 0x40 | 0x10 | 0x4 | 0x400, False, None, None)
    result = ctypes.windll.shell32.SHFileOperationW(ctypes.byref(op))
    if result or op.fAnyOperationsAborted or any(pathlib.Path(n).exists() for n in names):
        raise OSError(f'recycle failed ({result}) for {names}')
