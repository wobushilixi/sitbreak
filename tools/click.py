"""向窗口发合成鼠标点击：python click.py <title> xDip yDip [times] [gapMs] [delayMs]

坐标用 DIP（窗口内部逻辑坐标），按客户端宽度/设计宽度自动换算物理像素。
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

ctypes.windll.shcore.SetProcessDpiAwareness(2)
u = ctypes.windll.user32

title = sys.argv[1]
xd, yd = float(sys.argv[2]), float(sys.argv[3])
times = int(sys.argv[4]) if len(sys.argv) > 4 else 1
gap = int(sys.argv[5]) if len(sys.argv) > 5 else 400
delay = int(sys.argv[6]) if len(sys.argv) > 6 else 900
designW = float(sys.argv[7]) if len(sys.argv) > 7 else 372.0
designH = float(sys.argv[8]) if len(sys.argv) > 8 else 668.0

EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
found = []


def cb(h, l):
    t = ctypes.create_unicode_buffer(256)
    u.GetWindowTextW(h, t, 256)
    if title.lower() in t.value.lower():
        r = wt.RECT()
        u.GetClientRect(h, ctypes.byref(r))
        found.append((h, r.right - r.left, r.bottom - r.top, t.value))
    return True


u.EnumWindows(EnumProc(cb), 0)
found = [f for f in found if u.IsWindowVisible(ctypes.c_void_p(f[0]))]
if not found:
    print("no window")
    sys.exit(1)
h, cw, ch, ttl = found[0]
print("click target", ttl, cw, ch)

time.sleep(delay / 1000.0)
for i in range(times):
    x = int(xd * cw / designW)
    y = int(yd * ch / designH)
    lp = (y << 16) | (x & 0xFFFF)
    u.PostMessageW(h, 0x0200, 0, lp)          # WM_MOUSEMOVE
    time.sleep(0.05)
    u.PostMessageW(h, 0x0201, 1, lp)          # WM_LBUTTONDOWN
    time.sleep(0.06)
    u.PostMessageW(h, 0x0202, 0, lp)          # WM_LBUTTONUP
    print("click", i + 1, "at", x, y)
    time.sleep(gap / 1000.0)
