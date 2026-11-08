// 起身吧 SitBreak —— 应用逻辑：配置 / 统计 / 计时 / 托盘 / 通知 / 自启
#include "common.h"

#include <shlobj.h>
#include <objbase.h>
#include <mmsystem.h>

#pragma comment(lib, "d2d1.lib")

// ---------------------------------------------------------------- 路径工具
static std::wstring ExePathW() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    return std::wstring(buf, n);
}

static std::wstring DataDirW() {
    wchar_t buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf))) {
        std::wstring d = std::wstring(buf) + L"\\SitBreak";
        CreateDirectoryW(d.c_str(), nullptr);
        return d;
    }
    return L".";
}

static std::wstring CfgPath() { return DataDirW() + L"\\config.ini"; }
static std::wstring StatsPath() { return DataDirW() + L"\\stats.ini"; }

static int IniGetInt(const std::wstring& file, const wchar_t* sec, const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(sec, key, def, file.c_str());
}
static void IniSetInt(const std::wstring& file, const wchar_t* sec, const wchar_t* key, int v) {
    wchar_t b[32];
    wsprintfW(b, L"%d", v);
    WritePrivateProfileStringW(sec, key, b, file.c_str());
}

DWORD IdleSeconds() {
    LASTINPUTINFO li;
    li.cbSize = sizeof(li);
    if (!GetLastInputInfo(&li)) return 0;
    DWORD now = GetTickCount();
    DWORD idle = (now - li.dwTime) / 1000;
    return idle;
}

static void PlayCue(int kind) {
    if (!App::I().sound) return;
    if (kind == 1) {
        MessageBeep(MB_ICONASTERISK);
        Sleep(90);
        MessageBeep(MB_ICONASTERISK);
    } else {
        MessageBeep(MB_ICONASTERISK);
    }
}

// ---------------------------------------------------------------- 托盘图标（运行时绘制）
static HICON MakeIcon(int size) {
    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC dc = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !bits) return nullptr;

    HDC mdc = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(mdc, color);
    PatBlt(mdc, 0, 0, size, size, BLACKNESS);

    HBRUSH gb = CreateSolidBrush(RGB(0x12, 0xB7, 0x6A));
    HBRUSH wb = CreateSolidBrush(RGB(255, 255, 255));
    HGDIOBJ ob = SelectObject(mdc, gb);
    HGDIOBJ op = SelectObject(mdc, GetStockObject(NULL_PEN));
    Ellipse(mdc, 0, 0, size, size);
    SelectObject(mdc, wb);
    // 头
    int cx = size / 2;
    Ellipse(mdc, cx - size / 7, size / 5, cx + size / 7, size / 5 + (size * 2) / 7);
    // 身体
    RoundRect(mdc, cx - size / 4, size / 2 + size / 16, cx + size / 4, size - size / 6,
              size / 4, size / 4);
    SelectObject(mdc, ob);
    SelectObject(mdc, op);
    SelectObject(mdc, old);
    DeleteDC(mdc);
    DeleteObject(gb);
    DeleteObject(wb);

    // 用 GDI 画完的 alpha 需要补：把非黑像素设为不透明
    unsigned char* px = (unsigned char*)bits;
    for (int i = 0; i < size * size; i++) {
        unsigned char b = px[i * 4 + 0], g = px[i * 4 + 1], r = px[i * 4 + 2];
        if (r > 24 || g > 24 || b > 24) px[i * 4 + 3] = 255;
        else px[i * 4 + 3] = 0;
    }

    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON h = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return h;
}

static HICON g_trayIcon = nullptr;

// ---------------------------------------------------------------- App
App& App::I() {
    static App a;
    return a;
}

std::wstring App::TodayKey() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t b[32];
    wsprintfW(b, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    return b;
}

static std::wstring KeyDaysAgo(int back) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart -= (ULONGLONG)back * 864000000000ULL;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    FileTimeToSystemTime(&ft, &st);
    wchar_t b[32];
    wsprintfW(b, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    return b;
}

DayStat& App::Today() {
    return days[TodayKey()];
}

int App::WeekBreaks(int back) {
    auto it = days.find(KeyDaysAgo(back));
    return it == days.end() ? 0 : it->second.breaks;
}

int App::WeekSit(int back) {
    auto it = days.find(KeyDaysAgo(back));
    return it == days.end() ? 0 : it->second.sit;
}

void App::AddSit(int s) {
    Today().sit += s;
}

void App::CountBreak() {
    Today().breaks += 1;
    SaveStats();
}

void App::CountExercise() {
    Today().exercises += 1;
    doneToday++;
    SaveStats();
}

// ---------------------------------------------------------------- 动作库
static void AddEx(std::vector<Exercise>& v, const wchar_t* id, const wchar_t* name,
                  const wchar_t* tip, std::vector<Step> steps) {
    Exercise e;
    e.id = id;
    e.name = name;
    e.tip = tip;
    e.steps = steps;
    e.dur = 0;
    for (auto& s : e.steps) e.dur += s.secs;
    v.push_back(e);
}

static void BuildExercises(std::vector<Exercise>& v) {
    v.clear();
    AddEx(v, L"neck", L"颈部放松", L"动作要慢，出现头晕或刺痛立刻停下。",
          {{L"坐直，下巴微收，后脑勺向上延伸", 15},
           {L"头缓慢倒向右侧，感觉左侧拉长，保持呼吸", 20},
           {L"回正，再缓慢倒向左侧，同样保持呼吸", 20}});
    AddEx(v, L"shoulder", L"肩部绕环", L"肩膀画大圈，别耸肩。",
          {{L"双肩向前画大圈，慢慢来", 20},
           {L"反过来向后画圈，感受肩胛骨活动", 20}});
    AddEx(v, L"wall", L"靠墙天使", L"后脑、上背、臀部贴墙，腰不要塌。",
          {{L"背贴墙站好，手臂呈 W 形贴墙", 15},
           {L"沿墙面缓慢上滑到 Y 形，再回落", 30}});
    AddEx(v, L"squat", L"徒手深蹲", L"膝盖对准脚尖，蹲到大腿接近水平即可。",
          {{L"双脚与肩同宽，臀部向后坐", 20},
           {L"起身时收紧臀部，重复 10 次", 25}});
    AddEx(v, L"calf", L"提踵热身", L"扶着桌子做，落地要轻。",
          {{L"踮起脚尖，停一秒，缓慢落下", 25},
           {L"重复 15 次，感受小腿发力", 20}});
    AddEx(v, L"hip", L"髋屈肌拉伸", L"骨盆保持中立，别用塌腰换幅度。",
          {{L"单膝跪地，另一条腿在前呈弓步", 20},
           {L"收紧臀部，身体微微前移", 25}});
    AddEx(v, L"chest", L"门框扩胸", L"肘部略低于肩，别憋气。",
          {{L"双手扶门框，一脚在前", 15},
           {L"身体前倾打开胸口，深呼吸", 25}});
    AddEx(v, L"side", L"侧腰伸展", L"拉伸时不要屏气，保持均匀呼吸。",
          {{L"双脚交叉站立，一手举过头顶", 20},
           {L"向对侧缓慢侧倾，感受侧腰", 20}});
    AddEx(v, L"wrist", L"手腕放松", L"长时间打字后做，力度轻柔。",
          {{L"手臂前伸，掌心向外，手指向下压", 18},
           {L"换掌心向内，轻轻回拉手指", 18}});
    AddEx(v, L"eye", L"20-20-20 眼睛休息", L"看远处时别盯屏幕，让眼睛真正放松。",
          {{L"看向 6 米外的物体，保持 20 秒", 20},
           {L"闭眼深呼吸，转动眼球", 15}});
    AddEx(v, L"walk", L"起身走动", L"走起来就好，接杯水、看看窗外。",
          {{L"离开座位，慢慢走动", 60},
           {L"顺便做几次深呼吸，放松肩膀", 60}});
    AddEx(v, L"pushup", L"靠墙俯卧撑", L"身体保持一条直线，别塌腰。",
          {{L"双手撑墙略宽于肩，屈肘靠近墙面", 20},
           {L"推回起始位，重复 12 次", 25}});
}

// ---------------------------------------------------------------- 初始化 / 持久化
void App::Init(HWND msgWnd) {
    hwnd = msgWnd;
    BuildExercises(exs);
    LoadCfg();
    LoadStats();
    state = ST_WORK;
    work = 0;
    TrayInit();
}

void App::LoadCfg() {
    std::wstring f = CfgPath();
    intervalMin = IniGetInt(f, L"main", L"interval", 45);
    idleMin = IniGetInt(f, L"main", L"idle", 5);
    sound = IniGetInt(f, L"main", L"sound", 1) != 0;
    topmostPrompt = IniGetInt(f, L"main", L"topmost", 1) != 0;
    trayOnClose = IniGetInt(f, L"main", L"trayclose", 1) != 0;
    autostart = GetAutostart();
    if (intervalMin < 5) intervalMin = 5;
    if (idleMin < 1) idleMin = 1;
    // 恢复上次的连续坐时（同一天、且没超过一个提醒周期），重启不丢计时
    int w = IniGetInt(f, L"main", L"work", 0);
    int wd = IniGetInt(f, L"main", L"workday", 0);
    int today = (int)((long long)time(nullptr) / 86400);
    if (wd == today && w > 0 && w < intervalMin * 60) work = w;
}

void App::SaveCfg() {
    std::wstring f = CfgPath();
    IniSetInt(f, L"main", L"interval", intervalMin);
    IniSetInt(f, L"main", L"idle", idleMin);
    IniSetInt(f, L"main", L"sound", sound ? 1 : 0);
    IniSetInt(f, L"main", L"topmost", topmostPrompt ? 1 : 0);
    IniSetInt(f, L"main", L"trayclose", trayOnClose ? 1 : 0);
    IniSetInt(f, L"main", L"work", work);
    IniSetInt(f, L"main", L"workday", (int)((long long)time(nullptr) / 86400));
}

void App::LoadStats() {
    days.clear();
    std::wstring f = StatsPath();
    for (int i = 0; i < 45; i++) {
        std::wstring k = KeyDaysAgo(i);
        DayStat d;
        d.sit = IniGetInt(f, k.c_str(), L"sit", 0);
        d.breaks = IniGetInt(f, k.c_str(), L"breaks", 0);
        d.exercises = IniGetInt(f, k.c_str(), L"exercises", 0);
        d.skipped = IniGetInt(f, k.c_str(), L"skipped", 0);
        d.eyes = IniGetInt(f, k.c_str(), L"eyes", 0);
        d.games = IniGetInt(f, k.c_str(), L"games", 0);
        if (d.sit || d.breaks || d.exercises || d.skipped || d.eyes || d.games)
            days[k] = d;
    }
    Today();
}

void App::SaveStats() {
    std::wstring f = StatsPath();
    // 只落盘最近 45 天，避免文件膨胀
    for (int i = 0; i < 45; i++) {
        std::wstring k = KeyDaysAgo(i);
        auto it = days.find(k);
        if (it == days.end()) continue;
        IniSetInt(f, k.c_str(), L"sit", it->second.sit);
        IniSetInt(f, k.c_str(), L"breaks", it->second.breaks);
        IniSetInt(f, k.c_str(), L"exercises", it->second.exercises);
        IniSetInt(f, k.c_str(), L"skipped", it->second.skipped);
    }
}

void App::SaveAll() {
    SaveCfg();
    SaveStats();
}

void App::Shutdown() {
    SaveAll();
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (g_trayIcon) {
        DestroyIcon(g_trayIcon);
        g_trayIcon = nullptr;
    }
}

// ---------------------------------------------------------------- 自启
bool App::GetAutostart() {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                      KEY_READ, &hk) != ERROR_SUCCESS)
        return false;
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD sz = sizeof(buf);
    DWORD type = 0;
    LONG r = RegQueryValueExW(hk, L"SitBreak", nullptr, &type, (LPBYTE)buf, &sz);
    RegCloseKey(hk);
    return r == ERROR_SUCCESS && sz > 2;
}

void App::SetAutostart(bool on) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                      KEY_SET_VALUE, &hk) != ERROR_SUCCESS)
        return;
    if (on) {
        std::wstring v = L"\"" + ExePathW() + L"\" --silent";
        RegSetValueExW(hk, L"SitBreak", 0, REG_SZ, (const BYTE*)v.c_str(),
                       (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hk, L"SitBreak");
    }
    RegCloseKey(hk);
    autostart = on;
}

// ---------------------------------------------------------------- 托盘
void App::TrayInit() {
    if (!g_trayIcon) g_trayIcon = MakeIcon(32);
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = g_trayIcon;
    lstrcpynW(nid.szTip, L"起身吧 · 久坐提醒", 64);
    BOOL ok = Shell_NotifyIconW(NIM_ADD, &nid);
    AppLog(ok ? L"tray add ok" : L"tray add FAILED");
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void App::TrayTip(const std::wstring& s) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_TIP | NIF_SHOWTIP;
    lstrcpynW(nid.szTip, s.c_str(), 64);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::Notify(const std::wstring& title, const std::wstring& body) {
    if (g_trayIcon) {
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_INFO;
        nid.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
        lstrcpynW(nid.szInfoTitle, title.c_str(), 64);
        lstrcpynW(nid.szInfo, body.c_str(), 256);
        BOOL ok = Shell_NotifyIconW(NIM_MODIFY, &nid);
        AppLog(ok ? L"notify ok" : L"notify FAILED");
    } else {
        AppLog(L"notify skipped: no tray icon");
    }
}

void App::TrayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 100, L"显示主界面");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 101, L"现在起身");
    AppendMenuW(m, MF_STRING | (state == ST_PAUSED ? MF_CHECKED : 0), 102, L"暂停提醒");
    AppendMenuW(m, MF_STRING, 103, L"设置");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 105, L"退出");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenuEx(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, hwnd, nullptr);
    DestroyMenu(m);
}

void App::TrayCommand(int id) {
    switch (id) {
        case 100: UI_ShowMain(true); break;
        case 101: BreakNow(); break;
        case 102: TogglePause(); break;
        case 103: UI_Settings(true); break;
        case 105: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
    }
}

// ---------------------------------------------------------------- 计时
void App::Tick() {
    if (state == ST_PROMPT || state == ST_TRAINING) return;
    if (state == ST_PLAY) {          // 坐着练：倒计时由这里走，游戏内部另有帧计时
        PlayTick();
        return;
    }

    if (state == ST_SNOOZE) {
        if (--snoozeLeft <= 0) {
            state = ST_WORK;
            work = intervalMin * 60;  // 立刻提醒
            TriggerPrompt();
            return;
        }
        UI_Refresh();
        return;
    }
    if (state == ST_REST) {
        if (--restLeft <= 0) {
            state = ST_WORK;
            work = 0;
            UI_Refresh(true);
            UI_Toast(L"休息结束，继续加油");
            return;
        }
        UI_Refresh();
        return;
    }
    if (state == ST_PAUSED) return;

    // 工作态
    AddSit(1);
    work++;
    if (IdleSeconds() >= (DWORD)idleMin * 60 && work >= 120) {
        CountBreak();
        work = 0;
        SaveStats();
        UI_Refresh(true);
        return;
    }
    if (work >= intervalMin * 60) {
        TriggerPrompt();
        return;
    }
    if (work % 60 == 0) SaveCfg();   // 每分钟落盘一次，重开不丢进度
    if (work % 15 == 0) UI_Refresh();
}

void App::TrainTick() {
    if (state != ST_TRAINING || !current || trainPaused) return;
    if (stepIdx >= (int)current->steps.size()) {   // 完成态停留 2 秒
        if (++doneTicks >= 2) { FinishClose(); }
        return;
    }
    if (--stepLeft > 0) return;
    // 当前步骤结束
    if (stepIdx + 1 < (int)current->steps.size()) {
        stepIdx++;
        stepLeft = current->steps[stepIdx].secs;
        PlayCue(0);
        UI_Refresh(true);
    } else {
        TrainFinish(true);
    }
}

void App::RestTick() {}

// ---------------------------------------------------------------- 流程
void App::TriggerPrompt() {
    if (state == ST_PAUSED) return;
    state = ST_PROMPT;
    current = Pick();
    stepIdx = 0;
    if (current) stepLeft = current->steps[0].secs;
    PlayCue(0);
    std::wstring body = L"已连续坐 " + std::to_wstring(work / 60) + L" 分钟，起来活动 1 分钟吧";
    Notify(L"该起身啦", body);
    UI_Prompt(true);
    UI_Refresh(true);
}

Exercise* App::Pick() {
    if (exs.empty()) return nullptr;
    Exercise* e = &exs[(size_t)(rand() % exs.size())];
    if (e->id == lastExId && exs.size() > 1) {
        e = &exs[(size_t)(rand() % exs.size())];
    }
    lastExId = e->id;
    return e;
}

void App::PromptStart() {
    UI_Prompt(false);
    StartTraining(current);
}

void App::PromptWalk() {
    UI_Prompt(false);
    StartBreak(3);
}

void App::PromptSnooze(int min) {
    UI_Prompt(false);
    state = ST_SNOOZE;
    snoozeLeft = min * 60;
    work = 0;
    UI_Refresh(true);
    UI_Toast(L"好，5 分钟后再提醒你");
}

void App::PromptClose() {
    UI_Prompt(false);
    state = ST_WORK;
    Today().skipped++;
    SaveStats();
    UI_Refresh(true);
}

void App::StartBreak(int mins) {
    state = ST_REST;
    restLeft = mins * 60;
    work = 0;
    CountBreak();
    UI_Refresh(true);
    UI_Toast(L"休息一下，看看远处");
}

void App::BreakNow() {
    if (state == ST_PROMPT) UI_Prompt(false);
    if (state == ST_TRAINING) TrainFinish(false);
    if (current) {
        StartTraining(current);
    } else {
        StartBreak(3);
    }
}

void App::StartTraining(Exercise* e) {
    if (!e) return;
    current = e;
    stepIdx = 0;
    stepLeft = e->steps.empty() ? 30 : e->steps[0].secs;
    trainPaused = false;
    doneTicks = 0;
    state = ST_TRAINING;
    UI_Train(true);
    UI_Refresh(true);
}

void App::TrainNext() {
    if (!current) return;
    if (stepIdx >= (int)current->steps.size()) { FinishClose(); return; }
    if (stepIdx + 1 < (int)current->steps.size()) {
        stepIdx++;
        stepLeft = current->steps[stepIdx].secs;
        PlayCue(0);
        UI_Refresh(true);
    } else {
        TrainFinish(true);
    }
}

void App::TrainSkip() {
    if (!current) return;
    if (stepIdx >= (int)current->steps.size()) { FinishClose(); return; }
    Today().skipped++;
    SaveStats();
    TrainFinish(false);
}

void App::TrainFinish(bool completed) {
    if (completed && current) {          // 进入完成态：勾选动画后自动关闭
        stepIdx = (int)current->steps.size();
        stepLeft = 0;
        doneTicks = 0;
        CountExercise();
        PlayCue(1);
        UI_Pulse();
        return;
    }
    UI_Train(false);
    state = ST_WORK;
    work = 0;
    UI_Refresh(true);
}

void App::FinishClose() {
    std::wstring name = current ? current->name : std::wstring(L"训练");
    UI_Train(false);
    state = ST_WORK;
    work = 0;
    UI_Toast(L"完成 " + name + L"，很棒");
    UI_Refresh(true);
}

void App::TrainBack() {
    UI_Train(false);
    state = ST_WORK;
    UI_Refresh(true);
}

// ---------------------------------------------------------------- 坐一分钟（不用起身）
void App::StartPlay(int kind) {
    if (kind < 0 || kind >= PLAY_KIND_N) return;
    if (state == ST_PROMPT) UI_Prompt(false);
    if (state == ST_TRAINING) UI_Train(false);
    playKind = kind;
    playTotal = PlayDefOf(kind).secs;
    playLeft = playTotal;
    playDone = false;
    state = ST_PLAY;
    UI_Play(true);          // 窗口自己会重置本局状态
    UI_Refresh(true);
}

void App::OpenPlayList() {
    if (state == ST_PROMPT) UI_Prompt(false);
    if (state == ST_TRAINING) UI_Train(false);
    playKind = -1;
    playLeft = 0;
    playDone = false;
    state = ST_PLAY;
    UI_PlayPick();
    UI_Refresh(true);
}

void App::ClosePlay() {
    playKind = -1;
    playDone = false;
    UI_Play(false);
    state = ST_WORK;
    UI_Refresh(true);
}

void App::PlayTick() {
    if (state != ST_PLAY || playKind < 0) return;
    if (playDone) return;                     // 停在成绩页，等用户确认
    if (playLeft > 0) playLeft--;
    if (playLeft == 0) {
        FinishPlay();                         // 记一次，但不关窗
        return;
    }
    UI_Refresh();
}

void App::FinishPlay() {
    if (playKind < 0 || playDone) return;
    int k = playKind;
    playDone = true;
    bool game = PlayDefOf(k).game;
    if (game) Today().games++;
    else Today().eyes++;
    std::wstring msg = PlayDefOf(k).name;
    msg += game ? L" 完成 · 这一局记下了" : L" 完成 · 眼睛松了，看得更清";
    UI_Toast(msg);
    SaveStats();
    // 坐着练不算离座，但仍留一分钟缓冲再提醒，避免刚放松完就被叫起来
    int cap = intervalMin * 60 - 60;
    if (cap < 0) cap = 0;
    if (work > cap) work = cap;
    UI_Refresh(true);
}

void App::TogglePause() {
    if (state == ST_PAUSED) {
        state = ST_WORK;
        work = 0;
        UI_Toast(L"提醒已恢复");
    } else {
        if (state == ST_PROMPT) UI_Prompt(false);
        state = ST_PAUSED;
        UI_Toast(L"已暂停，休息够了再开");
    }
    UI_Refresh(true);
}

// ---------------------------------------------------------------- 展示用的派生值
int App::RemainSeconds() {
    if (state == ST_REST) return restLeft;
    if (state == ST_SNOOZE) return snoozeLeft;
    if (state == ST_TRAINING) return stepLeft;
    int r = intervalMin * 60 - work;
    return r < 0 ? 0 : r;
}

float App::Progress() {
    if (state == ST_REST) {
        int total = 180;
        return 1.f - (float)restLeft / (float)total;
    }
    if (state == ST_TRAINING && current && !current->steps.empty()) {
        int tot = 0, done = 0;
        for (int i = 0; i < (int)current->steps.size(); i++) {
            tot += current->steps[i].secs;
            if (i < stepIdx) done += current->steps[i].secs;
        }
        done += current->steps[stepIdx].secs - stepLeft;
        return tot ? (float)done / (float)tot : 0.f;
    }
    if (state == ST_SNOOZE) return 0.f;
    float f = (float)work / (float)(intervalMin * 60);
    return f > 1.f ? 1.f : f;
}

std::wstring App::StatusText() {
    switch (state) {
        case ST_PAUSED: return L"已暂停提醒";
        case ST_PROMPT: return L"等你决定 · 起来动一下";
        case ST_REST: return L"休息中 · 看看窗外";
        case ST_TRAINING: return current ? current->name : L"训练中";
        case ST_SNOOZE:
            return L"已延后 · " + std::to_wstring((snoozeLeft + 59) / 60) + L" 分钟后再提醒";
        default: {
            int m = (intervalMin * 60 - work + 59) / 60;
            if (m < 0) m = 0;
            if (work < 60) return L"刚刚开始 · 起身走动一下";
            return L"专注中 · " + std::to_wstring(m) + L" 分钟后提醒起身";
        }
    }
}
