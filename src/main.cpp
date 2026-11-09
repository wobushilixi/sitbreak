// 起身吧 SitBreak —— Direct2D 界面层
#include "common.h"
#include <vector>
#include <algorithm>
#include <dwmapi.h>
#include <mmsystem.h>
#include <shobjidl.h>
#include <propvarutil.h>
#include <propkey.h>

// ---------------------------------------------------------------- 全局
static Com<ID2D1Factory>   g_d2d;
static Com<IDWriteFactory> g_dw;
static std::map<std::wstring, IDWriteTextFormat*> g_fmt;
static bool  g_animTimer = false;
static float g_scale = 1.f;
static UINT  g_dpi = 96;

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif
#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER + 0)
#endif
#ifndef NIN_KEYSELECT
#define NIN_KEYSELECT (NIN_SELECT | 0x1)
#endif

// 控件 id
enum {
    ID_SETTINGS = 10, ID_MIN = 11, ID_CLOSE = 12,
    ID_ACTION = 20, ID_SNOOZE = 21, ID_PAUSE = 22, ID_PLAY = 23,
    ID_P_CLOSE = 30, ID_P_START = 31, ID_P_LATER = 32, ID_P_SIT = 33,
    ID_T_CLOSE = 40, ID_T_PAUSE = 41, ID_T_NEXT = 42, ID_T_BACK = 43,
    ID_PL_CLOSE = 60, ID_PL_AGAIN = 61, ID_PL_DONE = 62, ID_PL_CARD = 100,   // 100 + PlayKind
    ID_S_IV_DN = 50, ID_S_IV_UP = 51, ID_S_ID_DN = 52, ID_S_ID_UP = 53,
    ID_S_SOUND = 54, ID_S_AUTO = 55, ID_S_TOP = 56, ID_S_TRAY = 57, ID_S_DONE = 58,
    ID_S_X = 158
};

// 弹簧键
enum { K_APPEAR = 1, K_RING = 2, K_CHECK = 3, K_BAR = 100 };

void AppLog(const wchar_t* s) {
    wchar_t path[MAX_PATH];
    if (!GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH)) return;
    std::wstring p = std::wstring(path) + L"\\SitBreak\\run.log";
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"a, ccs=UTF-8") == 0 && f) {
        fwprintf(f, L"[%lu] %ls (err=%lu)\n", GetTickCount(), s, GetLastError());
        fclose(f);
    }
}

static void Log(const wchar_t* s) { AppLog(s); }

// ---------------------------------------------------------------- 窗口对象
struct Hot {
    int id = 0;
    D2D1_RECT_F r = {};
};

struct Win {
    HWND hwnd = nullptr;
    Com<ID2D1HwndRenderTarget> rt;
    std::vector<Hot> hots;
    int hover = 0, press = 0;
    int W = 0, H = 0;            // DIP
    std::unordered_map<int, Spring> sp;
};

static Win g_main, g_prompt, g_train, g_set, g_toast, g_play;
static std::wstring g_toastText;
static bool g_demo = false;
static bool g_silent = false;
static int g_setAuto = 0;   // 1 开 / -1 关 / 0 不处理
static bool g_quitReq = false;   // 第二个实例带 --quit：让已在跑的实例退出
static bool g_showReq = false;   // 第二个实例带 --show：把主界面叫出来
static bool g_breakReq = false;  // 第二个实例带 --break：立刻起身
static bool g_pauseReq = false;  // 第二个实例带 --pause：暂停/继续
static std::wstring g_demoKind;
static UINT g_msgTaskbarCreated = 0;
static int g_promptX = 0, g_promptY = 0;
static int g_toastX = 0, g_toastY = 0;

static Spring& SP(Win& w, int key, float init = 0.f) {
    auto it = w.sp.find(key);
    if (it == w.sp.end()) {
        Spring s(init);
        it = w.sp.emplace(key, s).first;
    }
    return it->second;
}

// ---------------------------------------------------------------- 文本格式
static IDWriteTextFormat* FMT(float size, bool bold, int align, bool wrap) {
    wchar_t key[64];
    swprintf(key, 64, L"t%.2f|%d|%d|%d", size, bold ? 1 : 0, align, wrap ? 1 : 0);
    auto it = g_fmt.find(key);
    if (it != g_fmt.end()) return it->second;

    IDWriteTextFormat* f = nullptr;
    HRESULT hr = g_dw->CreateTextFormat(theme::FONT, nullptr,
                                        bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD
                                             : DWRITE_FONT_WEIGHT_NORMAL,
                                        DWRITE_FONT_STYLE_NORMAL,
                                        DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &f);
    if (FAILED(hr) || !f) return nullptr;
    f->SetTextAlignment(align == 0 ? DWRITE_TEXT_ALIGNMENT_LEADING
                                   : align == 1 ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                : DWRITE_TEXT_ALIGNMENT_TRAILING);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    g_fmt[key] = f;
    return f;
}

static IDWriteTextFormat* FMT_ICON(float size) {
    wchar_t key[64];
    swprintf(key, 64, L"ic%.2f", size);
    auto it = g_fmt.find(key);
    if (it != g_fmt.end()) return it->second;
    IDWriteTextFormat* f = nullptr;
    HRESULT hr = g_dw->CreateTextFormat(theme::ICON_FONT, nullptr,
                                        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                        DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &f);
    if (FAILED(hr) || !f) return nullptr;
    f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    g_fmt[key] = f;
    return f;
}

static float TW(const std::wstring& s, float size, bool bold) {
    IDWriteTextFormat* f = FMT(size, bold, 0, false);
    if (!f || s.empty()) return 0;
    IDWriteTextLayout* lay = nullptr;
    if (FAILED(g_dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 2000, 100, &lay)) || !lay)
        return 0;
    DWRITE_TEXT_METRICS m = {};
    lay->GetMetrics(&m);
    lay->Release();
    return m.widthIncludingTrailingWhitespace;
}

// ---------------------------------------------------------------- 绘制上下文
struct Ctx {
    ID2D1HwndRenderTarget* rt = nullptr;
    float alpha = 1.f;
    Win* w = nullptr;

    D2D1_COLOR_F C(D2D1_COLOR_F c) { return withA(c, c.a * alpha); }

    void fillRect(D2D1_RECT_F r, D2D1_COLOR_F c) {
        ID2D1SolidColorBrush* b = nullptr;
        rt->CreateSolidColorBrush(C(c), &b);
        rt->FillRectangle(r, b);
        b->Release();
    }
    void fillR(D2D1_RECT_F r, float rad, D2D1_COLOR_F c) {
        if (r.right - r.left < 0.5f) return;
        ID2D1SolidColorBrush* b = nullptr;
        rt->CreateSolidColorBrush(C(c), &b);
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), b);
        b->Release();
    }
    void strokeR(D2D1_RECT_F r, float rad, D2D1_COLOR_F c, float width) {
        ID2D1SolidColorBrush* b = nullptr;
        rt->CreateSolidColorBrush(C(c), &b);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(r, rad, rad), b, width);
        b->Release();
    }
    void hline(float x0, float x1, float y, D2D1_COLOR_F c) {
        ID2D1SolidColorBrush* b = nullptr;
        rt->CreateSolidColorBrush(C(c), &b);
        rt->DrawLine(D2D1::Point2F(x0, y), D2D1::Point2F(x1, y), b, 1.f);
        b->Release();
    }
    void txt(const std::wstring& s, D2D1_RECT_F r, float size, bool bold, D2D1_COLOR_F c,
             int align = 0, bool wrap = false) {
        if (s.empty()) return;
        IDWriteTextFormat* f = FMT(size, bold, align, wrap);
        if (!f) return;
        ID2D1SolidColorBrush* b = nullptr;
        rt->CreateSolidColorBrush(C(c), &b);
        rt->DrawTextW(s.c_str(), (UINT32)s.size(), f, r, b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        b->Release();
    }
    void glyph(const wchar_t* g, D2D1_RECT_F r, float size, D2D1_COLOR_F c) {
        IDWriteTextFormat* f = FMT_ICON(size);
        if (!f) return;
        ID2D1SolidColorBrush* b = nullptr;
        rt->CreateSolidColorBrush(C(c), &b);
        rt->DrawTextW(g, 1, f, r, b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        b->Release();
    }
    void arc(D2D1_POINT_2F c, float radius, float width, float sweepDeg, D2D1_COLOR_F col) {
        if (sweepDeg <= 0.1f || radius <= 0) return;
        if (sweepDeg > 359.6f) sweepDeg = 359.6f;
        ID2D1PathGeometry* geo = nullptr;
        g_d2d->CreatePathGeometry(&geo);
        if (!geo) return;
        ID2D1GeometrySink* sink = nullptr;
        geo->Open(&sink);
        if (sink) {
            float startAngle = -90.f * 3.14159265f / 180.f;
            D2D1_POINT_2F p0 = D2D1::Point2F(c.x + radius * cosf(startAngle),
                                             c.y + radius * sinf(startAngle));
            float endAngle = startAngle + sweepDeg * 3.14159265f / 180.f;
            D2D1_POINT_2F p1 = D2D1::Point2F(c.x + radius * cosf(endAngle),
                                             c.y + radius * sinf(endAngle));
            sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
            D2D1_ARC_SEGMENT seg = {};
            seg.point = p1;
            seg.size = D2D1::SizeF(radius, radius);
            seg.rotationAngle = 0;
            seg.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
            seg.arcSize = sweepDeg > 180.f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
            sink->AddArc(seg);
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            sink->Release();
            ID2D1SolidColorBrush* b = nullptr;
            rt->CreateSolidColorBrush(C(col), &b);
            ID2D1StrokeStyle* st = nullptr;
            g_d2d->CreateStrokeStyle(
                D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                            D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                nullptr, 0, &st);
            rt->DrawGeometry(geo, b, width, st);
            if (st) st->Release();
            b->Release();
        }
        geo->Release();
    }
    void poly(const std::vector<D2D1_POINT_2F>& pts, D2D1_COLOR_F col, float width) {
        if (pts.size() < 2) return;
        ID2D1PathGeometry* geo = nullptr;
        g_d2d->CreatePathGeometry(&geo);
        if (!geo) return;
        ID2D1GeometrySink* sink = nullptr;
        geo->Open(&sink);
        if (sink) {
            sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
            for (size_t i = 1; i < pts.size(); i++) sink->AddLine(pts[i]);
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            sink->Release();
            ID2D1SolidColorBrush* b = nullptr;
            rt->CreateSolidColorBrush(C(col), &b);
            ID2D1StrokeStyle* st = nullptr;
            g_d2d->CreateStrokeStyle(
                D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                            D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                nullptr, 0, &st);
            rt->DrawGeometry(geo, b, width, st);
            if (st) st->Release();
            b->Release();
        }
        geo->Release();
    }
    void circle(float cx, float cy, float r, D2D1_COLOR_F col) {
        if (r <= 0.1f) return;
        fillR(D2D1::RectF(cx - r, cy - r, cx + r, cy + r), r, col);
    }
    void ellipse(float cx, float cy, float rx, float ry, D2D1_COLOR_F col) {
        if (rx <= 0.1f || ry <= 0.1f) return;
        ID2D1SolidColorBrush* b = nullptr;
        rt->CreateSolidColorBrush(C(col), &b);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), b);
        b->Release();
    }
    void tri(D2D1_POINT_2F p0, D2D1_POINT_2F p1, D2D1_POINT_2F p2, D2D1_COLOR_F col) {
        ID2D1PathGeometry* geo = nullptr;
        g_d2d->CreatePathGeometry(&geo);
        if (!geo) return;
        ID2D1GeometrySink* sink = nullptr;
        geo->Open(&sink);
        if (sink) {
            sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(p1);
            sink->AddLine(p2);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            ID2D1SolidColorBrush* b = nullptr;
            rt->CreateSolidColorBrush(C(col), &b);
            rt->FillGeometry(geo, b);
            b->Release();
        }
        geo->Release();
    }
    void hot(int id, D2D1_RECT_F r) {
        if (w) {
            Hot h;
            h.id = id;
            h.r = r;
            w->hots.push_back(h);
        }
    }
    void iconBtn(Win& win, int id, D2D1_RECT_F r, const wchar_t* g, float iconSize) {
        float hv = SP(win, id).v;
        float pr = SP(win, 10000 + id, 1.f).v;
        D2D1_POINT_2F c = D2D1::Point2F((r.left + r.right) / 2, (r.top + r.bottom) / 2);
        float hw = (r.right - r.left) / 2 * pr;
        float hh = (r.bottom - r.top) / 2 * pr;
        if (hv > 0.01f) {
            fillR(D2D1::RectF(c.x - hw, c.y - hh, c.x + hw, c.y + hh), hw,
                  withA(D2D1::ColorF(0x8A94A2), 0.13f * hv));
        }
        D2D1_COLOR_F fg = mix(theme::MUTED, theme::INK, hv);
        glyph(g, D2D1::RectF(c.x - hw, c.y - hh, c.x + hw, c.y + hh), iconSize, fg);
        hot(id, r);
    }
};

// ---------------------------------------------------------------- 通用组件
static void Card(Ctx& c, D2D1_RECT_F r, float rad, D2D1_COLOR_F fill) {
    c.fillR(D2D1::RectF(r.left, r.top + 1, r.right, r.bottom + 1), rad,
            withA(D2D1::ColorF(0x0F1620), 0.05f));
    c.fillR(r, rad, fill);
}

static void Button(Ctx& c, Win& w, int id, D2D1_RECT_F r, const std::wstring& label,
                   int kind /*0 primary,1 ghost,2 soft*/, float size = 15.f) {
    float hv = SP(w, id).v;
    float pr = SP(w, 10000 + id, 1.f).v;
    D2D1_RECT_F rr = D2D1::RectF(r.left, r.top + (1.f - pr) * 3.f, r.right, r.bottom);
    D2D1_COLOR_F bg;
    D2D1_COLOR_F fg;
    if (kind == 0) {
        bg = mix(theme::GREEN, theme::GREEN_D, hv * 0.85f);
        fg = D2D1::ColorF(0xFFFFFF);
    } else if (kind == 1) {
        bg = mix(withA(theme::INK, 0.f), D2D1::ColorF(0x8A94A2), hv * 0.30f);
        fg = mix(theme::SUB, theme::INK, hv);
    } else {
        bg = mix(theme::GREEN_S, D2D1::ColorF(0xD6F3E4), hv);
        fg = theme::GREEN_D;
    }
    if (kind == 1 && hv < 0.02f) {
        c.strokeR(rr, 14, theme::LINE, 1.2f);
    } else if (kind == 0) {
        c.fillR(D2D1::RectF(rr.left, rr.top + 2, rr.right, rr.bottom + 2), 15,
                withA(theme::GREEN_D, 0.16f));
        c.fillR(rr, 15, bg);
    } else {
        c.fillR(rr, 14, bg);
    }
    D2D1_RECT_F tr = rr;
    tr.top += (1.f - pr) * 2.f;
    c.txt(label, tr, size, kind == 0, fg, 1);
    c.hot(id, r);
}

static void Switch(Ctx& c, Win& w, int id, float x, float y, bool on) {
    float k = SP(w, id, on ? 1.f : 0.f).v;
    D2D1_RECT_F r = D2D1::RectF(x, y, x + 46, y + 28);
    D2D1_COLOR_F track = mix(theme::TRACK, theme::GREEN, k);
    c.fillR(r, 14, track);
    float knobX = x + 3 + k * 18;
    c.fillR(D2D1::RectF(knobX, y + 3, knobX + 22, y + 25), 11, D2D1::ColorF(0xFFFFFF));
    if (SP(w, id).moving()) {
        // 动画中
    }
    c.hot(id, r);
}

static void Stepper(Ctx& c, Win& w, int idDn, int idUp, float right, float y,
                    const std::wstring& value, const wchar_t* unit) {
    float h = 34;
    D2D1_RECT_F rUp = D2D1::RectF(right - h, y, right, y + h);
    D2D1_RECT_F rDn = D2D1::RectF(right - h - 8 - h, y, right - h - 8, y + h);
    float vw = 54;
    D2D1_RECT_F rV = D2D1::RectF(rDn.left - 4 - vw, y, rDn.left - 4, y + h);

    auto draw = [&](int id, D2D1_RECT_F r, const wchar_t* g) {
        float hv = SP(w, id).v;
        c.fillR(r, 12, mix(theme::CARD, theme::TRACK, hv));
        c.strokeR(r, 12, hv > 0.3f ? theme::TRACK : theme::LINE, 1.2f);
        c.glyph(g, r, 11, mix(theme::SUB, theme::INK, hv));
        c.hot(id, r);
    };
    draw(idDn, rDn, L"\uE738");
    draw(idUp, rUp, L"\uE710");
    c.txt(value, D2D1::RectF(rV.left, rV.top, rV.right - 19, rV.bottom), 15, true,
          theme::INK, 2);
    c.txt(unit, D2D1::RectF(rV.right - 14, rV.top + 4, rV.right, rV.bottom), 10.5f, false,
          theme::MUTED, 0);
}

// ---------------------------------------------------------------- 主窗口
static void PaintMain(Ctx& c, Win& w) {
    const float W = (float)w.W;
    c.fillRect(D2D1::RectF(0, 0, W, (float)w.H), theme::WINDOW);
    App& a = App::I();

    // 标题栏
    c.fillR(D2D1::RectF(22, 29, 30, 37), 4, theme::GREEN);
    c.txt(L"起身吧", D2D1::RectF(40, 22, 200, 44), 15.5f, true, theme::INK, 0);
    c.iconBtn(w, ID_SETTINGS, D2D1::RectF(250, 18, 282, 50), L"\uE713", 14);
    c.iconBtn(w, ID_MIN, D2D1::RectF(286, 18, 318, 50), L"\uE921", 14);
    c.iconBtn(w, ID_CLOSE, D2D1::RectF(322, 18, 354, 50), L"\uE711", 13);

    // 主卡
    D2D1_RECT_F hero = D2D1::RectF(22, 66, 350, 236);
    Card(c, hero, 22, theme::CARD);

    int mins = a.work / 60;
    std::wstring num = std::to_wstring(mins);
    c.txt(L"已连续坐", D2D1::RectF(44, 84, 200, 104), 12.f, false, theme::SUB, 0);
    c.txt(num, D2D1::RectF(44, 100, 210, 166), 46.f, true, theme::INK, 0);
    float nw = TW(num, 46.f, true);
    c.txt(L"分钟", D2D1::RectF(44 + nw + 7, 135, 215, 159), 13.f, false, theme::SUB, 0);

    std::wstring goal = L"目标 " + std::to_wstring(a.intervalMin) + L" 分钟 · 还剩 " +
                        std::to_wstring((a.RemainSeconds() + 59) / 60) + L" 分钟";
    if (a.state == ST_PAUSED) goal = L"提醒已暂停";
    if (a.state == ST_REST) goal = L"休息中 · 放松一下";
    if (a.state == ST_SNOOZE) goal = L"已延后提醒";
    c.txt(goal, D2D1::RectF(44, 192, 232, 212), 11.5f, false, theme::MUTED, 0);

    // 进度环
    D2D1_POINT_2F cc = D2D1::Point2F(272, 151);
    float rad = 50.f;
    c.arc(cc, rad, 9.f, 359.6f, theme::TRACK);
    float frac = a.Progress();
    if (frac > 1.f) frac = 1.f;
    if (frac < 0.f) frac = 0.f;
    if (frac > 0.03f) c.arc(cc, rad, 9.f, 359.6f * frac, theme::GREEN);
    std::wstring pct = std::to_wstring((int)(frac * 100 + 0.5f)) + L"%";
    c.txt(pct, D2D1::RectF(cc.x - 40, cc.y - 11, cc.x + 40, cc.y + 11), 13.f, true,
          theme::SUB, 1);

    // 状态行
    c.fillR(D2D1::RectF(22, 253, 28, 259), 3,
            a.state == ST_PAUSED ? theme::MUTED : theme::GREEN);
    c.txt(a.StatusText(), D2D1::RectF(36, 244, 350, 266), 12.5f, false,
          a.state == ST_PAUSED ? theme::MUTED : theme::SUB, 0);

    // 三个统计
    std::wstring vals[3];
    const wchar_t* labels[3] = {L"今日起身", L"完成训练", L"久坐时长"};
    vals[0] = std::to_wstring(a.Today().breaks);
    vals[1] = std::to_wstring(a.Today().exercises);
    int sitMin = a.Today().sit / 60;
    vals[2] = sitMin >= 60 ? (std::to_wstring(sitMin / 60) + L"h") : (std::to_wstring(sitMin) + L"m");

    float cw = 102, gap = 11, y0 = 274;
    for (int i = 0; i < 3; i++) {
        float x = 22 + i * (cw + gap);
        D2D1_RECT_F r = D2D1::RectF(x, y0, x + cw, y0 + 84);
        Card(c, r, 18, theme::CARD);
        c.txt(vals[i], D2D1::RectF(x + 15, y0 + 12, x + cw - 10, y0 + 48), 23.f, true,
              theme::INK, 0);
        c.txt(labels[i], D2D1::RectF(x + 15, y0 + 50, x + cw - 10, y0 + 70), 11.f, false,
              theme::MUTED, 0);
    }

    // 7 天柱状图
    D2D1_RECT_F ch = D2D1::RectF(22, 366, 350, 502);
    Card(c, ch, 22, theme::CARD);
    int maxv = 3, total = 0;
    for (int i = 0; i < 7; i++) {
        int v = a.WeekBreaks(6 - i);
        total += v;
        if (v > maxv) maxv = v;
    }
    c.txt(L"最近 7 天", D2D1::RectF(44, 378, 200, 398), 12.5f, false, theme::SUB, 0);
    c.txt(L"共 " + std::to_wstring(total) + L" 次", D2D1::RectF(220, 379, 328, 397), 11.5f,
          false, theme::MUTED, 2);
    if (total == 0) {
        c.txt(L"起身后会在这里留下记录", D2D1::RectF(44, 424, 328, 448), 11.5f, false,
              theme::MUTED, 1);
    }

    const wchar_t* wd[7] = {L"一", L"二", L"三", L"四", L"五", L"六", L"日"};
    SYSTEMTIME st;
    GetLocalTime(&st);
    int todayIdx = (st.wDayOfWeek + 6) % 7;
    float base = 470.f, top = 406.f, bw = 22.f, bx = 44.f;
    float stepx = (328.f - 44.f - 7 * bw) / 6.f;
    for (int i = 0; i < 7; i++) {
        int v = a.WeekBreaks(6 - i);
        float x = bx + i * (bw + stepx);
        c.fillR(D2D1::RectF(x, base - 3, x + bw, base), 1.5f, theme::TRACK);
        float hgt = (base - top) * ((float)v / (float)maxv);
        if (hgt > 0.5f) {
            D2D1_COLOR_F col = (i == 6) ? theme::GREEN : D2D1::ColorF(0xC9D5E2);
            c.fillR(D2D1::RectF(x, base - hgt, x + bw, base), bw / 2, col);
            if (i == 6) {
                c.txt(std::to_wstring(v),
                      D2D1::RectF(x - 10, base - hgt - 20, x + bw + 10, base - hgt - 4),
                      11.f, true, theme::GREEN_D, 1);
            }
        }
        int idx = (todayIdx - (6 - i) + 14) % 7;
        c.txt(wd[idx], D2D1::RectF(x - 6, base + 6, x + bw + 6, base + 22), 10.5f,
              i == 6, i == 6 ? theme::SUB : theme::MUTED, 1);
    }

    // 主按钮
    Button(c, w, ID_ACTION, D2D1::RectF(22, 512, 350, 566), L"现在起身", 0, 16.5f);

    // 次要按钮
    Button(c, w, ID_PLAY, D2D1::RectF(22, 576, 180, 620), L"坐一分钟", 2, 13.5f);
    std::wstring pl = (a.state == ST_PAUSED) ? L"恢复提醒" : L"暂停提醒";
    Button(c, w, ID_PAUSE, D2D1::RectF(192, 576, 350, 620), pl, 1, 13.f);

    // 页脚：左边提示，右边「稍后提醒」文字按钮
    c.txt(a.trayOnClose ? L"关闭后仍在托盘计时" : L"关闭窗口即退出",
          D2D1::RectF(22, 630, 236, 648), 10.f, false, theme::MUTED, 0);
    float shv = SP(w, ID_SNOOZE).v;
    c.txt(L"5 分钟后提醒", D2D1::RectF(244, 630, 350, 648), 10.f, shv > 0.5f,
          mix(theme::MUTED, theme::GREEN_D, shv), 2);
    c.hot(ID_SNOOZE, D2D1::RectF(242, 626, 350, 650));
}

// ---------------------------------------------------------------- 提醒弹窗
static void PaintPrompt(Ctx& c, Win& w) {
    const float W = (float)w.W;
    c.fillRect(D2D1::RectF(0, 0, W, (float)w.H), D2D1::ColorF(0xFFFFFF));

    App& a = App::I();
    Exercise* e = a.current;

    c.fillR(D2D1::RectF(18, 16, 62, 60), 22, theme::GREEN_S);
    c.glyph(L"\uE77B", D2D1::RectF(18, 16, 62, 60), 20, theme::GREEN);

    c.txt(L"该起身啦", D2D1::RectF(74, 14, W - 44, 38), 15.f, true, theme::INK, 0);
    std::wstring sub = L"已连续坐 " + std::to_wstring(a.work / 60) + L" 分钟";
    if (e) sub += L" · 建议 " + e->name + L" " + std::to_wstring(e->dur) + L" 秒";
    c.txt(sub, D2D1::RectF(74, 39, W - 18, 60), 11.5f, false, theme::SUB, 0);

    c.iconBtn(w, ID_P_CLOSE, D2D1::RectF(W - 38, 12, W - 14, 36), L"\uE711", 11);

    Button(c, w, ID_P_START, D2D1::RectF(W - 18 - 104, 72, W - 18, 108), L"开始拉伸", 0, 13.f);
    Button(c, w, ID_P_SIT, D2D1::RectF(W - 18 - 104 - 8 - 88, 72, W - 18 - 104 - 8, 108),
           L"坐一分钟", 2, 12.5f);
    Button(c, w, ID_P_LATER, D2D1::RectF(W - 18 - 104 - 8 - 88 - 8 - 64, 72, W - 18 - 104 - 8 - 88 - 8,
                                         108),
           L"稍后", 1, 12.5f);
}

// ---------------------------------------------------------------- 训练窗口
static void PaintTrain(Ctx& c, Win& w) {
    const float W = (float)w.W, H = (float)w.H;
    c.fillRect(D2D1::RectF(0, 0, W, H), theme::WINDOW);
    App& a = App::I();
    Exercise* e = a.current;
    if (!e) return;

    int n = (int)e->steps.size();
    bool done = a.stepIdx >= n;
    c.txt(e->name, D2D1::RectF(26, 20, 260, 44), 15.f, true, theme::INK, 0);
    std::wstring cnt = done ? std::wstring(L"已完成")
                            : std::to_wstring(a.stepIdx + 1) + L" / " + std::to_wstring(n);
    c.txt(cnt, D2D1::RectF(W - 108, 22, W - 34, 42), 12.f, false, theme::MUTED, 2);
    c.iconBtn(w, ID_T_CLOSE, D2D1::RectF(W - 32, 16, W - 8, 40), L"\uE711", 11);

    D2D1_POINT_2F cc = D2D1::Point2F(W / 2, 214);
    float rad = 82.f;

    if (!done) {
        c.arc(cc, rad, 12.f, 359.6f, theme::TRACK);
        float stepFrac = 1.f;
        if (a.stepIdx < n) {
            int total = e->steps[a.stepIdx].secs;
            stepFrac = total > 0 ? (float)a.stepLeft / (float)total : 0.f;
        }
        c.arc(cc, rad, 12.f, 359.6f * stepFrac, a.trainPaused ? theme::MUTED : theme::GREEN);

        c.txt(std::to_wstring(a.stepLeft), D2D1::RectF(cc.x - 60, cc.y - 36, cc.x + 60, cc.y + 12),
              44.f, true, theme::INK, 1);
        c.txt(L"秒", D2D1::RectF(cc.x - 30, cc.y + 18, cc.x + 30, cc.y + 42), 12.f, false,
              theme::MUTED, 1);

        c.txt(e->steps[a.stepIdx].text, D2D1::RectF(40, 320, W - 40, 392), 17.f, true,
              theme::INK2, 1, true);
        c.txt(e->tip, D2D1::RectF(40, 398, W - 40, 424), 11.5f, false, theme::MUTED, 1, true);

        float dotY = 442, dotW = 8, gapD = 10;
        float totalW = n * dotW + (n - 1) * gapD;
        float dx = W / 2 - totalW / 2;
        for (int i = 0; i < n; i++) {
            D2D1_COLOR_F col = i < a.stepIdx ? theme::GREEN_L
                                             : (i == a.stepIdx ? theme::GREEN : theme::TRACK);
            float wdt = (i == a.stepIdx) ? 18.f : dotW;
            c.fillR(D2D1::RectF(dx, dotY, dx + wdt, dotY + dotW), dotW / 2, col);
            dx += wdt + gapD;
        }

        Button(c, w, ID_T_PAUSE, D2D1::RectF(26, H - 84, 146, H - 32),
               a.trainPaused ? L"继续" : L"暂停", 1, 13.5f);
        Button(c, w, ID_T_NEXT, D2D1::RectF(156, H - 84, W - 26, H - 32),
               a.stepIdx + 1 >= n ? L"完成" : L"下一步", 0, 15.f);
        return;
    }

    // 完成态：勾选弹跳 + 短暂停留
    Spring& ks = SP(w, K_CHECK, 0.f);
    if (ks.target < 0.5f) {
        ks.snap(0.f);
        ks.k = 250.f;
        ks.c = 13.f;
        ks.to(1.f);
    }
    float k = ks.v;
    c.arc(cc, rad, 12.f, 359.6f, theme::GREEN_S);
    float r = 58.f * k;
    if (r > 0.5f) {
        c.fillR(D2D1::RectF(cc.x - r, cc.y - r, cc.x + r, cc.y + r), r, theme::GREEN);
        float gs = 34.f * k;
        c.glyph(L"\uE73E", D2D1::RectF(cc.x - gs, cc.y - gs - 2 * k, cc.x + gs, cc.y + gs + 2 * k),
                44.f * k, D2D1::ColorF(1, 1, 1, 1));
    }

    c.txt(L"完成啦", D2D1::RectF(40, 316, W - 40, 352), 22.f, true, theme::INK, 1);
    c.txt(L"今天第 " + std::to_wstring(a.doneToday) + L" 次起身活动，保持这个节奏",
          D2D1::RectF(40, 358, W - 40, 384), 11.5f, false, theme::SUB, 1, true);
    c.txt(e->tip, D2D1::RectF(40, 396, W - 40, 420), 11.f, false, theme::MUTED, 1, true);
    Button(c, w, ID_T_NEXT, D2D1::RectF(26, H - 84, W - 26, H - 32), L"太好了", 0, 15.f);
}

// ---------------------------------------------------------------- 设置窗口
static void PaintSettings(Ctx& c, Win& w) {
    const float W = (float)w.W, H = (float)w.H;
    c.fillRect(D2D1::RectF(0, 0, W, H), theme::WINDOW);
    App& a = App::I();

    c.txt(L"设置", D2D1::RectF(24, 20, 200, 48), 16.f, true, theme::INK, 0);
    c.iconBtn(w, ID_S_X, D2D1::RectF(W - 50, 16, W - 22, 44), L"\uE711", 11);

    float y = 64, rowH = 62;
    auto row = [&](const wchar_t* title, const wchar_t* sub, int i) {
        float top = y + i * rowH;
        c.txt(title, D2D1::RectF(24, top + 4, 220, top + 28), 13.5f, false, theme::INK, 0);
        c.txt(sub, D2D1::RectF(24, top + 28, 236, top + 48), 10.5f, false, theme::MUTED, 0);
        c.hline(24, W - 24, top + rowH - 8, theme::LINE);
        return top;
    };

    row(L"提醒间隔", L"连续工作这么久后提醒起身", 0);
    Stepper(c, w, ID_S_IV_DN, ID_S_IV_UP, W - 24, y + 9,
            std::to_wstring(a.intervalMin), L"分");

    row(L"起身判定", L"离开屏幕超过这个时间算起身", 1);
    Stepper(c, w, ID_S_ID_DN, ID_S_ID_UP, W - 24, y + rowH + 9,
            std::to_wstring(a.idleMin), L"分");

    row(L"提示音", L"提醒时播放一声轻响", 2);
    Switch(c, w, ID_S_SOUND, W - 24 - 46, y + 2 * rowH + 15, a.sound);

    row(L"开机自启", L"开机后自动在托盘常驻", 3);
    Switch(c, w, ID_S_AUTO, W - 24 - 46, y + 3 * rowH + 15, a.autostart);

    row(L"提醒时置顶", L"弹窗显示在最上层", 4);
    Switch(c, w, ID_S_TOP, W - 24 - 46, y + 4 * rowH + 15, a.topmostPrompt);

    row(L"关闭后留在托盘", L"关掉主界面但继续计时", 5);
    Switch(c, w, ID_S_TRAY, W - 24 - 46, y + 5 * rowH + 15, a.trayOnClose);

    Button(c, w, ID_S_DONE, D2D1::RectF(24, H - 76, W - 24, H - 24), L"完成", 0, 15.f);
}

// ---------------------------------------------------------------- 渲染
static void EnsureRT(Win& w) {
    if (w.rt) return;
    RECT rc;
    GetClientRect(w.hwnd, &rc);
    D2D1_SIZE_U sz = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
    if (sz.width == 0 || sz.height == 0) return;
    ID2D1HwndRenderTarget* rt = nullptr;
    // 固定 96 DPI，DPI 缩放统一由 SetTransform 处理，避免二次放大
    D2D1_RENDER_TARGET_PROPERTIES props =
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(),
                                     96.f, 96.f);
    g_d2d->CreateHwndRenderTarget(props,
                                  D2D1::HwndRenderTargetProperties(w.hwnd, sz,
                                                                  D2D1_PRESENT_OPTIONS_NONE),
                                  &rt);
    if (rt) {
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        w.rt.p = rt;
    }
}

// ---------------------------------------------------------------- 坐一分钟（不用起身）
static void Inval(Win& w);

struct PlayRun {
    int kind = -1;
    float t = 0.f;
    // 反应类
    int hits = 0, fa = 0, early = 0, miss = 0;
    std::vector<float> rts;
    float isi = 0.4f, stimAge = 0.f;
    bool stimOn = false, go = true;
    float sx = 0.f, sy = 0.f, lastRT = -1.f;
    float flash = 0.f;
    int flashKind = 0;      // 1 对 / 2 错
    // 找出不一样
    int odd = -1, dir = 0, oddDir = 0, rounds = 0, wrong = 0;
    float roundT = 0.f, solvedT = 0.f;
    bool solved = false;
    std::vector<float> finds;
};
static PlayRun g_pr;

std::wstring PlaySummary(int kind) {
    wchar_t buf[192];
    if (kind == PLAY_REACT) {
        if (g_pr.rts.empty()) return L"反应力 · 这次没测到，下次再来";
        std::vector<float> v = g_pr.rts;
        std::sort(v.begin(), v.end());
        float med = v[v.size() / 2];
        swprintf(buf, 192, L"反应力 · %d 次，中位 %.0f ms", (int)v.size(), med);
        return buf;
    }
    if (kind == PLAY_GONOGO) {
        swprintf(buf, 192, L"绿点红点 · 按对 %d 次，误按 %d 次", g_pr.hits, g_pr.fa);
        return buf;
    }
    if (kind == PLAY_SEARCH) {
        float avg = 0.f;
        for (float f : g_pr.finds) avg += f;
        if (!g_pr.finds.empty()) avg /= (float)g_pr.finds.size();
        swprintf(buf, 192, L"找出不一样 · 找到 %d 个，平均 %.1f 秒", (int)g_pr.finds.size(),
                 g_pr.finds.empty() ? 0.f : avg);
        return buf;
    }
    return L"眼睛松了，看得更清";
}

static void PlaySearchGrid(float W, float H, float& x0, float& y0, float& cell, float& gap) {
    gap = 9.f;
    float top = 96.f, bot = H - 58.f;
    cell = (bot - top - 4 * gap) / 5.f;
    float total = cell * 5 + gap * 4;
    x0 = (W - total) / 2.f;
    y0 = top + ((bot - top) - total) / 2.f;
}

static void NewSearchRound() {
    g_pr.odd = rand() % 25;
    g_pr.dir = rand() % 4;
    int off = 1 + rand() % 3;
    g_pr.oddDir = (g_pr.dir + off) % 4;
    g_pr.roundT = 0.f;
    g_pr.solved = false;
    g_pr.solvedT = 0.f;
}

static void PaintPlayPick(Ctx& c, Win& w) {
    const float W = (float)w.W;
    c.txt(L"坐一分钟", D2D1::RectF(24, 20, 320, 48), 16.f, true, theme::INK, 0);
    c.txt(L"不用起身，眼睛和注意力也能歇一歇", D2D1::RectF(24, 50, W - 60, 72), 11.5f, false,
          theme::SUB, 0);
    c.iconBtn(w, ID_PL_CLOSE, D2D1::RectF(W - 40, 18, W - 16, 42), L"\uE711", 11);

    const float cw = 250.f, chh = 86.f, gx = 12.f, gy = 12.f, x0 = 24.f, y0 = 84.f;
    for (int k = 0; k < PLAY_KIND_N; k++) {
        const PlayDef& d = PlayDefOf(k);
        bool half = (k < 6);
        float x = half ? x0 + (k % 2) * (cw + gx) : x0;
        float y = y0 + (k / 2) * (chh + gy);
        float ww = half ? cw : (W - 48.f);
        D2D1_RECT_F r = D2D1::RectF(x, y, x + ww, y + chh);
        int id = ID_PL_CARD + k;
        float hv = SP(w, id).v;
        float pr = SP(w, 10000 + id, 1.f).v;
        D2D1_RECT_F rr = D2D1::RectF(r.left, r.top, r.right, r.bottom - (1.f - pr) * 2.f);
        c.fillR(D2D1::RectF(rr.left, rr.top + 1, rr.right, rr.bottom + 2), 18,
                withA(D2D1::ColorF(0x0F1620), 0.045f));
        c.fillR(rr, 18, mix(theme::CARD, D2D1::ColorF(0xEDF2F8), hv));
        c.fillR(D2D1::RectF(x + 14, y + 23, x + 54, y + 63), 20,
                mix(theme::GREEN_S, D2D1::ColorF(0xD8F2E4), hv));
        c.glyph(d.glyph, D2D1::RectF(x + 14, y + 23, x + 54, y + 63), 17, theme::GREEN);
        c.txt(d.name, D2D1::RectF(x + 66, y + 12, x + ww - 60, y + 38), 14.5f, true, theme::INK, 0);
        c.txt(d.sub, D2D1::RectF(x + 66, y + 38, x + ww - 20, y + 78), 11.f, false, theme::SUB, 0,
              true);
        c.txt(std::to_wstring(d.secs) + L" 秒",
              D2D1::RectF(x + ww - 72, y + 13, x + ww - 16, y + 33), 10.5f, false, theme::MUTED, 2);
        c.hot(id, r);
    }
    c.txt(L"这些练习坐着就能做完 · Esc 退出", D2D1::RectF(24, (float)w.H - 20, W - 24, (float)w.H - 2),
          10.5f, false, theme::MUTED, 1);
}

static void PaintPlayDone(Ctx& c, Win& w) {
    const float W = (float)w.W, H = (float)w.H;
    App& a = App::I();
    float cx = W / 2.f;
    float hv = SP(w, K_CHECK, 0.f).v;
    Spring& ks = SP(w, K_CHECK, 0.f);
    if (ks.target < 0.5f) {
        ks.snap(0.f);
        ks.k = 250.f;
        ks.c = 13.f;
        ks.to(1.f);
    }
    hv = ks.v;
    float cy = 124.f;
    c.circle(cx, cy, 46.f * hv, theme::GREEN_S);
    c.circle(cx, cy, 34.f * hv, theme::GREEN);
    c.glyph(L"\uE73E", D2D1::RectF(cx - 22, cy - 22, cx + 22, cy + 22), 22.f * hv, theme::WINDOW);
    c.txt(L"这一项完成", D2D1::RectF(24, cy + 56, W - 24, cy + 90), 19.f, true, theme::INK, 1);
    c.txt(PlaySummary(g_pr.kind), D2D1::RectF(40, cy + 94, W - 40, cy + 132), 12.5f, false,
          theme::SUB, 1, true);

    float py = cy + 146.f;
    c.fillR(D2D1::RectF(40, py, W - 40, py + 76), 16, theme::CARD);
    wchar_t l1[96], l2[96];
    swprintf(l1, 96, L"今天 眼部 %d 次 · 小游戏 %d 局", a.Today().eyes, a.Today().games);
    int left = a.intervalMin * 60 - a.work;
    if (left < 0) left = 0;
    swprintf(l2, 96, L"已坐 %d 分钟 · 距离下次提醒 %d 分钟", a.work / 60, (left + 59) / 60);
    c.txt(l1, D2D1::RectF(62, py + 16, W - 62, py + 40), 11.5f, false, theme::SUB, 0);
    c.txt(l2, D2D1::RectF(62, py + 42, W - 62, py + 64), 11.f, false, theme::MUTED, 0);

    float bw = 118.f, bh = 44.f, gap = 12.f;
    float bx = (W - (bw * 2 + gap)) / 2.f, by = H - 96.f;
    Button(c, w, ID_PL_AGAIN, D2D1::RectF(bx, by, bx + bw, by + bh), L"再来一局", 2, 14.f);
    Button(c, w, ID_PL_DONE, D2D1::RectF(bx + bw + gap, by, bx + bw * 2 + gap, by + bh),
           L"回到工作", 1, 14.f);
    c.txt(L"这些练习都不算离座，起身走动仍然是最好的休息", D2D1::RectF(24, H - 34, W - 24, H - 14),
          10.5f, false, theme::MUTED, 1);
}

static void PaintPlayRun(Ctx& c, Win& w) {
    const float W = (float)w.W, H = (float)w.H;
    App& a = App::I();
    const PlayDef& d = PlayDefOf(g_pr.kind);
    float top = 84.f, bot = H - 58.f;
    float cx = W / 2.f, cy = (top + bot) / 2.f;

    c.txt(d.name, D2D1::RectF(24, 18, 320, 46), 15.f, true, theme::INK, 0);
    std::wstring head = a.playDone ? std::wstring(L"已完成")
                                   : (L"剩余 " + std::to_wstring(a.playLeft) + L" 秒");
    c.txt(head, D2D1::RectF(W - 176, 20, W - 46, 44), 12.f, false, theme::MUTED, 2);
    c.iconBtn(w, ID_PL_CLOSE, D2D1::RectF(W - 40, 16, W - 16, 40), L"\uE711", 11);

    if (a.playDone) {
        PaintPlayDone(c, w);
        return;
    }

    switch (g_pr.kind) {
        case PLAY_EYE8: {
            const float ax = 200.f, ay = 94.f, T = 7.f;
            std::vector<D2D1_POINT_2F> path;
            for (int i = 0; i <= 120; i++) {
                float u = (float)i / 120.f * T;
                path.push_back(D2D1::Point2F(cx + ax * sinf(6.2831853f * u / T),
                                             cy + ay * sinf(12.566371f * u / T)));
            }
            c.poly(path, withA(theme::GREEN, 0.15f), 2.f);
            float px = cx + ax * sinf(6.2831853f * g_pr.t / T);
            float py = cy + ay * sinf(12.566371f * g_pr.t / T);
            c.circle(px, py, 19, withA(theme::GREEN, 0.12f));
            c.circle(px, py, 10, theme::GREEN);
            c.circle(px, py, 3.2f, D2D1::ColorF(0xFFFFFF));
            c.txt(L"头别动，眼睛跟着圆点走一遍横 8", D2D1::RectF(24, H - 58, W - 24, H - 34), 11.5f,
                  false, theme::SUB, 1);
            break;
        }
        case PLAY_FAR: {
            D2D1_POINT_2F cc = D2D1::Point2F(cx, cy - 8);
            c.arc(cc, 76, 10.f, 359.6f, theme::TRACK);
            float f = a.playTotal ? 1.f - (float)a.playLeft / (float)a.playTotal : 0.f;
            if (f > 0.004f) c.arc(cc, 76, 10.f, 359.6f * f, theme::GREEN);
            c.txt(std::to_wstring(a.playLeft),
                  D2D1::RectF(cx - 60, cy - 48, cx + 60, cy + 4), 40.f, true, theme::INK, 1);
            c.txt(L"秒", D2D1::RectF(cx - 40, cy + 8, cx + 40, cy + 30), 11.5f, false, theme::MUTED,
                  1);
            c.txt(L"看向 6 米外的一个静物", D2D1::RectF(24, H - 112, W - 24, H - 86), 14.f, true,
                  theme::INK2, 1);
            c.txt(L"找一样不会动的东西，一直看到它变清楚", D2D1::RectF(24, H - 84, W - 24, H - 62),
                  11.f, false, theme::SUB, 1);
            break;
        }
        case PLAY_BLINK: {
            float ph = fmodf(g_pr.t, 4.f);
            float open;
            if (ph < 0.4f) open = 1.f - ph / 0.4f;
            else if (ph < 2.f) open = 0.06f;
            else if (ph < 2.4f) open = 0.06f + (ph - 2.f) / 0.4f;
            else open = 1.f;
            bool closed = ph < 2.f;
            float ey = cy - 10.f, er = 98.f, h = 50.f * open;
            std::vector<D2D1_POINT_2F> up, lo;
            for (int i = 0; i <= 34; i++) {
                float u = -1.f + 2.f * (float)i / 34.f;
                float k = powf(1.f - u * u, 0.55f);
                up.push_back(D2D1::Point2F(cx + er * u, ey - h * k));
                lo.push_back(D2D1::Point2F(cx + er * u, ey + h * k));
            }
            c.ellipse(cx, ey, er * 0.97f, h * 0.9f, withA(theme::GREEN_S, 0.9f));
            c.circle(cx, ey, 21.f * open, withA(theme::GREEN, 0.45f));
            c.circle(cx, ey, 9.f * open, withA(theme::GREEN_D, 0.85f));
            c.poly(up, withA(theme::GREEN, 0.85f), 4.5f);
            c.poly(lo, withA(theme::GREEN, 0.85f), 4.5f);
            c.txt(closed ? L"轻闭眼，保持两秒" : L"慢慢睁开，别用力",
                  D2D1::RectF(24, H - 116, W - 24, H - 88), 15.f, true, theme::INK2, 1);
            c.txt(L"眨眼是把泪膜重新铺一遍，用屏时人平均每分钟只眨 5 到 7 次",
                  D2D1::RectF(24, H - 84, W - 24, H - 62), 11.f, false, theme::SUB, 1);
            break;
        }
        case PLAY_BREATH: {
            float ph = fmodf(g_pr.t, 10.f);
            float k = ph < 4.f ? ph / 4.f : 1.f - (ph - 4.f) / 6.f;
            if (k < 0) k = 0;
            k = k * k * (3.f - 2.f * k);
            float r = 56.f + 62.f * k;
            c.arc(D2D1::Point2F(cx, cy), 124, 2.4f, 359.6f, theme::TRACK);
            float cyc = ph / 10.f;
            if (cyc > 0.004f)
                c.arc(D2D1::Point2F(cx, cy), 124, 2.4f, 359.6f * cyc, withA(theme::GREEN, 0.6f));
            c.circle(cx, cy, r + 12, withA(theme::GREEN, 0.09f));
            c.circle(cx, cy, r, withA(theme::GREEN, 0.2f));
            c.txt(ph < 4.f ? L"吸气" : L"呼气", D2D1::RectF(cx - 80, cy - 24, cx + 80, cy + 16),
                  20.f, true, theme::GREEN_D, 1);
            wchar_t sb[32];
            float phaseSec = ph < 4.f ? 4.f - ph : 10.f - ph;
            swprintf(sb, 32, L"%d 秒", (int)ceilf(phaseSec - 0.06f));
            c.txt(sb, D2D1::RectF(cx - 40, cy + 18, cx + 40, cy + 40), 11.f, false, theme::MUTED, 1);
            c.txt(L"肩膀放下来，别耸肩", D2D1::RectF(24, H - 78, W - 24, H - 58), 11.5f, false,
                  theme::SUB, 1);
            break;
        }
        case PLAY_REACT:
        case PLAY_GONOGO: {
            c.arc(D2D1::Point2F(cx, cy), 54, 1.f, 359.6f, withA(theme::LINE, 0.85f));
            c.arc(D2D1::Point2F(cx, cy), 104, 1.f, 359.6f, withA(theme::LINE, 0.6f));
            c.arc(D2D1::Point2F(cx, cy), 154, 1.f, 359.6f, withA(theme::LINE, 0.42f));
            c.fillR(D2D1::RectF(cx - 13, cy - 1, cx + 13, cy + 1), 1, withA(theme::LINE, 1.f));
            c.fillR(D2D1::RectF(cx - 1, cy - 13, cx + 1, cy + 13), 1, withA(theme::LINE, 1.f));
            if (g_pr.stimOn) {
                D2D1_COLOR_F col = g_pr.go ? theme::GREEN : D2D1::ColorF(0xE5484D);
                c.circle(g_pr.sx, g_pr.sy, 30, withA(col, 0.13f));
                c.circle(g_pr.sx, g_pr.sy, 17, col);
            }
            if (g_pr.flash > 0.f) {
                float fade = g_pr.flash / 0.35f;
                D2D1_COLOR_F col = g_pr.flashKind == 1 ? theme::GREEN : D2D1::ColorF(0xE5484D);
                c.circle(cx, cy, 18 + (1.f - fade) * 8, withA(col, 0.22f * fade));
            }
            std::wstring st;
            if (g_pr.kind == PLAY_REACT) {
                st = L"按对 " + std::to_wstring(g_pr.hits);
                if (g_pr.lastRT > 0) st += L" · 最近 " + std::to_wstring((int)g_pr.lastRT) + L" ms";
            } else {
                st = L"按对 " + std::to_wstring(g_pr.hits) + L" · 误按 " + std::to_wstring(g_pr.fa);
            }
            if (g_pr.wrong || g_pr.early) st += L" · 抢按 " + std::to_wstring(g_pr.early);
            c.txt(st, D2D1::RectF(24, H - 84, W - 24, H - 60), 13.f, true, theme::INK2, 1);
            c.txt(g_pr.kind == PLAY_REACT ? L"看到圆点立刻按空格，或者直接点它"
                                          : L"绿点按空格，红点忍住不按",
                  D2D1::RectF(24, H - 58, W - 24, H - 40), 11.f, false, theme::SUB, 1);
            break;
        }
        case PLAY_SEARCH: {
            float gx0, gy0, cell, gap;
            PlaySearchGrid(W, H, gx0, gy0, cell, gap);
            for (int i = 0; i < 25; i++) {
                int rw = i / 5, cl = i % 5;
                float x = gx0 + cl * (cell + gap);
                float y = gy0 + rw * (cell + gap);
                c.fillR(D2D1::RectF(x, y, x + cell, y + cell), 16, theme::CARD);
                int dir = (i == g_pr.odd) ? g_pr.oddDir : g_pr.dir;
                float ang = (float)(dir * 90 + 45) * 3.14159265f / 180.f;
                float mx = x + cell * 0.5f + cosf(ang) * cell * 0.16f;
                float my = y + cell * 0.5f + sinf(ang) * cell * 0.16f;
                float s = cell * 0.135f;
                float ux = cosf(ang), uy = sinf(ang);
                float vx = -uy, vy = ux;
                c.tri(D2D1::Point2F(mx + ux * s, my + uy * s),
                      D2D1::Point2F(mx - ux * s * 0.5f + vx * s * 0.9f,
                                    my - uy * s * 0.5f + vy * s * 0.9f),
                      D2D1::Point2F(mx - ux * s * 0.5f - vx * s * 0.9f,
                                    my - uy * s * 0.5f - vy * s * 0.9f),
                      withA(theme::INK, 0.62f));
            }
            std::wstring st = L"找到 " + std::to_wstring(g_pr.rounds) + L" 个";
            if (!g_pr.finds.empty()) {
                float avg = 0.f;
                for (float f : g_pr.finds) avg += f;
                avg /= (float)g_pr.finds.size();
                wchar_t buf[64];
                swprintf(buf, 64, L" · 平均 %.1f 秒", avg);
                st += buf;
            }
            c.txt(st, D2D1::RectF(24, 52, W - 60, 76), 11.5f, false, theme::MUTED, 0);
            c.txt(L"点一下方向不一样的那一个", D2D1::RectF(24, H - 50, W - 24, H - 30), 10.5f,
                  false, theme::MUTED, 1);
            break;
        }
    }

    // 底部进度
    float frac = a.playTotal ? 1.f - (float)a.playLeft / (float)a.playTotal : 0.f;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    float bx0 = 24.f, bx1 = W - 24.f, by = H - 26.f;
    c.fillR(D2D1::RectF(bx0, by, bx1, by + 4), 2, theme::TRACK);
    if (frac > 0.002f)
        c.fillR(D2D1::RectF(bx0, by, bx0 + (bx1 - bx0) * frac, by + 4), 2, theme::GREEN);
}

static void PaintPlay(Ctx& c, Win& w) {
    c.fillRect(D2D1::RectF(0, 0, (float)w.W, (float)w.H), theme::WINDOW);
    if (g_pr.kind < 0) PaintPlayPick(c, w);
    else PaintPlayRun(c, w);
}

// 游戏/动画一帧；返回是否还需要下一帧
static bool PlayFrame(float dt) {
    if (!g_play.hwnd || !IsWindowVisible(g_play.hwnd)) return false;
    if (g_pr.kind < 0) return false;
    App& a = App::I();
    if (a.state != ST_PLAY) return false;
    if (a.playDone) return false;
    g_pr.t += dt;
    if (g_pr.flash > 0.f) g_pr.flash -= dt;

    if (g_pr.kind == PLAY_REACT || g_pr.kind == PLAY_GONOGO) {
        float top = 84.f, bot = (float)g_play.H - 58.f;
        float cx = g_play.W / 2.f, cy = (top + bot) / 2.f;
        if (g_pr.stimOn) {
            g_pr.stimAge += dt;
            if (g_pr.stimAge > 1.05f) {
                if (g_pr.go) g_pr.miss++;
                g_pr.stimOn = false;
                g_pr.isi = 0.7f + (float)(rand() % 130) / 100.f;
            }
        } else {
            g_pr.isi -= dt;
            if (g_pr.isi <= 0.f) {
                g_pr.stimOn = true;
                g_pr.stimAge = 0.f;
                g_pr.go = (g_pr.kind == PLAY_REACT) ? true : ((rand() % 100) < 75);
                float ang = (float)(rand() % 360) * 3.14159265f / 180.f;
                float rr = 92.f + (float)(rand() % 96);
                g_pr.sx = cx + cosf(ang) * rr;
                g_pr.sy = cy + sinf(ang) * rr * 0.7f;
            }
        }
        Inval(g_play);
        return true;
    }
    if (g_pr.kind == PLAY_SEARCH) {
        if (g_pr.odd < 0) {
            NewSearchRound();
        } else if (g_pr.solved) {
            g_pr.solvedT += dt;
            if (g_pr.solvedT > 0.4f) NewSearchRound();
        } else {
            g_pr.roundT += dt;
            if (g_pr.roundT > 12.f) NewSearchRound();
        }
        Inval(g_play);
        return true;
    }
    Inval(g_play);
    return true;
}

static void PlayHit(float x, float y) {
    if (App::I().playDone) return;
    if (g_pr.kind == PLAY_REACT || g_pr.kind == PLAY_GONOGO) {
        if (!g_pr.stimOn) {
            if (g_pr.t > 0.25f) {
                g_pr.early++;
                g_pr.flash = 0.35f;
                g_pr.flashKind = 2;
            }
            return;
        }
        float rt = g_pr.stimAge * 1000.f;
        g_pr.stimOn = false;
        g_pr.isi = 0.7f + (float)(rand() % 130) / 100.f;
        if (rt < 100.f) {
            g_pr.early++;
            g_pr.flash = 0.35f;
            g_pr.flashKind = 2;
            return;
        }
        g_pr.flash = 0.35f;
        if (g_pr.go) {
            g_pr.hits++;
            g_pr.rts.push_back(rt);
            g_pr.lastRT = rt;
            g_pr.flashKind = 1;
        } else {
            g_pr.fa++;
            g_pr.flashKind = 2;
        }
        return;
    }
    if (g_pr.kind == PLAY_SEARCH && g_pr.odd >= 0 && !g_pr.solved) {
        float gx0, gy0, cell, gap;
        PlaySearchGrid((float)g_play.W, (float)g_play.H, gx0, gy0, cell, gap);
        int cl = (int)((x - gx0) / (cell + gap));
        int rw = (int)((y - gy0) / (cell + gap));
        if (cl < 0 || cl > 4 || rw < 0 || rw > 4) return;
        float lx = x - (gx0 + cl * (cell + gap));
        float ly = y - (gy0 + rw * (cell + gap));
        if (lx > cell || ly > cell) return;
        if (rw * 5 + cl == g_pr.odd) {
            g_pr.finds.push_back(g_pr.roundT);
            g_pr.rounds++;
            g_pr.solved = true;
            g_pr.solvedT = 0.f;
        } else {
            g_pr.wrong++;
        }
    }
}

static void PaintWin(Win& w) {
    EnsureRT(w);
    if (!w.rt) return;
    w.hots.clear();

    RECT rc;
    GetClientRect(w.hwnd, &rc);
    w.rt->Resize(D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));

    Ctx c;
    c.rt = w.rt.get();
    c.w = &w;

    w.rt->BeginDraw();
    w.rt->SetTransform(D2D1::Matrix3x2F::Scale(g_scale, g_scale));

    if (w.hwnd == g_main.hwnd) {
        PaintMain(c, w);
    } else if (w.hwnd == g_prompt.hwnd) {
        PaintPrompt(c, w);
    } else if (w.hwnd == g_train.hwnd) {
        PaintTrain(c, w);
    } else if (w.hwnd == g_set.hwnd) {
        PaintSettings(c, w);
    } else if (w.hwnd == g_play.hwnd) {
        PaintPlay(c, w);
    } else if (w.hwnd == g_toast.hwnd) {
        c.fillRect(D2D1::RectF(0, 0, (float)w.W, (float)w.H), theme::DARKBAR);
        c.fillR(D2D1::RectF(18, w.H / 2.f - 3, 24, w.H / 2.f + 3), 3, theme::GREEN_L);
        c.txt(g_toastText, D2D1::RectF(34, 0, (float)w.W - 14, (float)w.H), 12.5f, false,
              D2D1::ColorF(0xFFFFFF), 0);
    }

    HRESULT hr = w.rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        w.rt.reset();
        InvalidateRect(w.hwnd, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------- 命中测试
static int HitTest(Win& w, POINT pt) {
    ScreenToClient(w.hwnd, &pt);
    float x = (float)pt.x / g_scale, y = (float)pt.y / g_scale;
    for (int i = (int)w.hots.size() - 1; i >= 0; i--) {
        const Hot& h = w.hots[i];
        if (x >= h.r.left && x <= h.r.right && y >= h.r.top && y <= h.r.bottom) return h.id;
    }
    return 0;
}

// ---------------------------------------------------------------- 动画驱动
static void Inval(Win& w) {
    if (w.hwnd && IsWindowVisible(w.hwnd)) InvalidateRect(w.hwnd, nullptr, FALSE);
}

static bool StepAnims(float dt) {
    bool any = false;
    Win* all[6] = {&g_main, &g_prompt, &g_train, &g_set, &g_toast, &g_play};
    for (Win* w : all) {
        if (!w->hwnd) continue;
        bool moved = false;
        for (auto& kv : w->sp) {
            if (kv.second.update(dt)) moved = true;
        }
        if (moved) {
            any = true;
            Inval(*w);
        }
    }
    return any;
}

static void EnsureAnimTimer(bool on) {
    if (on && !g_animTimer) {
        SetTimer(g_main.hwnd, 2, 16, nullptr);
        g_animTimer = true;
    } else if (!on && g_animTimer) {
        KillTimer(g_main.hwnd, 2);
        g_animTimer = false;
    }
}

// ---------------------------------------------------------------- 窗口辅助
static void SetRounded(HWND h) {
    int pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    COLORREF none = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(h, DWMWA_BORDER_COLOR, &none, sizeof(none));
}

static RECT WorkArea() {
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    return wa;
}

// 把窗口拉到前台（跨进程前台权限限制时借用前台线程输入队列）
static void ForceForeground(HWND h) {
    if (!h) return;
    HWND fg = GetForegroundWindow();
    DWORD tidFg = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD tidMe = GetCurrentThreadId();
    bool attached = false;
    if (tidFg && tidFg != tidMe) {
        attached = AttachThreadInput(tidMe, tidFg, TRUE) != FALSE;
    }
    BringWindowToTop(h);
    SetForegroundWindow(h);
    SetActiveWindow(h);
    if (attached) AttachThreadInput(tidMe, tidFg, FALSE);
    FlashWindow(h, FALSE);
}

// ---------------------------------------------------------------- 窗口过程
static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Win* w = nullptr;
    if (h == g_main.hwnd) w = &g_main;
    else if (h == g_prompt.hwnd) w = &g_prompt;
    else if (h == g_train.hwnd) w = &g_train;
    else if (h == g_set.hwnd) w = &g_set;
    else if (h == g_play.hwnd) w = &g_play;
    else if (h == g_toast.hwnd) w = &g_toast;

    switch (msg) {
        case WM_CREATE:
            SetRounded(h);
            return 0;

        case WM_TIMER:
            if (wp == 1) {
                App& a = App::I();
                if (a.state == ST_TRAINING) a.TrainTick();
                else a.Tick();
            } else if (wp == 2) {
                bool need = StepAnims(0.016f);
                if (PlayFrame(0.016f)) need = true;
                if (!need) EnsureAnimTimer(false);
                if (g_prompt.hwnd && IsWindowVisible(g_prompt.hwnd)) {
                    float k = SP(g_prompt, K_APPEAR).v;
                    int yoff = (int)((1.f - k) * 26 * g_scale);
                    SetWindowPos(g_prompt.hwnd, nullptr, g_promptX, g_promptY + yoff, 0, 0,
                                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
                }
                if (g_toast.hwnd && IsWindowVisible(g_toast.hwnd)) {
                    float k = SP(g_toast, K_APPEAR).v;
                    int yoff = (int)((1.f - k) * 16 * g_scale);
                    SetWindowPos(g_toast.hwnd, nullptr, g_toastX, g_toastY + yoff, 0, 0,
                                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
                }
            } else if (wp == 9) {
                KillTimer(h, 9);
                ShowWindow(g_toast.hwnd, SW_HIDE);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            if (w) PaintWin(*w);
            EndPaint(h, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEMOVE: {
            if (!w) break;
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(h, &pt);
            int id = HitTest(*w, pt);
            if (id != w->hover) {
                SP(*w, w->hover, 0.f).to(0.f);
                SP(*w, id, 0.f).to(1.f);
                w->hover = id;
                EnsureAnimTimer(true);
                Inval(*w);
            }
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = h;
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE:
            if (w && w->hover) {
                SP(*w, w->hover, 0.f).to(0.f);
                w->hover = 0;
                EnsureAnimTimer(true);
                Inval(*w);
            }
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, (w && w->hover) ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_LBUTTONDOWN: {
            if (!w) break;
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            POINT sp = pt;
            ClientToScreen(h, &sp);
            int id = HitTest(*w, sp);
            if (!id && h == g_play.hwnd && g_pr.kind >= 0) {
                PlayHit((float)pt.x / g_scale, (float)pt.y / g_scale);
                Inval(g_play);
                return 0;
            }
            if (id) {
                w->press = id;
                Spring& s = SP(*w, 10000 + id, 1.f);
                s.k = 460.f;
                s.c = 30.f;
                s.to(0.955f);
                EnsureAnimTimer(true);
                Inval(*w);
            } else {
                ReleaseCapture();
                SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (!w) break;
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            POINT sp = pt;
            ClientToScreen(h, &sp);
            int id = HitTest(*w, sp);
            int pressed = w->press;
            w->press = 0;
            if (pressed) {
                SP(*w, 10000 + pressed, 1.f).to(1.f);
                EnsureAnimTimer(true);
                Inval(*w);
            }
            if (id && id == pressed) {
                App& a = App::I();
                if (h == g_main.hwnd) {
                    switch (id) {
                        case ID_SETTINGS: UI_Settings(true); break;
                        case ID_MIN: ShowWindow(h, SW_MINIMIZE); break;
                        case ID_CLOSE: PostMessageW(h, WM_CLOSE, 0, 0); break;
                        case ID_ACTION: a.BreakNow(); break;
                        case ID_PLAY: a.OpenPlayList(); break;
                        case ID_SNOOZE: a.PromptSnooze(5); break;
                        case ID_PAUSE: a.TogglePause(); break;
                    }
                } else if (h == g_prompt.hwnd) {
                    switch (id) {
                        case ID_P_CLOSE: a.PromptClose(); break;
                        case ID_P_START: a.PromptStart(); break;
                        case ID_P_LATER: a.PromptSnooze(5); break;
                        case ID_P_SIT: a.StartPlay(rand() % 4); break;
                    }
                } else if (h == g_train.hwnd) {
                    switch (id) {
                        case ID_T_CLOSE:
                        case ID_T_BACK: a.TrainBack(); break;
                        case ID_T_PAUSE:
                            a.trainPaused = !a.trainPaused;
                            UI_Refresh(true);
                            break;
                        case ID_T_NEXT: a.TrainNext(); break;
                    }
                } else if (h == g_play.hwnd) {
                    if (id == ID_PL_CLOSE) {
                        a.ClosePlay();
                    } else if (id == ID_PL_AGAIN) {
                        a.StartPlay(g_pr.kind);
                    } else if (id == ID_PL_DONE) {
                        a.ClosePlay();
                    } else if (id >= ID_PL_CARD && id < ID_PL_CARD + PLAY_KIND_N) {
                        a.StartPlay(id - ID_PL_CARD);
                    }
                } else if (h == g_set.hwnd) {
                    switch (id) {
                        case ID_S_IV_DN:
                            a.intervalMin = a.intervalMin > 5 ? a.intervalMin - 5 : 5;
                            a.SaveCfg(); UI_Refresh(true); break;
                        case ID_S_IV_UP:
                            a.intervalMin = a.intervalMin < 120 ? a.intervalMin + 5 : 120;
                            a.SaveCfg(); UI_Refresh(true); break;
                        case ID_S_ID_DN:
                            a.idleMin = a.idleMin > 2 ? a.idleMin - 1 : 2;
                            a.SaveCfg(); UI_Refresh(true); break;
                        case ID_S_ID_UP:
                            a.idleMin = a.idleMin < 20 ? a.idleMin + 1 : 20;
                            a.SaveCfg(); UI_Refresh(true); break;
                        case ID_S_SOUND: a.sound = !a.sound; a.SaveCfg(); UI_Refresh(true); break;
                        case ID_S_AUTO: a.SetAutostart(!a.autostart); UI_Refresh(true); break;
                        case ID_S_TOP:
                            a.topmostPrompt = !a.topmostPrompt;
                            a.SaveCfg(); UI_ApplyTopmost(); UI_Refresh(true); break;
                        case ID_S_TRAY:
                            a.trayOnClose = !a.trayOnClose; a.SaveCfg(); UI_Refresh(true); break;
                        case ID_S_DONE:
                        case ID_S_X: UI_Settings(false); break;
                    }
                }
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (h == g_play.hwnd) {
                if (wp == VK_SPACE) {
                    PlayHit(-1.f, -1.f);
                    Inval(g_play);
                    return 0;
                }
                if (wp == VK_ESCAPE) {
                    App::I().ClosePlay();
                    return 0;
                }
            }
            break;

        case WM_SYSCOMMAND: {
            UINT cmd = (UINT)(wp & 0xFFF0);
            if (cmd == SC_RESTORE) AppLog(L"sc restore");
            if (cmd == SC_MINIMIZE) AppLog(L"sc minimize");
            if (cmd == SC_RESTORE || cmd == SC_MINIMIZE) {
                LRESULT r = DefWindowProcW(h, msg, wp, lp);   // 系统负责最小化/还原
                if (cmd == SC_RESTORE && h == g_main.hwnd) {
                    ForceForeground(h);
                    UI_Refresh(true);
                }
                return r;
            }
            if (cmd == SC_CLOSE) return SendMessageW(h, WM_CLOSE, 0, 0);
            break;
        }

        case WM_ACTIVATE:
            AppLog(L"activate");
            if (LOWORD(wp) != WA_INACTIVE && IsIconic(h)) ShowWindow(h, SW_RESTORE);
            break;

        case WM_CLOSE:
            if (h == g_play.hwnd) {
                App::I().ClosePlay();
                return 0;
            }
            if (h == g_main.hwnd) {
                App& a = App::I();
                if (a.trayOnClose) {
                    // 最小化而不是隐藏：任务栏按钮保留，用户点一下就能回来
                    ShowWindow(h, SW_MINIMIZE);
                    a.TrayTip(L"在后台计时 · 右键托盘可退出");
                    return 0;
                }
                a.SaveAll();
                a.Shutdown();
                PostQuitMessage(0);
                return 0;
            }
            ShowWindow(h, SW_HIDE);
            return 0;

        case WM_TRAY: {
            // NOTIFYICON_VERSION_4 下：左键来 NIN_SELECT / NIN_KEYSELECT，右键来 WM_CONTEXTMENU，
            // 老版本则是 WM_LBUTTONUP / WM_RBUTTONUP。两套都接住，托盘才真的点得动。
            UINT ev = (UINT)lp;
            if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU || ev == WM_RBUTTONDOWN) {
                AppLog(L"tray menu");
                App::I().TrayMenu();
            } else if (ev == NIN_SELECT || ev == NIN_KEYSELECT ||
                       ev == WM_LBUTTONUP || ev == WM_LBUTTONDBLCLK) {
                AppLog(L"tray click");
                UI_ShowMain(true);
            }
            return 0;
        }

        case WM_HOTKEY:
            AppLog(L"hotkey");
            UI_ShowMain(true);
            return 0;

        case WM_BREAKNOW:
            App::I().BreakNow();
            return 0;

        case WM_TOGGLEPAUSE:
            App::I().TogglePause();
            return 0;

        case WM_SHOWMAIN:
            AppLog(L"show main");
            UI_ShowMain(true);
            return 0;

        case WM_QUITAPP:            // 外部（SitBreak.exe --quit）要求退出
            App::I().SaveAll();
            DestroyWindow(h);
            PostQuitMessage(0);
            return 0;

        case WM_QUERYENDSESSION:
            App::I().SaveAll();
            return TRUE;

        case WM_ENDSESSION:
            if (wp) App::I().Shutdown();
            break;

        case WM_DPICHANGED: {
            g_dpi = HIWORD(wp);
            g_scale = g_dpi / 96.f;
            RECT* nr = (RECT*)lp;
            SetWindowPos(h, nullptr, nr->left, nr->top, nr->right - nr->left,
                         nr->bottom - nr->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (w && w->rt) w->rt.reset();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
    }
    if (g_msgTaskbarCreated && msg == g_msgTaskbarCreated) {
        App::I().TrayInit();    // explorer 重启后重建托盘图标
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------- UI 接口
void UI_Refresh(bool all) {
    Inval(g_main);
    if (all) {
        Inval(g_prompt);
        Inval(g_train);
        Inval(g_set);
    }
}

bool UI_MainVisible() {
    return g_main.hwnd && IsWindowVisible(g_main.hwnd);
}

void UI_ShowMain(bool show, bool animate) {
    (void)animate;
    if (!g_main.hwnd) return;
    if (show) {
        RECT wa = WorkArea();
        if (!IsWindowVisible(g_main.hwnd) || IsIconic(g_main.hwnd)) {
            int x = wa.left + (wa.right - wa.left - (int)(g_main.W * g_scale)) / 2;
            int y = wa.top + (wa.bottom - wa.top - (int)(g_main.H * g_scale)) / 2;
            SetWindowPos(g_main.hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
        }
        if (IsIconic(g_main.hwnd)) ShowWindow(g_main.hwnd, SW_RESTORE);
        else ShowWindow(g_main.hwnd, SW_SHOWNORMAL);
        SetWindowPos(g_main.hwnd, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        ForceForeground(g_main.hwnd);
        UI_Refresh(true);
    } else {
        ShowWindow(g_main.hwnd, SW_HIDE);
    }
}

void UI_Prompt(bool show) {
    if (!g_prompt.hwnd) return;
    App& a = App::I();
    if (show) {
        RECT wa = WorkArea();
        int wpx = (int)(g_prompt.W * g_scale);
        int hpx = (int)(g_prompt.H * g_scale);
        int x = wa.right - wpx - (int)(16 * g_scale);
        int y = wa.bottom - hpx - (int)(16 * g_scale);
        g_promptX = x;
        g_promptY = y;
        SetWindowPos(g_prompt.hwnd, a.topmostPrompt ? HWND_TOPMOST : HWND_TOP, x,
                     y + (int)(26 * g_scale), wpx, hpx, SWP_NOACTIVATE);
        Spring& s = SP(g_prompt, K_APPEAR, 0.f);
        s.snap(0.f);
        s.k = 200.f;
        s.c = 16.f;
        s.to(1.f);
        ShowWindow(g_prompt.hwnd, SW_SHOWNOACTIVATE);
        EnsureAnimTimer(true);
        UI_Refresh(true);
    } else {
        ShowWindow(g_prompt.hwnd, SW_HIDE);
    }
}

void UI_Train(bool show) {
    if (!g_train.hwnd) return;
    if (show) {
        RECT wa = WorkArea();
        int wpx = (int)(g_train.W * g_scale);
        int hpx = (int)(g_train.H * g_scale);
        int x = wa.left + (wa.right - wa.left - wpx) / 2;
        int y = wa.top + (wa.bottom - wa.top - hpx) / 2;
        SetWindowPos(g_train.hwnd, HWND_TOPMOST, x, y, wpx, hpx, SWP_NOACTIVATE);
        SP(g_train, K_APPEAR, 0.f).snap(0.f);
        SP(g_train, K_APPEAR).to(1.f);
        ShowWindow(g_train.hwnd, SW_SHOW);
        SetForegroundWindow(g_train.hwnd);
        EnsureAnimTimer(true);
        UI_Refresh(true);
    } else {
        ShowWindow(g_train.hwnd, SW_HIDE);
    }
}

// 让动画定时器跑一轮（完成态勾选弹跳等）
void UI_Pulse() {
    EnsureAnimTimer(true);
    UI_Refresh(true);
}

// 列出所有「坐一分钟」项目
void UI_PlayPick() {
    if (!g_play.hwnd) return;
    g_pr = PlayRun();
    g_pr.kind = -1;
    srand(GetTickCount());
    RECT wa = WorkArea();
    int wpx = (int)(g_play.W * g_scale), hpx = (int)(g_play.H * g_scale);
    int x = wa.left + (wa.right - wa.left - wpx) / 2;
    int y = wa.top + (wa.bottom - wa.top - hpx) / 2;
    SetWindowPos(g_play.hwnd, HWND_TOPMOST, x, y, wpx, hpx, SWP_NOACTIVATE);
    ShowWindow(g_play.hwnd, SW_SHOW);
    ForceForeground(g_play.hwnd);
    EnsureAnimTimer(true);
    Inval(g_play);
}

void UI_Play(bool show) {
    if (!g_play.hwnd) return;
    App& a = App::I();
    if (show) {
        g_pr = PlayRun();
        g_pr.kind = a.playKind;
        RECT wa = WorkArea();
        int wpx = (int)(g_play.W * g_scale), hpx = (int)(g_play.H * g_scale);
        if (!IsWindowVisible(g_play.hwnd)) {
            int x = wa.left + (wa.right - wa.left - wpx) / 2;
            int y = wa.top + (wa.bottom - wa.top - hpx) / 2;
            SetWindowPos(g_play.hwnd, HWND_TOPMOST, x, y, wpx, hpx, SWP_NOACTIVATE);
            ShowWindow(g_play.hwnd, SW_SHOW);
        }
        ForceForeground(g_play.hwnd);
        EnsureAnimTimer(true);
        Inval(g_play);
    } else {
        g_pr.kind = -1;
        ShowWindow(g_play.hwnd, SW_HIDE);
        Inval(g_main);
    }
}

void UI_Settings(bool show) {
    if (!g_set.hwnd) return;
    if (show) {
        RECT wa = WorkArea();
        int wpx = (int)(g_set.W * g_scale);
        int hpx = (int)(g_set.H * g_scale);
        int x = wa.left + (wa.right - wa.left - wpx) / 2;
        int y = wa.top + (wa.bottom - wa.top - hpx) / 2;
        SetWindowPos(g_set.hwnd, HWND_TOP, x, y, wpx, hpx, SWP_NOACTIVATE);
        ShowWindow(g_set.hwnd, SW_SHOW);
        SetForegroundWindow(g_set.hwnd);
        UI_Refresh(true);
    } else {
        ShowWindow(g_set.hwnd, SW_HIDE);
        App::I().SaveCfg();
    }
}

void UI_Toast(const std::wstring& s) {
    if (!g_toast.hwnd) return;
    g_toastText = s;
    RECT wa = WorkArea();
    int wpx = (int)(g_toast.W * g_scale), hpx = (int)(g_toast.H * g_scale);
    int x = wa.left + (wa.right - wa.left - wpx) / 2;
    int y = wa.bottom - hpx - (int)(28 * g_scale);
    g_toastX = x;
    g_toastY = y;
    SetWindowPos(g_toast.hwnd, HWND_TOPMOST, x, y + (int)(16 * g_scale), wpx, hpx,
                 SWP_NOACTIVATE);
    Spring& sp = SP(g_toast, K_APPEAR, 0.f);
    sp.snap(0.f);
    sp.k = 230.f;
    sp.c = 18.f;
    sp.to(1.f);
    EnsureAnimTimer(true);
    ShowWindow(g_toast.hwnd, SW_SHOWNOACTIVATE);
    Inval(g_toast);
    SetTimer(g_toast.hwnd, 9, 2200, nullptr);
}

void UI_ApplyTopmost() { UI_Refresh(true); }

// ---------------------------------------------------------------- 入口
static void InitD2D() {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g_d2d.put());
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        (IUnknown**)g_dw.put());
}

static void CreateWin(Win& w, int W, int H, DWORD exStyle, LPCWSTR title) {
    w.W = W;
    w.H = H;
    w.hwnd = CreateWindowExW(exStyle, L"SitBreakWin", title, WS_POPUP, 0, 0,
                             (int)(W * g_scale), (int)(H * g_scale), nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
}

// 任务栏按钮右键 → 跳转列表。把常用动作列进去，右键才有反应。
static void SetupJumpList() {
    wchar_t exe[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));

    Com<ICustomDestinationList> jump;
    if (FAILED(CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(jump.put())))) {
        AppLog(L"jumplist: no destlist");
        return;
    }
    UINT slots = 0;
    Com<IObjectArray> removed;
    if (FAILED(jump->BeginList(&slots, IID_PPV_ARGS(removed.put())))) {
        AppLog(L"jumplist: begin failed");
        return;
    }
    Com<IObjectCollection> list;
    if (FAILED(CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(list.put())))) {
        AppLog(L"jumplist: no collection");
        return;
    }

    struct Item { const wchar_t* name; const wchar_t* arg; };
    const Item items[] = {
        { L"显示主界面", L"--show" },
        { L"现在起身", L"--break" },
        { L"暂停 / 继续提醒", L"--pause" },
        { L"退出 SitBreak", L"--quit" },
    };
    for (const Item& it : items) {
        Com<IShellLinkW> link;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(link.put())))) continue;
        link->SetPath(exe);
        link->SetArguments(it.arg);
        link->SetIconLocation(exe, 0);
        link->SetDescription(it.name);
        Com<IPropertyStore> ps;
        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(ps.put())))) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            pv.vt = VT_LPWSTR;
            pv.pwszVal = const_cast<wchar_t*>(it.name);
            ps->SetValue(PKEY_Title, pv);
            ps->Commit();
        }
        list->AddObject(link.get());
    }
    if (FAILED(jump->AddUserTasks(list.get()))) {
        AppLog(L"jumplist: add failed");
        return;
    }
    if (FAILED(jump->CommitList())) {
        AppLog(L"jumplist: commit failed");
        return;
    }
    AppLog(L"jumplist ok");
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    Log(L"start");
    {
        const std::wstring cmd = GetCommandLineW();
        size_t p = cmd.find(L"--demo=");
        if (p != std::wstring::npos) {
            g_demo = true;
            size_t e = cmd.find(L' ', p);
            g_demoKind = cmd.substr(p + 7, e == std::wstring::npos ? std::wstring::npos : e - p - 7);
        }
        if (cmd.find(L"--silent") != std::wstring::npos) g_silent = true;
        if (cmd.find(L"--quit") != std::wstring::npos) g_quitReq = true;
        if (cmd.find(L"--show") != std::wstring::npos) g_showReq = true;
        if (cmd.find(L"--break") != std::wstring::npos) g_breakReq = true;
        if (cmd.find(L"--pause") != std::wstring::npos) g_pauseReq = true;
        if (cmd.find(L"--autostart=on") != std::wstring::npos) g_setAuto = 1;
        else if (cmd.find(L"--autostart=off") != std::wstring::npos) g_setAuto = -1;
    }
    HANDLE mtx = CreateMutexW(nullptr, TRUE,
                              g_demo ? L"Local\\SitBreak_Mutex_demo" : L"Global\\SitBreak_Mutex_v2");
    if (!g_demo && mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
        Log(L"already running");
        HWND other = FindWindowW(L"SitBreakWin", L"起身吧 SitBreak");
        if (other && g_quitReq) {
            Log(L"quit requested");
            PostMessageW(other, WM_QUITAPP, 0, 0);
            return 0;
        }
        if (other) {
            DWORD pid = 0;
            GetWindowThreadProcessId(other, &pid);
            AllowSetForegroundWindow(pid);
            if (g_breakReq) PostMessageW(other, WM_BREAKNOW, 0, 0);
            else if (g_pauseReq) PostMessageW(other, WM_TOGGLEPAUSE, 0, 0);
            if (g_showReq || (!g_breakReq && !g_pauseReq)) {
                PostMessageW(other, WM_SHOWMAIN, 0, 0);
                // 兜底：即使消息没被主循环及时处理，也直接把它叫到前台
                ShowWindow(other, IsIconic(other) ? SW_RESTORE : SW_SHOWNORMAL);
                SetWindowPos(other, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                SetForegroundWindow(other);
                Log(L"raised");
            }
        }
        return 0;
    }

    typedef BOOL(WINAPI * PFN_SPDC)(HANDLE);
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    auto pSetCtx = (PFN_SPDC)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    if (pSetCtx) pSetCtx((HANDLE)-4);
    else SetProcessDPIAware();

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HDC sdc = GetDC(nullptr);
    g_dpi = GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(nullptr, sdc);
    g_scale = g_dpi / 96.f;
    Log(L"dpi ok");

    InitD2D();
    if (!g_d2d || !g_dw) {
        Log(L"d2d failed");
        return 2;
    }
    Log(L"d2d ok");

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"SitBreakWin";
    wc.style = CS_DROPSHADOW;
    wc.hIcon = (HICON)LoadImageW(hInst, L"IDI_SITBREAK", IMAGE_ICON, 0, 0,
                                 LR_DEFAULTSIZE | LR_SHARED);
    wc.hIconSm = (HICON)LoadImageW(hInst, L"IDI_SITBREAK", IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    RegisterClassExW(&wc);
    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    g_main.W = theme::WIN_W;
    g_main.H = theme::WIN_H;
    g_main.hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"SitBreakWin", L"起身吧 SitBreak",
                                  WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN,
                                  0, 0, (int)(g_main.W * g_scale),
                                  (int)(g_main.H * g_scale), nullptr, nullptr, hInst, nullptr);
    if (!g_main.hwnd) {
        Log(L"create main failed");
        return 1;
    }
    Log(L"main win ok");

    CreateWin(g_prompt, 348, 124, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"SitBreakPrompt");
    CreateWin(g_train, 420, 580, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"SitBreakTrain");
    CreateWin(g_set, 372, 540, WS_EX_TOOLWINDOW, L"SitBreakSettings");
    CreateWin(g_play, 560, 496, WS_EX_TOOLWINDOW, L"SitBreakPlay");
    CreateWin(g_toast, 320, 52, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"SitBreakToast");

    App& a = App::I();
    a.hwndMain = g_main.hwnd;
    a.Init(g_main.hwnd);
    Log(L"app init ok");

    SetTimer(g_main.hwnd, 1, 1000, nullptr);
    SetupJumpList();
    // Ctrl+Alt+S：不管窗口在哪，一键叫出来
    if (!RegisterHotKey(g_main.hwnd, HOTKEY_SHOW, MOD_CONTROL | MOD_ALT, 'S'))
        AppLog(L"hotkey failed");
    else
        AppLog(L"hotkey ok");
    if (g_setAuto != 0) {
        a.SetAutostart(g_setAuto > 0);
        AppLog(g_setAuto > 0 ? L"autostart on" : L"autostart off");
        a.SaveAll();
        a.Shutdown();
        if (mtx) CloseHandle(mtx);
        return 0;
    }
    if (g_silent) {
        // 开机静默启动：不弹窗打扰，但窗口是「最小化到任务栏」而不是隐藏。
        // 隐藏的 WS_EX_APPWINDOW 窗口会留下一个点不动的假任务栏按钮，这里避开它。
        ShowWindow(g_main.hwnd, SW_SHOWMINNOACTIVE);
        Log(L"shown minimized");
    } else {
        UI_ShowMain(true);
        Log(L"shown");
    }

    // 开发期预览：--demo=prompt|train|settings|toast
    if (g_demo) {
        const std::wstring& d = g_demoKind;
        if (d == L"prompt") a.TriggerPrompt();
        else if (d == L"train") a.StartTraining(a.Pick());
        else if (d == L"done") {
            a.StartTraining(a.Pick());
            if (a.current) {
                a.stepIdx = (int)a.current->steps.size();
                a.stepLeft = 0;
                a.doneToday = 3;
                a.trainPaused = true;   // 预览时停在完成态
                UI_Pulse();
            }
        }
        else if (d == L"settings") UI_Settings(true);
        else if (d == L"pick") a.OpenPlayList();
        else if (d == L"eye") a.StartPlay(PLAY_EYE8);
        else if (d == L"far") a.StartPlay(PLAY_FAR);
        else if (d == L"blink") a.StartPlay(PLAY_BLINK);
        else if (d == L"breathe") a.StartPlay(PLAY_BREATH);
        else if (d == L"react") a.StartPlay(PLAY_REACT);
        else if (d == L"gonogo") a.StartPlay(PLAY_GONOGO);
        else if (d == L"search") a.StartPlay(PLAY_SEARCH);
        else if (d == L"playdone") {
            a.StartPlay(PLAY_REACT);
            g_pr.hits = 14;
            g_pr.lastRT = 268.f;
            g_pr.early = 1;
            for (int i = 0; i < 14; i++) g_pr.rts.push_back(230.f + (float)(i * 11 % 60));
            a.FinishPlay();
        }
        else if (d == L"toast") UI_Toast(L"已经坐了 45 分钟，起来活动一下");
        Log(L"demo shown");
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Log(L"loop end");
    a.Shutdown();
    if (mtx) CloseHandle(mtx);
    return 0;
}
