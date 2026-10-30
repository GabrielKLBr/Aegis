// UiApp.cpp - GuardianAV desktop UI.
//
// Rendering: Direct2D (GPU-accelerated 2D vector graphics) + DirectWrite (text).
// This is hardware-accelerated through Direct3D under the hood, but contains no
// shader/swap-chain/3D-pipeline code, no web/browser engine, and no raw
// OpenGL/Vulkan. It is the same class of API real native apps use for crisp,
// accelerated UI.
#ifndef UNICODE
#define UNICODE
#endif

// Target Windows 10+ so per-monitor-v2 DPI APIs (SetProcessDpiAwarenessContext,
// GetDpiForWindow, GetDpiForSystem) are declared by the SDK headers.
#define WINVER        0x0A00
#define _WIN32_WINNT  0x0A00
#define NTDDI_VERSION 0x0A000004

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <shobjidl.h>
#include <shlobj.h>

#include <string>
#include <vector>
#include <cmath>

#include "ComPtr.h"
#include "Common.h"
#include "EngineFacade.h"
#include "quarantine/Store.h"
#include "Sandbox.h"
#include "services/Services.h"

#include <thread>
#include <mutex>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "ole32.lib")

using D2D1::ColorF;
using D2D1::RectF;
using D2D1::Point2F;
using D2D1::RoundedRect;

// ---------------------------------------------------------------------------
// Messages + small helpers
// ---------------------------------------------------------------------------
static const UINT WM_APP_PROGRESS = WM_APP + 1;
static const UINT WM_APP_DONE     = WM_APP + 2;
static const UINT WM_APP_Q_DONE = WM_APP + 5;
static const UINT WM_APP_SBX_DONE = WM_APP + 3;
static const UINT WM_APP_SERVICE_DONE = WM_APP + 4;
static const UINT_PTR TIMER_ANIM  = 1;
static const UINT_PTR TIMER_SERVICES = 2;
static constexpr int ID_SERVICE_INSTALL = 2000, ID_SERVICE_AV = 2001, ID_SERVICE_SANDBOX = 2002;

static bool PtIn(const D2D1_RECT_F& r, float x, float y) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
struct Theme {
    ColorF bg        = ColorF(0x15171C);
    ColorF sidebar   = ColorF(0x1B1E25);
    ColorF card      = ColorF(0x20242D);
    ColorF cardHi    = ColorF(0x262B35);
    ColorF stroke    = ColorF(0x2E333E);
    ColorF text      = ColorF(0xF2F4F8);
    ColorF textDim   = ColorF(0x8A92A6);
    ColorF accent    = ColorF(0x4C8DFF); // primary blue
    ColorF accentHi  = ColorF(0x6BA1FF);
    ColorF good      = ColorF(0x3DD68C); // protected green
    ColorF danger    = ColorF(0xFF5C5C);
    ColorF navSel    = ColorF(0x2A3550);
};
static Theme T;

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
enum class Page { Dashboard, Scan, Sandbox, Quarantine, Settings };

struct App {
    HWND hwnd = nullptr;
    float scale = 1.0f;

    // Direct2D / DirectWrite (device-independent)
    ComPtr<ID2D1Factory>    d2dFactory;
    ComPtr<IDWriteFactory>  dwFactory;
    ComPtr<IDWriteTextFormat> fTitle, fH1, fH2, fBody, fSmall, fBtn, fNum, fNumSmall, fIcon;

    // Device-dependent
    ComPtr<ID2D1HwndRenderTarget> rt;

    // UI state
    Page page = Page::Dashboard;
    int  hoverNav = -1;
    int  hoverBtn = -1;     // 0 quickscan, 1 choose, 2 cancel
    av::services::Status services[2];
    HWND serviceButtons[3]{};
    bool serviceBusy = false;
    std::thread serviceThread;
    std::wstring serviceMessage;
    std::mutex serviceMtx;
    float animAngle = 0.0f;
    std::wstring lastScanText = L"No scans yet";

    // hit rects (recomputed each paint)
    D2D1_RECT_F navRect[5]{};
    D2D1_RECT_F quickBtn{}, chooseBtn{}, cancelBtn{}, toggleRect[3]{};
    D2D1_RECT_F sbxAnalyzeBtn{}, sbxSampleBtn{}, sbxVmBtn{}, sbxCheckpointBtn{}, sbxResumeBtn{};

    av::Engine engine{true};

    std::atomic<bool> qRunning{false}, qCancel{false};
    std::mutex qMtx;
    av::services::Json qReport=av::services::Json::object(), qEntries=av::services::Json::array(),qAudit=av::services::Json::array();
    bool qAuditMode=false;
    std::wstring qMessage;
    std::string qSelected,qConfirm;
    size_t qOffset=0,qPageSize=1,qResumeOffset=0;
    std::thread qThread;
    D2D1_RECT_F qButtons[7]{};
    std::vector<std::pair<D2D1_RECT_F,std::string>> qRows;
    std::vector<std::pair<D2D1_RECT_F,av::Detection>> qDetections;
    // sandbox state
    std::atomic<bool>     sbxRunning{false};
    std::atomic<bool>     sbxCancel{false};
    std::wstring          sbxFile;
    std::mutex            sbxMtx;
    av::services::Json    sbxReport;
    std::string          sbxStage;
    bool                  sbxHasReport = false;
    std::thread           sbxThread;
};
static App g;

static void RefreshServices() {
    g.services[0] = av::services::Query(av::services::Kind::Antivirus);
    g.services[1] = av::services::Query(av::services::Kind::Sandbox);
    for (int i = 0; i < 2; ++i) {
        const auto& state = g.services[i];
        const bool stable = state.state == SERVICE_RUNNING || state.state == SERVICE_STOPPED;
        SetWindowTextW(g.serviceButtons[i + 1], state.Running() ? L"Desligar" : L"Ligar");
        EnableWindow(g.serviceButtons[i + 1], !g.serviceBusy && state.installed && !state.error && stable);
    }
    EnableWindow(g.serviceButtons[0], !g.serviceBusy && !g.engine.IsRunning() && !g.sbxRunning);
    SetWindowTextW(g.serviceButtons[0], g.services[0].installed || g.services[1].installed ? L"Reparar serviços..." : L"Instalar serviços...");
}

static void ServiceAction(int action) {
    if (g.serviceBusy) return;
    if (action == ID_SERVICE_INSTALL && (g.engine.IsRunning() || g.sbxRunning)) return;
    if (g.serviceThread.joinable()) g.serviceThread.join();
    g.serviceBusy = true;
    { std::lock_guard lock(g.serviceMtx); g.serviceMessage = action == ID_SERVICE_INSTALL ? L"Aguardando a autorização do Windows..." : L"Atualizando o serviço..."; }
    RefreshServices();
    const bool stop = action != ID_SERVICE_INSTALL && g.services[action - ID_SERVICE_AV].Running();
    g.serviceThread = std::thread([action, stop] {
        std::wstring message;
        try {
            if (action == ID_SERVICE_INSTALL) {
                const auto result = av::services::Setup(g.hwnd);
                if (result) av::services::WinError("Instalação dos serviços", result);
                message = L"Serviços instalados. Use Ligar para iniciar cada engine.";
            } else {
                av::services::SetRunning(action == ID_SERVICE_AV ? av::services::Kind::Antivirus : av::services::Kind::Sandbox, !stop);
                message = stop ? L"Serviço desligado; as análises dele foram encerradas." : L"Serviço ligado.";
            }
        } catch (const std::exception& error) { message = av::services::Wide(error.what()); }
        { std::lock_guard lock(g.serviceMtx); g.serviceMessage = message; }
        PostMessageW(g.hwnd, WM_APP_SERVICE_DONE, 0, 0);
    });
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

static float S(float v) { return v * g.scale; }

// ---------------------------------------------------------------------------
// Resource creation
// ---------------------------------------------------------------------------
static ComPtr<IDWriteTextFormat> MakeFormat(const wchar_t* family, float size,
                                            DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL) {
    ComPtr<IDWriteTextFormat> f;
    g.dwFactory->CreateTextFormat(family, nullptr, w, DWRITE_FONT_STYLE_NORMAL,
                                  DWRITE_FONT_STRETCH_NORMAL, S(size), L"en-us",
                                  f.GetAddressOf());
    return f;
}

static void CreateDeviceIndependentResources() {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                      __uuidof(ID2D1Factory),
                      reinterpret_cast<void**>(g.d2dFactory.GetAddressOf()));

    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(g.dwFactory.GetAddressOf()));

    g.fTitle    = MakeFormat(L"Segoe UI", 26.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g.fH1       = MakeFormat(L"Segoe UI", 20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g.fH2       = MakeFormat(L"Segoe UI", 15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g.fBody     = MakeFormat(L"Segoe UI", 13.0f);
    g.fSmall    = MakeFormat(L"Segoe UI", 11.5f);
    g.fBtn      = MakeFormat(L"Segoe UI", 13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g.fNum      = MakeFormat(L"Segoe UI", 30.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g.fNumSmall = MakeFormat(L"Segoe UI", 12.0f);
    g.fIcon     = MakeFormat(L"Segoe UI Symbol", 17.0f);
}

static void CreateDeviceResources() {
    if (g.rt) return;
    RECT rc; GetClientRect(g.hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    g.d2dFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(g.hwnd, size, D2D1_PRESENT_OPTIONS_NONE),
        g.rt.ReleaseAndGetAddressOf());

    // Lay out in DIP-like pixels: we scale metrics ourselves via S().
    g.rt->SetDpi(96.0f, 96.0f);
}

static void DiscardDeviceResources() { g.rt.Reset(); }

// ---------------------------------------------------------------------------
// Drawing primitives
// ---------------------------------------------------------------------------
static ComPtr<ID2D1SolidColorBrush> Brush(const ColorF& c) {
    ComPtr<ID2D1SolidColorBrush> b;
    g.rt->CreateSolidColorBrush(c, b.GetAddressOf());
    return b;
}

static void FillRound(const D2D1_RECT_F& r, float radius, const ColorF& c) {
    auto b = Brush(c);
    g.rt->FillRoundedRectangle(RoundedRect(r, radius, radius), b.Get());
}
static void Text(const std::wstring& s, IDWriteTextFormat* fmt, const D2D1_RECT_F& r,
                 const ColorF& c,
                 DWRITE_TEXT_ALIGNMENT ha = DWRITE_TEXT_ALIGNMENT_LEADING,
                 DWRITE_PARAGRAPH_ALIGNMENT va = DWRITE_PARAGRAPH_ALIGNMENT_NEAR) {
    fmt->SetTextAlignment(ha);
    fmt->SetParagraphAlignment(va);
    auto b = Brush(c);
    g.rt->DrawTextW(s.c_str(), (UINT32)s.size(), fmt, r, b.Get(),
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// A filled circle with a check mark (the "protected" badge).
static void DrawShieldCheck(float cx, float cy, float radius, const ColorF& col) {
    auto b = Brush(col);
    g.rt->FillEllipse(D2D1::Ellipse(Point2F(cx, cy), radius, radius), b.Get());

    ComPtr<ID2D1StrokeStyle> cap;
    g.d2dFactory->CreateStrokeStyle(
        D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                    D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
        nullptr, 0, cap.GetAddressOf());
    auto white = Brush(ColorF(ColorF::White));
    float w = radius * 0.16f;
    g.rt->DrawLine(Point2F(cx - radius*0.42f, cy + radius*0.02f),
                   Point2F(cx - radius*0.10f, cy + radius*0.34f), white.Get(), w, cap.Get());
    g.rt->DrawLine(Point2F(cx - radius*0.10f, cy + radius*0.34f),
                   Point2F(cx + radius*0.44f, cy - radius*0.30f), white.Get(), w, cap.Get());
}

// A spinning arc used while scanning.
static void DrawSpinner(float cx, float cy, float radius, float angleDeg, const ColorF& col) {
    ComPtr<ID2D1PathGeometry> geo;
    g.d2dFactory->CreatePathGeometry(geo.GetAddressOf());
    ComPtr<ID2D1GeometrySink> sink;
    geo->Open(sink.GetAddressOf());

    float a0 = angleDeg * 3.14159265f / 180.0f;
    float sweep = 270.0f * 3.14159265f / 180.0f;
    D2D1_POINT_2F start = Point2F(cx + radius*std::cos(a0), cy + radius*std::sin(a0));
    D2D1_POINT_2F end   = Point2F(cx + radius*std::cos(a0+sweep), cy + radius*std::sin(a0+sweep));
    sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(end, D2D1::SizeF(radius, radius), 0.0f,
                                  D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();

    ComPtr<ID2D1StrokeStyle> cap;
    g.d2dFactory->CreateStrokeStyle(
        D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                    D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
        nullptr, 0, cap.GetAddressOf());
    auto b = Brush(col);
    g.rt->DrawGeometry(geo.Get(), b.Get(), S(4.0f), cap.Get());
}

// --- crisp vector icons (no emoji/symbol-font dependency) ------------------
static ComPtr<ID2D1StrokeStyle> RoundStroke() {
    ComPtr<ID2D1StrokeStyle> s;
    g.d2dFactory->CreateStrokeStyle(
        D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                    D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
        nullptr, 0, s.GetAddressOf());
    return s;
}

static void IconGrid(float cx, float cy, float r, const ColorF& c) {
    float sq = r * 0.82f, gp = r * 0.36f;
    float off = (sq + gp) / 2.0f, h = sq / 2.0f;
    float xs[2] = { cx - off, cx + off };
    float ys[2] = { cy - off, cy + off };
    for (int iy = 0; iy < 2; ++iy)
        for (int ix = 0; ix < 2; ++ix)
            FillRound(RectF(xs[ix]-h, ys[iy]-h, xs[ix]+h, ys[iy]+h), r*0.18f, c);
}

static void IconMagnifier(float cx, float cy, float r, const ColorF& c) {
    auto b = Brush(c); auto cap = RoundStroke();
    float gx = cx - r*0.22f, gy = cy - r*0.22f, gr = r*0.60f;
    g.rt->DrawEllipse(D2D1::Ellipse(Point2F(gx, gy), gr, gr), b.Get(), r*0.22f);
    g.rt->DrawLine(Point2F(gx + gr*0.72f, gy + gr*0.72f),
                   Point2F(cx + r*0.80f, cy + r*0.80f), b.Get(), r*0.26f, cap.Get());
}

static void IconLock(float cx, float cy, float r, const ColorF& c) {
    auto b = Brush(c); auto cap = RoundStroke();
    ComPtr<ID2D1PathGeometry> geo; g.d2dFactory->CreatePathGeometry(geo.GetAddressOf());
    ComPtr<ID2D1GeometrySink> sink; geo->Open(sink.GetAddressOf());
    float sr = r*0.46f, sy = cy - r*0.20f;
    sink->BeginFigure(Point2F(cx - sr, sy), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(Point2F(cx + sr, sy), D2D1::SizeF(sr, sr), 0.0f,
                                  D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN); sink->Close();
    g.rt->DrawGeometry(geo.Get(), b.Get(), r*0.20f, cap.Get());
    FillRound(RectF(cx - r*0.66f, cy - r*0.22f, cx + r*0.66f, cy + r*0.78f), r*0.20f, c);
}

static void IconSliders(float cx, float cy, float r, const ColorF& c) {
    auto b = Brush(c); auto cap = RoundStroke();
    float y1 = cy - r*0.5f, y2 = cy + r*0.5f;
    g.rt->DrawLine(Point2F(cx - r, y1), Point2F(cx + r, y1), b.Get(), r*0.22f, cap.Get());
    g.rt->DrawLine(Point2F(cx - r, y2), Point2F(cx + r, y2), b.Get(), r*0.22f, cap.Get());
    g.rt->FillEllipse(D2D1::Ellipse(Point2F(cx - r*0.4f, y1), r*0.27f, r*0.27f), b.Get());
    g.rt->FillEllipse(D2D1::Ellipse(Point2F(cx + r*0.4f, y2), r*0.27f, r*0.27f), b.Get());
}

static void IconBeaker(float cx, float cy, float r, const ColorF& c) {
    auto b = Brush(c); auto cap = RoundStroke();
    // beaker outline: neck down to a wider flask
    ComPtr<ID2D1PathGeometry> geo; g.d2dFactory->CreatePathGeometry(geo.GetAddressOf());
    ComPtr<ID2D1GeometrySink> sink; geo->Open(sink.GetAddressOf());
    sink->BeginFigure(Point2F(cx - r*0.35f, cy - r), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(Point2F(cx - r*0.35f, cy - r*0.15f));
    sink->AddLine(Point2F(cx - r*0.8f,  cy + r*0.85f));
    sink->AddLine(Point2F(cx + r*0.8f,  cy + r*0.85f));
    sink->AddLine(Point2F(cx + r*0.35f, cy - r*0.15f));
    sink->AddLine(Point2F(cx + r*0.35f, cy - r));
    sink->EndFigure(D2D1_FIGURE_END_OPEN); sink->Close();
    g.rt->DrawGeometry(geo.Get(), b.Get(), r*0.2f, cap.Get());
    // top rim + a "liquid" line
    g.rt->DrawLine(Point2F(cx - r*0.5f, cy - r), Point2F(cx + r*0.5f, cy - r), b.Get(), r*0.2f, cap.Get());
    g.rt->DrawLine(Point2F(cx - r*0.55f, cy + r*0.35f), Point2F(cx + r*0.55f, cy + r*0.35f), b.Get(), r*0.2f, cap.Get());
}

static void DrawNavIcon(int i, float cx, float cy, float r, const ColorF& c) {
    switch (i) {
        case 0: IconGrid(cx, cy, r, c); break;
        case 1: IconMagnifier(cx, cy, r, c); break;
        case 2: IconBeaker(cx, cy, r, c); break;
        case 3: IconLock(cx, cy, r, c); break;
        default: IconSliders(cx, cy, r, c); break;
    }
}

// A filled circle with an exclamation mark (threat badge).
static void DrawAlert(float cx, float cy, float r, const ColorF& col) {
    auto b = Brush(col);
    g.rt->FillEllipse(D2D1::Ellipse(Point2F(cx, cy), r, r), b.Get());
    auto w = Brush(ColorF(ColorF::White)); auto cap = RoundStroke();
    g.rt->DrawLine(Point2F(cx, cy - r*0.42f), Point2F(cx, cy + r*0.12f), w.Get(), r*0.18f, cap.Get());
    g.rt->FillEllipse(D2D1::Ellipse(Point2F(cx, cy + r*0.42f), r*0.11f, r*0.11f), w.Get());
}

// A pill button. Returns its rect.
static void Button(const D2D1_RECT_F& r, const std::wstring& label,
                   const ColorF& fill, const ColorF& fg, bool hover) {
    FillRound(r, S(9.0f), hover ? T.accentHi : fill);
    Text(label, g.fBtn.Get(), r, fg,
         DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

// ---------------------------------------------------------------------------
// Layout + paint
// ---------------------------------------------------------------------------
static const wchar_t* kNav[5] = { L"Dashboard", L"Scan", L"Sandbox", L"Quarantine", L"Configurações" };

static void PaintSidebar(float W, float H) {
    (void)W;
    D2D1_RECT_F side = RectF(0, 0, S(220), H);
    FillRound(side, 0, T.sidebar);

    // brand
    DrawShieldCheck(S(40), S(46), S(15), T.good);
    Text(L"GuardianAV", g.fH1.Get(), RectF(S(66), S(30), S(210), S(64)), T.text);

    // nav
    float y = S(110);
    for (int i = 0; i < 5; ++i) {
        D2D1_RECT_F r = RectF(S(14), y, S(206), y + S(44));
        g.navRect[i] = r;
        bool sel = (int)g.page == i;
        if (sel)              FillRound(r, S(10), T.navSel);
        else if (g.hoverNav == i) FillRound(r, S(10), T.cardHi);
        if (sel) FillRound(RectF(S(14), y + S(10), S(17), y + S(34)), S(2), T.accent);

        DrawNavIcon(i, S(47), y + S(22), S(9), sel ? T.accent : T.textDim);
        Text(kNav[i], g.fH2.Get(), RectF(S(60), y, S(206), y + S(44)),
             sel ? T.text : T.textDim,
             DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        y += S(50);
    }

    // footer
    Text(g.services[0].Running() || g.services[1].Running() ? L"v1.0  •  Serviços ativos" : L"v1.0  •  Engines desligadas", g.fSmall.Get(),
         RectF(S(20), H - S(40), S(210), H - S(16)), T.textDim);
}

static void PaintStatCard(float x, float y, float w, const std::wstring& num,
                          const std::wstring& label, const ColorF& numCol) {
    D2D1_RECT_F r = RectF(x, y, x + w, y + S(96));
    FillRound(r, S(12), T.card);
    Text(num, g.fNum.Get(), RectF(x + S(18), y + S(14), x + w - S(12), y + S(60)), numCol);
    Text(label, g.fNumSmall.Get(), RectF(x + S(20), y + S(62), x + w - S(12), y + S(86)), T.textDim);
}

static void PaintDashboard(float W, float H) {
    float x = S(220) + S(36);
    float top = S(34);
    auto& st = g.engine.State();
    bool scanning = g.engine.IsRunning();

    Text(L"Dashboard", g.fTitle.Get(), RectF(x, top, W - S(36), top + S(40)), T.text);
    Text(L"Your device at a glance", g.fBody.Get(),
         RectF(x, top + S(40), W - S(36), top + S(64)), T.textDim);

    // Hero card
    float hy = top + S(80);
    D2D1_RECT_F hero = RectF(x, hy, W - S(36), hy + S(168));
    FillRound(hero, S(16), T.card);

    if (!scanning) {
        DrawShieldCheck(x + S(64), hy + S(84), S(40), g.services[0].Running() ? T.good : T.textDim);
        Text(g.services[0].Running() ? L"Engine do antivírus ligada" : L"Engine do antivírus desligada", g.fH1.Get(),
             RectF(x + S(130), hy + S(44), W - S(60), hy + S(78)), T.text);
        Text(g.lastScanText, g.fBody.Get(),
             RectF(x + S(130), hy + S(84), W - S(60), hy + S(110)), T.textDim);
    } else {
        DrawSpinner(x + S(64), hy + S(84), S(34), g.animAngle, T.accent);
        std::wstring cur;
        { std::lock_guard<std::mutex> lk(st.mtx); cur = st.currentFile; }
        if (cur.size() > 58) cur = L"..." + cur.substr(cur.size() - 55);
        Text(L"Scanning…", g.fH1.Get(),
             RectF(x + S(130), hy + S(40), W - S(60), hy + S(74)), T.text);
        Text(cur, g.fSmall.Get(),
             RectF(x + S(130), hy + S(78), W - S(60), hy + S(100)), T.textDim);
        // thin animated progress strip
        D2D1_RECT_F track = RectF(x + S(130), hy + S(112), W - S(64), hy + S(118));
        FillRound(track, S(3), T.cardHi);
        float t = (std::sin(g.animAngle * 3.14159265f / 180.0f) + 1.0f) * 0.5f;
        float bw = (track.right - track.left) * 0.32f;
        float bx = track.left + ( (track.right - track.left) - bw ) * t;
        FillRound(RectF(bx, track.top, bx + bw, track.bottom), S(3), T.accent);
    }

    // Stat cards
    float sy = hy + S(188);
    float gap = S(16);
    float cw = (W - S(36) - x - 2*gap) / 3.0f;
    PaintStatCard(x, sy, cw, std::to_wstring(st.filesScanned.load()), L"Files scanned", T.text);
    PaintStatCard(x + cw + gap,     sy, cw, std::to_wstring(st.threatsFound.load()),
                  L"Threats found", st.threatsFound.load() ? T.danger : T.text);
    {
        size_t q;
        { std::lock_guard<std::mutex> lk(st.mtx); q = st.detections.size(); }
        PaintStatCard(x + 2*(cw + gap), sy, cw, std::to_wstring(q), L"Detections", T.good);
    }

    // Buttons
    float by = sy + S(118);
    g.quickBtn  = RectF(x, by, x + S(168), by + S(46));
    g.chooseBtn = RectF(x + S(184), by, x + S(184) + S(168), by + S(46));
    if (!scanning) {
        Button(g.quickBtn,  L"Quick Scan", T.accent, ColorF(ColorF::White), g.hoverBtn == 0);
        Button(g.chooseBtn, L"Choose folder…", T.cardHi, T.text, g.hoverBtn == 1);
        g.cancelBtn = RectF(0,0,0,0);
    } else {
        g.cancelBtn = RectF(x, by, x + S(168), by + S(46));
        Button(g.cancelBtn, L"Cancel scan", T.danger, ColorF(ColorF::White), g.hoverBtn == 2);

    g.quickBtn = g.chooseBtn = RectF(0,0,0,0);
    }
    (void)H;
}

static void PaintScanPage(float W, float H) {
    float x = S(220) + S(36);
    float top = S(34);
    auto& st = g.engine.State();
    bool scanning = g.engine.IsRunning();

    Text(L"Scan", g.fTitle.Get(), RectF(x, top, W - S(36), top + S(40)), T.text);
    Text(L"Choose a target and inspect detections", g.fBody.Get(),
         RectF(x, top + S(40), W - S(36), top + S(64)), T.textDim);

    float by = top + S(82);
    g.quickBtn  = RectF(x, by, x + S(168), by + S(44));
    g.chooseBtn = RectF(x + S(184), by, x + S(184) + S(168), by + S(44));
    if (!scanning) {
        Button(g.quickBtn,  L"Quick Scan", T.accent, ColorF(ColorF::White), g.hoverBtn == 0);
        Button(g.chooseBtn, L"Choose folder…", T.cardHi, T.text, g.hoverBtn == 1);
        g.cancelBtn = RectF(0,0,0,0);
    } else {
        g.cancelBtn = RectF(x, by, x + S(168), by + S(44));
        Button(g.cancelBtn, L"Cancel", T.danger, ColorF(ColorF::White), g.hoverBtn == 2);
        g.quickBtn = g.chooseBtn = RectF(0,0,0,0);
    }

    // status line
    std::wstring status = L"Scanned " + std::to_wstring(st.filesScanned.load()) +
                          L" files  •  " + std::to_wstring(st.threatsFound.load()) + L" threats, " + std::to_wstring(st.suspicious.load()) + L" suspicious, " +
                          std::to_wstring(st.inconclusive.load()) + L" inconclusive";
    Text(status, g.fH2.Get(), RectF(x, by + S(56), W - S(36), by + S(82)),
         st.threatsFound.load() ? T.danger : T.textDim);

    // detections list
    D2D1_RECT_F list = RectF(x, by + S(92), W - S(36), H - S(34));
    FillRound(list, S(12), T.card);
    std::vector<av::Detection> dets;
    { std::lock_guard<std::mutex> lk(st.mtx); dets = st.detections; }
    if (dets.empty()) {
        Text(scanning ? L"Scanning…" : (st.cancel.load() ? L"Scan cancelled; results are incomplete." :
            (st.inconclusive.load() ? L"Scan incomplete. Some files could not be analyzed." :
            (st.filesScanned.load() ? L"No signature match. Files remain unknown." : L"Choose a target to scan."))), g.fBody.Get(),
             RectF(list.left + S(18), list.top + S(16), list.right - S(16), list.top + S(44)),
             T.textDim);
    } else {
        float ry = list.top + S(12);
        for (size_t i = 0; i < dets.size() && ry < list.bottom - S(40); ++i) {
            D2D1_RECT_F row = RectF(list.left + S(10), ry, list.right - S(10), ry + S(46));
            FillRound(row, S(8), T.cardHi);
            DrawAlert(row.left + S(24), ry + S(23), S(9), T.danger);
            Text(dets[i].threat, g.fH2.Get(),
                 RectF(row.left + S(46), ry + S(6), row.right - S(120), ry + S(28)), T.text);
            std::wstring f = dets[i].file;
            if (f.size() > 64) f = L"..." + f.substr(f.size() - 61);
            Text(f, g.fSmall.Get(),
                 RectF(row.left + S(46), ry + S(25), row.right - S(120), ry + S(43)), T.textDim);
            Text(dets[i].reason, g.fSmall.Get(),
                 RectF(row.right - S(200), ry, row.right - S(110), ry + S(46)), T.danger,
                 DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if(!scanning&&!g.qRunning){auto isolate=RectF(row.right-S(102),ry+S(5),row.right-S(8),ry+S(39));Button(isolate,L"Isolar",T.cardHi,T.text,false);g.qDetections.push_back({isolate,dets[i]});}
            ry += S(52);
        }
    }
}

static std::wstring BaseName(const std::wstring& p);
static void PaintQuarantine(float W,float H){
    const float x=S(256),top=S(34);Text(L"Quarentena",g.fTitle.Get(),RectF(x,top,W-S(36),top+S(40)),T.text);
    Text(L"Cópias criptografadas; restauração sem sobrescrever arquivos",g.fBody.Get(),RectF(x,top+S(42),W-S(36),top+S(68)),T.textDim);
    const wchar_t* labels[]={L"Atualizar",L"Isolar arquivo...",L"Restaurar como...",g.qConfirm.empty()?L"Excluir do cofre":L"Confirmar exclusão",L"Histórico",L"Anterior",L"Próxima"};
    for(int i=0;i<7;++i){int row=i/4,column=i%4;const float bx=x+S(float(column*148)),by=top+S(float(80+row*46));g.qButtons[i]=RectF(bx,by,bx+S(140),by+S(38));Button(g.qButtons[i],labels[i],i==3&&!g.qConfirm.empty()?T.danger:T.cardHi,T.text,false);}
    av::services::Json entries;std::wstring message;bool auditMode=false;{std::lock_guard lock(g.qMtx);auditMode=g.qAuditMode;entries=auditMode?g.qAudit:g.qEntries;message=g.qMessage;}
    Text(g.qRunning?L"Operação em andamento...":message,g.fSmall.Get(),RectF(x,top+S(178),W-S(36),top+S(226)),T.textDim);
    const auto card=RectF(x,top+S(232),W-S(36),H-S(34));FillRound(card,S(12),T.card);g.qRows.clear();float y=card.top+S(10);g.qPageSize=size_t((std::max)(1.0f,std::ceil((card.bottom-y-S(62))/S(auditMode?60.0f:66.0f))));
    if(entries.empty())Text(L"Nenhum item carregado. Use Atualizar.",g.fBody.Get(),RectF(x+S(16),y,card.right-S(16),y+S(35)),T.textDim);
    for(size_t i=g.qOffset;i<entries.size()&&y+S(62)<card.bottom;i++){
        const auto& e=entries[i];if(auditMode){Text(av::services::Wide(e.value("action",std::string()))+L"  "+BaseName(av::services::Wide(e.value("source",std::string()))),g.fBody.Get(),RectF(x+S(16),y,card.right-S(16),y+S(55)),T.textDim);y+=S(60);continue;}auto id=e.value("id",std::string());auto r=RectF(x+S(8),y,card.right-S(8),y+S(60));FillRound(r,S(6),id==g.qSelected?T.cardHi:T.card);g.qRows.push_back({r,id});
        auto source=av::services::Wide(e.value("source",id));Text(BaseName(source),g.fH2.Get(),RectF(r.left+S(12),y+S(4),r.right-S(12),y+S(28)),T.text);
        const auto state=e.value("state",std::string("corrupt"));auto status=state=="isolated"?L"Isolado":state=="restored-copy"?L"Restaurado; cópia mantida":state=="corrupt"?L"Integridade inválida":L"Requer revisão; cópia preservada";
        Text(status,g.fSmall.Get(),RectF(r.left+S(12),y+S(30),r.right-S(12),y+S(55)),state=="corrupt"?T.danger:T.textDim);y+=S(66);
    }
}

static void PaintSettings(float W, float H) {
    const float x = S(256), top = S(34);
    Text(L"Configurações", g.fTitle.Get(), RectF(x, top, W - S(36), top + S(40)), T.text);
    Text(L"Serviços das engines", g.fBody.Get(), RectF(x, top + S(42), W - S(36), top + S(68)), T.textDim);
    for (int i = 0; i < 2; ++i) {
        const float y = top + S(float(88 + i * 120));
        const auto card = RectF(x, y, W - S(36), y + S(100));
        FillRound(card, S(12), T.card);
        const auto& state = g.services[i];
        std::wstring status;
        if (state.error) status = L"Não foi possível consultar: " + av::services::ErrorText(state.error);
        else if (!state.installed) status = L"Ainda não instalado";
        else if (state.state == SERVICE_RUNNING) status = L"Ligado";
        else if (state.state == SERVICE_STOPPED) status = L"Desligado";
        else if (state.state == SERVICE_START_PENDING) status = L"Iniciando...";
        else if (state.state == SERVICE_STOP_PENDING) status = L"Encerrando...";
        else status = L"Indisponível";
        Text(i == 0 ? L"Engine do antivírus" : L"Engine do sandbox MiniNT", g.fH2.Get(),
            RectF(x + S(18), y + S(12), card.right - S(140), y + S(38)), T.text);
        Text(status, g.fBody.Get(), RectF(x + S(18), y + S(42), card.right - S(140), y + S(65)),
            state.Running() ? T.good : T.textDim);
        Text(i == 0 ? L"Análises de arquivos e pastas" : L"Execuções isoladas e sessões gráficas preparadas", g.fSmall.Get(),
            RectF(x + S(18), y + S(70), card.right - S(18), y + S(92)), T.textDim);
        SetWindowPos(g.serviceButtons[i + 1], nullptr, int(card.right - S(122)), int(y + S(28)), int(S(104)), int(S(34)), SWP_NOACTIVATE | SWP_NOZORDER);
    }
    const float y = top + S(336);
    SetWindowPos(g.serviceButtons[0], nullptr, int(x), int(y), int(S(196)), int(S(38)), SWP_NOACTIVATE | SWP_NOZORDER);
    Text(L"Instalação inicial com autorização de administrador.\nAo fechar o app, os serviços ligados continuam disponíveis.", g.fSmall.Get(),
        RectF(x, y + S(50), W - S(36), y + S(100)), T.textDim);
    std::wstring message;
    { std::lock_guard lock(g.serviceMtx); message = g.serviceMessage; }
    Text(message, g.fSmall.Get(), RectF(x, y + S(108), W - S(36), H - S(18)), T.accentHi);
}

static std::wstring BaseName(const std::wstring& p) {
    size_t s = p.find_last_of(L"\\/");
    return (s == std::wstring::npos) ? p : p.substr(s + 1);
}

static void PaintSandbox(float W, float H) {
    const float x = S(256), top = S(34), by = top + S(90);
    const bool running = g.sbxRunning.load();
    Text(L"Sandbox MiniNT", g.fTitle.Get(), RectF(x, top, W - S(36), top + S(40)), T.text);
    Text(L"Execuções supervisionadas pelo serviço AegisSandbox", g.fBody.Get(),
        RectF(x, top + S(42), W - S(36), top + S(66)), T.textDim);
    g.sbxAnalyzeBtn = g.sbxSampleBtn = g.sbxVmBtn = g.sbxCheckpointBtn = g.sbxResumeBtn = RectF(0, 0, 0, 0);
    if (!running) {
        g.sbxAnalyzeBtn = RectF(x, by, x + S(168), by + S(44));
        g.sbxSampleBtn = RectF(x + S(184), by, x + S(344), by + S(44));
        g.sbxVmBtn = RectF(x + S(360), by, x + S(536), by + S(44));
        Button(g.sbxAnalyzeBtn, L"Analisar um arquivo...", T.accent, ColorF(ColorF::White), g.hoverBtn == 3);
        Button(g.sbxSampleBtn, L"Executar exemplo", T.cardHi, T.text, g.hoverBtn == 4);
        Button(g.sbxVmBtn, L"Bundle preparado...", T.cardHi, T.text, g.hoverBtn == 5);
    } else {
        g.sbxVmBtn = RectF(x, by, x + S(168), by + S(44));
        Button(g.sbxVmBtn, L"Cancelar", T.danger, ColorF(ColorF::White), g.hoverBtn == 5);
    }
    if(!running){g.sbxCheckpointBtn=RectF(x,by+S(52),x+S(168),by+S(92));g.sbxResumeBtn=RectF(x+S(184),by+S(52),x+S(344),by+S(92));Button(g.sbxCheckpointBtn,L"Salvar ponto...",T.cardHi,T.text,false);Button(g.sbxResumeBtn,L"Retomar ponto...",T.cardHi,T.text,false);}
    const auto card = RectF(x, by + S(110), W - S(36), H - S(34));
    FillRound(card, S(12), T.card);
    float y = card.top + S(18);
    auto line = [&](const std::wstring& text, const ColorF& color = T.textDim) {
        if(y+S(38)>card.bottom-S(12))return;Text(text, g.fBody.Get(), RectF(x + S(18), y, card.right - S(18), y + S(38)), color); y += S(40);
    };
    if (running) { line(L"Executando " + BaseName(g.sbxFile) + L"...", T.accentHi);std::string stage;{std::lock_guard lock(g.sbxMtx);stage=g.sbxStage;}line(stage=="preparing"?L"Preparando dependências...":stage=="validating"?L"Conferindo o pacote...":stage=="running"?L"Programa em execução no MiniNT...":L"Conectando ao serviço...");line(L"Cancelar encerra a sessão e seus processos.");return; }
    av::services::Json report; bool has = false;
    { std::lock_guard lock(g.sbxMtx); report = g.sbxReport; has = g.sbxHasReport; }
    if (!has) {
        line(L"O arquivo é executado dentro do MiniNT, usando WHP.");
        line(L"Selecione um EXE; o pacote é preparado com as DLLs disponíveis.");
        line(L"Salvar e retomar: janelas USER32 básicas, sem timers ativos.");
        line(L"Uma execução concluída não determina que o arquivo é seguro."); return;
    }
    const auto error = report.value("error", std::string());
    if (!error.empty()) line(av::services::Wide(error),T.danger);
    line(L"Backend: " + av::services::Wide(report.value("backend", "MiniNT")), T.text);
    line(report.value("checkpointCreated",false)?L"Ponto salvo. Use Retomar ponto e escolha recovery.json.":report.value("complete", false) ? L"Execução concluída. Veredito de malware ainda depende da análise." : L"Execução incompleta; cobertura insuficiente.", T.accentHi);
    line(L"Código de saída: " + std::to_wstring(report.value("exitCode", 0)));
    if (report.contains("output")) line(L"Relatórios: " + av::services::Wide(report.at("output").get<std::string>()));
    {
        line(L"Eventos: " + std::to_wstring(report.value("events", av::services::Json::array()).size()) +
             L"  •  Arquivos virtuais: " + std::to_wstring(report.value("virtualFiles", av::services::Json::array()).size()));
        const auto unresolved = report.value("unresolved", av::services::Json::array());
        if (!unresolved.empty()) {line(L"Dependências ausentes: " + std::to_wstring(unresolved.size()), T.danger);for(size_t i=0;i<(std::min)(unresolved.size(),size_t(3));++i)line(av::services::Wide(unresolved[i].get<std::string>()),T.danger);}
        if (report.contains("console")) line(av::services::Wide(report.at("console").get<std::string>()));
    }
}

static void OnPaint() {
    g.qRows.clear();g.qDetections.clear();for(auto& button:g.qButtons)button=RectF(0,0,0,0);
    CreateDeviceResources();
    g.rt->BeginDraw();
    g.rt->Clear(T.bg);

    RECT rc; GetClientRect(g.hwnd, &rc);
    float W = (float)(rc.right - rc.left);
    float H = (float)(rc.bottom - rc.top);

    g.quickBtn = g.chooseBtn = g.cancelBtn = g.sbxAnalyzeBtn = g.sbxSampleBtn = g.sbxVmBtn = g.sbxCheckpointBtn = g.sbxResumeBtn = RectF(0,0,0,0);
    for (const auto button : g.serviceButtons) ShowWindow(button, g.page == Page::Settings ? SW_SHOW : SW_HIDE);
    PaintSidebar(W, H);
    switch (g.page) {
        case Page::Dashboard:  PaintDashboard(W, H); break;
        case Page::Scan:       PaintScanPage(W, H); break;
        case Page::Sandbox:    PaintSandbox(W, H); break;
        case Page::Quarantine: PaintQuarantine(W, H); break;
        case Page::Settings:   PaintSettings(W, H); break;
    }

    HRESULT hr = g.rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) DiscardDeviceResources();
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
static std::wstring DefaultScanFolder() {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &p))) {
        out = p; CoTaskMemFree(p);
    }
    if (out.empty() || GetFileAttributesW(out.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wchar_t buf[MAX_PATH];
        if (GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH)) out = buf;
    }
    return out;
}

static std::wstring PickFolder() {
    std::wstring result;
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dlg.GetAddressOf()))))
        return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (SUCCEEDED(dlg->Show(g.hwnd))) {
        ComPtr<IShellItem> item;
        if (SUCCEEDED(dlg->GetResult(item.GetAddressOf()))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path; CoTaskMemFree(path);
            }
        }
    }
    return result;
}

static void StartScan(const std::wstring& folder) {
    if (folder.empty() || g.engine.IsRunning()) return;
    RefreshServices();
    if (!g.services[0].Running()) { g.page = Page::Settings; InvalidateRect(g.hwnd, nullptr, FALSE); return; }
    g.lastScanText = L"Scanning " + folder;
    g.engine.StartScan(folder,
        []() { PostMessageW(g.hwnd, WM_APP_PROGRESS, 0, 0); },
        []() { PostMessageW(g.hwnd, WM_APP_DONE, 0, 0); });
    SetTimer(g.hwnd, TIMER_ANIM, 16, nullptr);
}

static std::wstring PickFile(const wchar_t* title=L"Escolha um executável para o MiniNT") {
    std::wstring result;
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dlg.GetAddressOf()))))
        return result;
    dlg->SetTitle(title);DWORD opts = 0; dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    if (SUCCEEDED(dlg->Show(g.hwnd))) {
        ComPtr<IShellItem> item;
        if (SUCCEEDED(dlg->GetResult(item.GetAddressOf()))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path; CoTaskMemFree(path);
            }
        }
    }
    return result;
}

static void StartSandbox(const std::wstring& file, av::services::Operation operation = av::services::Operation::Detonate) {
    if (file.empty() || g.sbxRunning.load()) return;
    RefreshServices();
    if (!g.services[1].Running()) { g.page = Page::Settings; InvalidateRect(g.hwnd, nullptr, FALSE); return; }
    if (g.sbxThread.joinable()) g.sbxThread.join();
    g.sbxFile = file; g.sbxCancel = false;
    { std::lock_guard lock(g.sbxMtx); g.sbxHasReport = false; g.sbxStage.clear(); }
    g.sbxRunning = true;
    g.sbxThread = std::thread([file, operation] {
        av::services::Json report;
        try { report = av::services::Call(av::services::Kind::Sandbox,
            operation,file,[](const av::services::Json& value){if(value.value("type","")=="runtime-progress"){{std::lock_guard lock(g.sbxMtx);g.sbxStage=value.value("stage","");}PostMessageW(g.hwnd,WM_APP_PROGRESS,0,0);}}, &g.sbxCancel, 60000); }
        catch (const std::exception& error) { report = {{"error", g.sbxCancel ? "Análise cancelada; resultados incompletos." : error.what()}, {"complete", false}}; }
        { std::lock_guard lock(g.sbxMtx); g.sbxReport = std::move(report); g.sbxHasReport = true; }
        g.sbxRunning = false; PostMessageW(g.hwnd, WM_APP_SBX_DONE, 0, 0);
    });
    SetTimer(g.hwnd, TIMER_ANIM, 16, nullptr);
}
static void QuarantineAction(av::services::Operation operation,av::services::Json request=av::services::Json::object()){
    if(g.qRunning)return;RefreshServices();if(!g.services[0].Running()){std::lock_guard lock(g.qMtx);g.qMessage=L"Ligue a engine do antivírus nas configurações.";InvalidateRect(g.hwnd,nullptr,FALSE);return;}
    if(g.qThread.joinable())g.qThread.join();g.qRunning=true;g.qCancel=false;g.qConfirm.clear();{std::lock_guard lock(g.qMtx);g.qResumeOffset=request.value("before",uint64_t(0))?g.qAudit.size():0;}
    g.qThread=std::thread([operation,request]{av::services::Json result,listing;std::filesystem::path job;
        try{auto input=av::services::Executable();if(operation!=av::services::Operation::QuarantineList&&(operation!=av::services::Operation::QuarantineAudit||!request.empty())){auto dir=av::quarantine::DefaultRoot().parent_path()/L"quarantine-requests";av::scan::PrivateState(dir);job=dir/(av::scan::Token()+".json");av::scan::WriteJson(job,request);input=job;}
            result=av::services::Call(av::services::Kind::Antivirus,operation,input,{},&g.qCancel,60000);
            if(operation==av::services::Operation::QuarantineList)listing=result;
            else if(operation!=av::services::Operation::QuarantineAudit)listing=av::services::Call(av::services::Kind::Antivirus,av::services::Operation::QuarantineList,av::services::Executable(),{},&g.qCancel,60000);
        }catch(const std::exception& e){result={{"complete",false},{"error",e.what()}};}
        if(!job.empty()){std::error_code ignored;std::filesystem::remove(job,ignored);}
        {std::lock_guard lock(g.qMtx);g.qReport=result;if(listing.contains("entries"))g.qEntries=listing["entries"];if(result.contains("events")){auto page=result["events"];std::reverse(page.begin(),page.end());if(request.value("before",uint64_t(0)))for(auto& e:page)g.qAudit.push_back(std::move(e));else g.qAudit=std::move(page);g.qAuditMode=true;}else g.qAuditMode=false;if(result.contains("events"))g.qMessage=L"Histórico autenticado: "+std::to_wstring(result["events"].size())+L" ações registradas.";else g.qMessage=result.value("complete",false)?L"Operação concluída. Integridade do conteúdo é conferida antes de restaurar.":av::services::Wide(result.value("error",std::string("Operação incompleta; consulte os itens para revisar o estado.")));}
        PostMessageW(g.hwnd,WM_APP_Q_DONE,0,0);
    });InvalidateRect(g.hwnd,nullptr,FALSE);
}
static void IsolateDetection(const av::Detection& d){g.page=Page::Quarantine;QuarantineAction(av::services::Operation::QuarantineIsolate,{{"schema",2},{"source",av::services::Text(d.file)},{"sha256",d.sha256},{"threat",av::services::Text(d.threat)},{"reason",av::services::Text(d.reason)}});}
static void QuarantineButton(int index){
    using O=av::services::Operation;if(g.qRunning)return;
    if(index==0){QuarantineAction(O::QuarantineList);return;}if(index==1){auto path=PickFile(L"Escolha um arquivo para isolar; ele será removido após cópia verificada");if(!path.empty())QuarantineAction(O::QuarantineIsolate,{{"schema",2},{"source",av::services::Text(path)},{"threat","Manual"}});return;}
    if(index==4){QuarantineAction(O::QuarantineAudit);return;}if(index==5){g.qOffset=g.qOffset>=g.qPageSize?g.qOffset-g.qPageSize:0;InvalidateRect(g.hwnd,nullptr,FALSE);return;}if(index==6){uint64_t cursor=0;{std::lock_guard lock(g.qMtx);const auto size=g.qAuditMode?g.qAudit.size():g.qEntries.size();if(g.qOffset+g.qPageSize<size)g.qOffset+=g.qPageSize;else if(g.qAuditMode)cursor=g.qReport.value("nextBefore",uint64_t(0));}if(cursor)QuarantineAction(O::QuarantineAudit,{{"schema",2},{"before",cursor}});InvalidateRect(g.hwnd,nullptr,FALSE);return;}
    if(g.qSelected.empty())return;if(index==3){if(g.qConfirm!=g.qSelected){g.qConfirm=g.qSelected;{std::lock_guard lock(g.qMtx);g.qMessage=L"Confirme a exclusão permanente da cópia do cofre. Os arquivos fora dele não serão removidos.";}InvalidateRect(g.hwnd,nullptr,FALSE);return;}QuarantineAction(O::QuarantineErase,{{"schema",2},{"id",g.qSelected}});return;}
    if(index==2){ComPtr<IFileSaveDialog> dlg;if(FAILED(CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(dlg.GetAddressOf()))))return;dlg->SetTitle(L"Escolha um destino novo para restaurar; o cofre manterá uma cópia");DWORD options=0;dlg->GetOptions(&options);dlg->SetOptions((options&~FOS_OVERWRITEPROMPT)|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);std::wstring filename=L"restaurado.bin";{std::lock_guard lock(g.qMtx);for(const auto& e:g.qEntries)if(e.value("id",std::string())==g.qSelected)filename=BaseName(av::services::Wide(e.value("source",std::string("restaurado.bin"))));}dlg->SetFileName(filename.c_str());
        if(SUCCEEDED(dlg->Show(g.hwnd))){ComPtr<IShellItem> item;if(SUCCEEDED(dlg->GetResult(item.GetAddressOf()))){PWSTR text=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&text))){std::filesystem::path path=text;CoTaskMemFree(text);QuarantineAction(O::QuarantineRestore,{{"schema",2},{"id",g.qSelected},{"directory",av::services::Text(path.parent_path().wstring())},{"filename",av::services::Text(path.filename().wstring())}});}}}}
}
static void RunTestSample() {
    StartSandbox((av::services::Executable().parent_path() / L"guardian-service-sample.exe").wstring());
}
static void OpenInVm() {
    if (g.sbxRunning) { g.sbxCancel = true; return; }
    const auto folder = PickFolder();
    if (!folder.empty()) StartSandbox(folder, av::services::Operation::DesktopBundle);
}



// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
static void OnMouseMove(float x, float y) {
    int oldNav = g.hoverNav, oldBtn = g.hoverBtn;
    g.hoverNav = -1; g.hoverBtn = -1;
    for (int i = 0; i < 5; ++i) if (PtIn(g.navRect[i], x, y)) g.hoverNav = i;
    if (PtIn(g.quickBtn, x, y))       g.hoverBtn = 0;
    if (PtIn(g.chooseBtn, x, y))      g.hoverBtn = 1;
    if (PtIn(g.cancelBtn, x, y))      g.hoverBtn = 2;
    if (PtIn(g.sbxAnalyzeBtn, x, y))  g.hoverBtn = 3;
    if (PtIn(g.sbxSampleBtn, x, y))   g.hoverBtn = 4;
    if (PtIn(g.sbxVmBtn, x, y))       g.hoverBtn = 5;
    if (oldNav != g.hoverNav || oldBtn != g.hoverBtn)
        InvalidateRect(g.hwnd, nullptr, FALSE);
}

static void OnClick(float x, float y) {
    for (int i = 0; i < 5; ++i)
        if (PtIn(g.navRect[i], x, y)) { g.page = (Page)i; InvalidateRect(g.hwnd, nullptr, FALSE); return; }
    if(g.page==Page::Quarantine){
        for(const auto& item:g.qRows)if(PtIn(item.first,x,y)){g.qSelected=item.second;g.qConfirm.clear();InvalidateRect(g.hwnd,nullptr,FALSE);return;}
        for(int i=0;i<7;++i)if(PtIn(g.qButtons[i],x,y)){QuarantineButton(i);return;}
        return;
    }
    if(g.page==Page::Scan)for(const auto& item:g.qDetections)if(PtIn(item.first,x,y)){IsolateDetection(item.second);return;}
    if (PtIn(g.quickBtn, x, y))  { StartScan(DefaultScanFolder()); return; }
    if (PtIn(g.chooseBtn, x, y)) { std::wstring f = PickFolder(); if (!f.empty()) StartScan(f); return; }
    if (PtIn(g.cancelBtn, x, y)) { g.engine.Cancel(); return; }
    if (PtIn(g.sbxAnalyzeBtn, x, y)) { std::wstring f = PickFile(); if (!f.empty()) StartSandbox(f); return; }
    if (PtIn(g.sbxSampleBtn, x, y))  { RunTestSample(); return; }
    if (PtIn(g.sbxVmBtn, x, y))      { OpenInVm(); return; }
    if(PtIn(g.sbxCheckpointBtn,x,y)){const auto file=PickFile();if(!file.empty())StartSandbox(file,av::services::Operation::Checkpoint);return;}
    if(PtIn(g.sbxResumeBtn,x,y)){std::wstring last;{std::lock_guard lock(g.sbxMtx);if(g.sbxReport.value("checkpointCreated",false))last=(std::filesystem::path(av::services::Wide(g.sbxReport.at("output").get<std::string>()))/L"recovery.json").wstring();}const auto file=last.empty()?PickFile(L"Escolha o arquivo recovery.json do ponto salvo"):last;if(!file.empty())StartSandbox(file,av::services::Operation::Resume);return;}
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g.hwnd = hwnd;
        g.scale = (float)GetDpiForWindow(hwnd) / 96.0f;
        CreateDeviceIndependentResources();
        for (int i = 0; i < 3; ++i) {
            g.serviceButtons[i] = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 1, 1, hwnd, reinterpret_cast<HMENU>(INT_PTR(ID_SERVICE_INSTALL + i)), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g.serviceButtons[i], WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        }
        RefreshServices(); SetTimer(hwnd, TIMER_SERVICES, 1000, nullptr);
        return 0;

    case WM_DPICHANGED: {
        g.scale = (float)HIWORD(wp) / 96.0f;
        CreateDeviceIndependentResources(); // rebuild scaled text formats
        RECT* nr = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, nr->left, nr->top,
                     nr->right - nr->left, nr->bottom - nr->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_SIZE:
        if (g.rt) {
            g.rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = (LONG)S(820);
        mmi->ptMinTrackSize.y = (LONG)S(560);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
        OnPaint();
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:    OnMouseMove((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp)); return 0;
    case WM_LBUTTONDOWN:  OnClick((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));     return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT &&
            (g.hoverNav >= 0 || g.hoverBtn >= 0)) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
        break;

    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED && LOWORD(wp) >= ID_SERVICE_INSTALL && LOWORD(wp) <= ID_SERVICE_SANDBOX) {
            ServiceAction(LOWORD(wp)); return 0;
        }
        break;
    case WM_APP_Q_DONE:
        if(g.qThread.joinable())g.qThread.join();g.qSelected.clear();g.qConfirm.clear();g.qRunning=false;g.qOffset=g.qResumeOffset;g.qResumeOffset=0;InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_APP_SERVICE_DONE:
        if (g.serviceThread.joinable()) g.serviceThread.join();
        g.serviceBusy = false; RefreshServices(); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_TIMER:
        if (wp == TIMER_SERVICES) { RefreshServices(); InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (wp == TIMER_ANIM) {
            g.animAngle += 6.0f;
            if (g.animAngle >= 360.0f) g.animAngle -= 360.0f;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_APP_PROGRESS:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_APP_DONE: {
        g.engine.Join();
        KillTimer(hwnd, TIMER_ANIM);
        auto& st = g.engine.State();
        g.lastScanText = L"Last scan: " + std::to_wstring(st.filesScanned.load()) +
                         L" files, " + std::to_wstring(st.threatsFound.load()) + L" threats, " + std::to_wstring(st.suspicious.load()) + L" suspicious, " +
                          std::to_wstring(st.inconclusive.load()) + L" inconclusive";
        { std::lock_guard lock(st.mtx); if (!st.error.empty()) g.lastScanText = st.error; }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_APP_SBX_DONE:
        if (g.sbxThread.joinable()) g.sbxThread.join();
        KillTimer(hwnd, TIMER_ANIM);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_CLOSE:
        if (g.serviceBusy) { MessageBeep(MB_ICONINFORMATION); return 0; }
        DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        g.qCancel=true;if(g.qThread.joinable())g.qThread.join();
        KillTimer(hwnd, TIMER_SERVICES);
        g.engine.Cancel(); g.engine.Join();
        g.sbxCancel = true;
        if (g.serviceThread.joinable()) g.serviceThread.join();
        if (g.sbxThread.joinable()) g.sbxThread.join();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);



    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"GuardianAVWindow";
    RegisterClassExW(&wc);

    int w = (int)(980 * ((float)GetDpiForSystem() / 96.0f));
    int h = (int)(640 * ((float)GetDpiForSystem() / 96.0f));
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"GuardianAV",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, w, h,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (IsDialogMessageW(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    CoUninitialize();
    return 0;
}
