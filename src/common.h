// 起身吧 SitBreak —— C++ / Direct2D 版公共定义
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <algorithm>

// ---------------------------------------------------------------- COM 小工具
template <class T>
struct Com {
    T* p = nullptr;
    Com() {}
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset() { if (p) { p->Release(); p = nullptr; } }
    T* operator->() const { return p; }
    T* get() const { return p; }
    T** put() { reset(); return &p; }
    operator bool() const { return p != nullptr; }
};

// ---------------------------------------------------------------- 主题
namespace theme {
    constexpr int   WIN_W      = 372;
    constexpr int   WIN_H      = 668;

    constexpr int   R_CARD     = 20;
    constexpr int   R_BTN      = 16;

    const D2D1_COLOR_F WINDOW  = D2D1::ColorF(0xFFFFFF);
    const D2D1_COLOR_F CARD    = D2D1::ColorF(0xF6F8FA);
    const D2D1_COLOR_F CARD2   = D2D1::ColorF(0xFFFFFF);
    const D2D1_COLOR_F INK     = D2D1::ColorF(0x16181D);
    const D2D1_COLOR_F INK2    = D2D1::ColorF(0x2A2F38);
    const D2D1_COLOR_F SUB     = D2D1::ColorF(0x626C7A);
    const D2D1_COLOR_F MUTED   = D2D1::ColorF(0x9AA4B2);
    const D2D1_COLOR_F LINE    = D2D1::ColorF(0xEBEFF4);
    const D2D1_COLOR_F TRACK   = D2D1::ColorF(0xE7ECF2);
    const D2D1_COLOR_F GREEN   = D2D1::ColorF(0x12B76A);
    const D2D1_COLOR_F GREEN_D = D2D1::ColorF(0x0E9C59);
    const D2D1_COLOR_F GREEN_L = D2D1::ColorF(0x5FD79B);
    const D2D1_COLOR_F GREEN_S = D2D1::ColorF(0xE7F8F0);
    const D2D1_COLOR_F AMBER   = D2D1::ColorF(0xF79009);
    const D2D1_COLOR_F AMBER_S = D2D1::ColorF(0xFEF4E6);
    const D2D1_COLOR_F DARKBAR = D2D1::ColorF(0x1B2027);

    const wchar_t* const FONT = L"Microsoft YaHei UI";
    const wchar_t* const ICON_FONT = L"Segoe Fluent Icons";
    const wchar_t* const ICON_FALLBACK = L"Segoe MDL2 Assets";
}

inline D2D1_COLOR_F mix(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                        a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}
inline D2D1_COLOR_F withA(D2D1_COLOR_F c, float a) {
    c.a = a;
    return c;
}

// ---------------------------------------------------------------- 弹簧（果冻）
struct Spring {
    float v = 0, target = 0, vel = 0;
    float k = 200.f, c = 26.f;
    Spring() {}
    explicit Spring(float init) : v(init), target(init) {}
    void snap(float x) { v = target = x; vel = 0; }
    void to(float t) { target = t; }
    bool moving() const { return std::fabs(vel) > 0.02f || std::fabs(target - v) > 0.02f; }
    // 返回是否仍在运动
    bool update(float dt) {
        if (dt > 0.05f) dt = 0.05f;
        float a = k * (target - v) - c * vel;
        vel += a * dt;
        v += vel * dt;
        if (std::fabs(vel) < 0.5f && std::fabs(target - v) < 0.15f) {
            v = target; vel = 0;
            return false;
        }
        return true;
    }
};

// ---------------------------------------------------------------- 数据
struct Step {
    std::wstring text;
    int secs = 30;
};

struct Exercise {
    std::wstring id;
    std::wstring name;
    std::wstring tip;
    int dur = 60;
    std::vector<Step> steps;
};

struct DayStat {
    int sit = 0;        // 秒
    int breaks = 0;
    int exercises = 0;
    int skipped = 0;
    int eyes = 0;       // 眼部放松次数
    int games = 0;      // 反应力小游戏次数
};

enum AppState {
    ST_WORK = 0,
    ST_PAUSED,
    ST_PROMPT,
    ST_REST,
    ST_TRAINING,
    ST_SNOOZE,
    ST_PLAY
};

// 不用起身的「坐一分钟」项目
enum PlayKind {
    PLAY_EYE8 = 0,   // 8 字追踪
    PLAY_FAR,        // 6 米远眺
    PLAY_BLINK,      // 慢眨练习
    PLAY_BREATH,     // 呼吸调节
    PLAY_REACT,      // 反应力
    PLAY_GONOGO,     // 绿点红点
    PLAY_SEARCH,     // 找出不一样
    PLAY_KIND_N
};

struct PlayDef {
    const wchar_t* name;
    const wchar_t* sub;
    int secs;
    const wchar_t* glyph;
    bool game;       // true = 反应类（计入 games）
};

inline const PlayDef& PlayDefOf(int k) {
    static const PlayDef defs[PLAY_KIND_N] = {
        {L"8 字追踪", L"眼睛跟着圆点画横 8，头别动", 50, L"\uE9F3", false},
        {L"6 米远眺", L"看向 6 米外的一个静物，让眼睛对焦到远处", 45, L"\uE81E", false},
        {L"慢眨练习", L"轻闭两秒、睁开两秒，给眼睛补一层泪膜", 40, L"\uE890", false},
        {L"呼吸放松", L"吸四秒、呼六秒，把肩膀放下来", 60, L"\uEB51", false},
        {L"反应力", L"看到圆点立刻按空格，测你的反应速度", 60, L"\uE945", true},
        {L"绿点红点", L"绿点按、红点忍住，练的是克制", 60, L"\uEA3A", true},
        {L"找出不一样", L"在一堆图形里找到方向不同的那一个", 60, L"\uE721", true},
    };
    if (k < 0 || k >= PLAY_KIND_N) k = 0;
    return defs[k];
}

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_TICK = WM_APP + 2;

// ---------------------------------------------------------------- 应用逻辑
struct App {
    static App& I();

    HWND hwnd = nullptr;
    HWND hwndMain = nullptr;

    // 配置
    int  intervalMin = 45;
    int  idleMin = 5;
    bool sound = true;
    bool topmostPrompt = true;
    bool trayOnClose = true;
    bool autostart = false;

    // 运行态
    AppState state = ST_WORK;
    int work = 0;              // 连续久坐秒数
    int restLeft = 0;
    int snoozeLeft = 0;
    Exercise* current = nullptr;
    int stepIdx = 0;
    int stepLeft = 0;
    bool trainPaused = false;
    int doneTicks = 0;
    int playKind = -1;
    int playLeft = 0;
    int playTotal = 0;
    bool playDone = false;
    std::wstring lastExId;
    int doneToday = 0;

    std::map<std::wstring, DayStat> days;
    std::vector<Exercise> exs;

    void Init(HWND msgWnd);
    void Shutdown();

    void LoadCfg();
    void SaveCfg();
    void LoadStats();
    void SaveStats();

    void Tick();               // 每秒
    void TrainTick();          // 训练中每秒
    void RestTick();

    DayStat& Today();
    std::wstring TodayKey();
    int WeekBreaks(int back);  // back=0 今天
    int WeekSit(int back);

    void AddSit(int s);
    void CountBreak();
    void CountExercise();

    void TriggerPrompt();
    void PromptStart();        // 现在起身 → 开始训练
    void PromptWalk();
    void PromptSnooze(int min);
    void PromptClose();

    void StartBreak(int mins);
    Exercise* Pick();
    void StartTraining(Exercise* e);
    void TrainNext();
    void TrainSkip();
    void TrainFinish(bool completed);
    void FinishClose();
    void TrainBack();
    void StartPlay(int kind);
    void OpenPlayList();
    void ClosePlay();
    void PlayTick();
    void FinishPlay();

    void TogglePause();
    void BreakNow();

    void Notify(const std::wstring& title, const std::wstring& body);
    void SetAutostart(bool on);
    bool GetAutostart();

    std::wstring StatusText();
    int RemainSeconds();
    float Progress();
    void TrayInit();
    void TrayMenu();
    void TrayCommand(int id);
    void TrayTip(const std::wstring& s);

    void SaveAll();
};

// ---------------------------------------------------------------- UI 入口（main.cpp）
void AppLog(const wchar_t* s);
void UI_Refresh(bool all = false);
void UI_ShowMain(bool show, bool animate = true);
bool UI_MainVisible();
void UI_Prompt(bool show);
void UI_Train(bool show);
void UI_Play(bool show);
void UI_PlayPick();
void UI_Settings(bool show);
void UI_Toast(const std::wstring& s);
void UI_Pulse();
void UI_ApplyTopmost();
