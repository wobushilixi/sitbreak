"""抓取指定窗口的画面：python shot.py <title-substr> <out.png> [scale]

优先 PrintWindow(PW_RENDERFULLCONTENT)，失败（全黑）时回退到屏幕抓取（会先置顶）。
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    ctypes.windll.user32.SetProcessDPIAware()

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
target = sys.argv[1] if len(sys.argv) > 1 else "SitBreak"
out = sys.argv[2] if len(sys.argv) > 2 else "shot.png"
scale = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0

found = []


def cb(hwnd, lparam):
    if not user32.IsWindowVisible(hwnd):
        return True
    n = user32.GetWindowTextLengthW(hwnd)
    if n == 0:
        return True
    buf = ctypes.create_unicode_buffer(n + 1)
    user32.GetWindowTextW(hwnd, buf, n + 1)
    if target in buf.value:
        r = wt.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(r))
        area = (r.right - r.left) * (r.bottom - r.top)
        found.append((area, hwnd, buf.value, (r.left, r.top, r.right - r.left, r.bottom - r.top)))
    return True


user32.EnumWindows(EnumWindowsProc(cb), 0)
if not found:
    print("NOT FOUND")
    sys.exit(1)
found.sort(reverse=True)
area, hwnd, title, (x, y, w, h) = found[0]
print(f"window: {title!r} at {x},{y} size {w}x{h}")

from PIL import Image


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
        ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
        ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG),
        ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD),
    ]


hdc_win = user32.GetDC(wt.HWND(hwnd))
gdi32.CreateCompatibleDC.restype = wt.HDC
gdi32.CreateCompatibleDC.argtypes = [wt.HDC]
hdc = gdi32.CreateCompatibleDC(wt.HDC(hdc_win))
gdi32.CreateDIBSection.restype = ctypes.c_void_p
gdi32.CreateDIBSection.argtypes = [wt.HDC, ctypes.c_void_p, wt.UINT,
                                   ctypes.POINTER(ctypes.c_void_p), wt.HANDLE, wt.DWORD]
gdi32.SelectObject.restype = ctypes.c_void_p
gdi32.SelectObject.argtypes = [wt.HDC, ctypes.c_void_p]
gdi32.DeleteObject.argtypes = [ctypes.c_void_p]
gdi32.DeleteObject.restype = wt.BOOL
gdi32.DeleteDC.argtypes = [wt.HDC]
user32.ReleaseDC.argtypes = [wt.HWND, wt.HDC]
user32.PrintWindow.argtypes = [wt.HWND, wt.HDC, wt.UINT]

bi = BITMAPINFOHEADER(wt.DWORD(40), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
bits = ctypes.c_void_p()
hbmp = gdi32.CreateDIBSection(hdc, ctypes.byref(bi), 0, ctypes.byref(bits), None, 0)
gdi32.SelectObject(hdc, hbmp)
user32.PrintWindow(wt.HWND(hwnd), hdc, 2)  # PW_RENDERFULLCONTENT

raw = ctypes.string_at(bits, w * h * 4)
img = Image.frombuffer("RGB", (w, h), raw, "raw", "BGRX", 0, 1).copy()
gray = img.convert("L")
mean = sum(gray.histogram()[i] * i for i in range(256)) / (w * h)
gdi32.DeleteObject(hbmp)
gdi32.DeleteDC(hdc)
user32.ReleaseDC(hwnd, hdc_win)

if mean < 6.0:
    # PrintWindow 拿不到内容 → 置顶后屏幕抓取
    user32.ShowWindow(hwnd, 9)
    user32.SetWindowPos(hwnd, -1, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040)
    user32.BringWindowToTop(hwnd)
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.5)
    from PIL import ImageGrab
    img = ImageGrab.grab(bbox=(x, y, x + w, y + h), all_screens=True)
    user32.SetWindowPos(hwnd, -2, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040)
    print("fallback: screen grab")

if scale != 1.0:
    img = img.resize((int(img.width * scale), int(img.height * scale)), 1)
img.save(out)
print(f"saved {out} {img.size}")
