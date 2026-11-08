"""弹出托盘右键菜单并截图：python tray_test.py <out.png>"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

ctypes.windll.shcore.SetProcessDpiAwareness(2)
user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

out = sys.argv[1] if len(sys.argv) > 1 else "tray.png"

# 找主窗口（按类名 + 标题含 SitBreak，排除弹窗）
EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
cands = []


def _cb(h, l):
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(h, buf, 256)
    if buf.value != "SitBreakWin":
        return True
    t = ctypes.create_unicode_buffer(256)
    user32.GetWindowTextW(h, t, 256)
    if "SitBreak" in t.value and "Prompt" not in t.value and "Train" not in t.value \
            and "Settings" not in t.value and "Toast" not in t.value:
        cands.append(h)
    return True


user32.EnumWindows(EnumProc(_cb), 0)
hwnd_main = cands[0] if cands else None
if not hwnd_main:
    print("main window not found")
    sys.exit(1)
print("main hwnd", hwnd_main)

WM_TRAY = 0x8000 + 1  # WM_APP + 1
user32.PostMessageW(wt.HWND(hwnd_main), WM_TRAY, 1, 0x0205)  # WM_RBUTTONUP
time.sleep(0.6)


class GUITHREADINFO(ctypes.Structure):
    _fields_ = [
        ("cbSize", wt.DWORD), ("flags", wt.DWORD), ("hwndActive", wt.HWND),
        ("hwndFocus", wt.HWND), ("hwndCapture", wt.HWND), ("hwndMenuOwner", wt.HWND),
        ("hwndMoveSize", wt.HWND), ("hwndCaret", wt.HWND), ("rcCaret", wt.RECT),
    ]


gi = GUITHREADINFO()
gi.cbSize = ctypes.sizeof(gi)
tid = user32.GetWindowThreadProcessId(wt.HWND(hwnd_main), None)
user32.GetGUIThreadInfo(tid, ctypes.byref(gi))
hwnd_menu = gi.hwndMenuOwner

# 找真正的菜单窗口（类名 #32768）
pops = []


def _cb2(h, l):
    b = ctypes.create_unicode_buffer(128)
    user32.GetClassNameW(h, b, 128)
    if b.value == "#32768":
        pops.append(h)
    return True


user32.EnumThreadWindows(tid, EnumProc(_cb2), 0)
menu_hwnd = pops[0] if pops else None
print("menu popup", menu_hwnd)

target = menu_hwnd or hwnd_menu or hwnd_main
r = wt.RECT()
user32.GetWindowRect(wt.HWND(target), ctypes.byref(r))
w, h = r.right - r.left, r.bottom - r.top
print("rect", r.left, r.top, w, h)

from PIL import Image, ImageGrab
img = ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True)
img = img.resize((w * 2, h * 2), 1)
img.save(out)
print("saved", out, img.size)

# 关掉菜单
user32.PostMessageW(wt.HWND(target), 0x0100, 0x1B, 0)  # WM_KEYDOWN VK_ESCAPE
