// 班级随机抽人 · 深色极客风 GUI（Win32 原生，零依赖，可用 mingw-w64 交叉编译）
#include <windows.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <objidl.h>

#include <string>
#include <vector>

#include "roster.h"

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(l) ((int)(short)LOWORD(l))
#define GET_Y_LPARAM(l) ((int)(short)HIWORD(l))
#endif

// ---------- 资源 ID（与 resources.rc 对应）----------
static const UINT kResIcon = 1;      // exe/标题栏图标（ctw.ico）
static const UINT kResLogoPng = 201; // 右上角徽标 PNG（icon_dark）

// ---------- 配色（深色极客风）----------
static const COLORREF C_BG = RGB(11, 14, 20);
static const COLORREF C_PANEL = RGB(17, 21, 28);
static const COLORREF C_PANEL2 = RGB(23, 28, 37);
static const COLORREF C_TEXT = RGB(230, 237, 243);
static const COLORREF C_DIM = RGB(110, 118, 129);
static const COLORREF C_ACCENT = RGB(0, 229, 160);
static const COLORREF C_ACCENT2 = RGB(124, 92, 255);
static const COLORREF C_BORDER = RGB(38, 44, 56);

static const wchar_t* kSignature = L"byHry · CTW";  // 署名，帮助里可见

// ---------- CTW 徽标（从资源加载 PNG）----------
static Gdiplus::Bitmap* g_logo = nullptr;
static ULONG_PTR g_gdipToken = 0;

static void loadLogo(HINSTANCE hInst) {
    Gdiplus::GdiplusStartupInput si;
    if (Gdiplus::GdiplusStartup(&g_gdipToken, &si, nullptr) != Gdiplus::Ok) return;
    HRSRC hr = FindResourceW(hInst, MAKEINTRESOURCEW(kResLogoPng), MAKEINTRESOURCEW(10) /*RT_RCDATA*/);
    if (!hr) return;
    HGLOBAL hg = LoadResource(hInst, hr);
    if (!hg) return;
    const void* data = LockResource(hg);
    DWORD sz = SizeofResource(hInst, hr);
    if (!data || !sz) return;
    HGLOBAL gm = GlobalAlloc(GMEM_MOVEABLE, sz);
    if (!gm) return;
    void* p = GlobalLock(gm);
    if (!p) { GlobalFree(gm); return; }
    memcpy(p, data, sz);
    GlobalUnlock(gm);
    IStream* stream = nullptr;
    if (SUCCEEDED(CreateStreamOnHGlobal(gm, TRUE, &stream))) {
        g_logo = Gdiplus::Bitmap::FromStream(stream);  // Bitmap 接管内存，stream 释放时回收
        stream->Release();
    } else {
        GlobalFree(gm);
    }
}

// ---------- 状态 ----------
static Roster g_roster;
static int g_scope = -1;  // -1 全班；>=0 指定组
static int g_sel = -1;
static int g_scroll = 0;
static PickResult g_last;
static bool g_hasLast = false;
static std::wstring g_status = L"就绪 · 空格=抽人  G=抽组  Ctrl+O=导入  H=帮助";
static int g_hover = -1;

enum {
    B_IMPORT = 1, B_SAVE, B_PICK, B_PICKGROUP, B_NOREPEAT, B_RESET,
    B_ADD, B_GROUP, B_DEL, B_HIST, B_HELP
};

struct Btn {
    int id;
    const wchar_t* label;
    RECT r;
};

static std::vector<Btn> g_buttons;
static std::vector<RECT> g_tabRects;
static RECT g_listRect, g_cardRect, g_histRect;
static HFONT g_fTitle = nullptr, g_fBody = nullptr, g_fBig = nullptr, g_fSmall = nullptr;
static HFONT g_fMono = nullptr;   // 等宽字体：只用于 seed / 编号等纯 ASCII 文本
static std::wstring g_uiFace;     // 自动探测到的中文字体名

static HFONT makeFont(int h, int weight, const std::wstring& face) {
    return CreateFontW(-h, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, face.empty() ? nullptr : face.c_str());
}

static HFONT makeMonoFont(int h, int weight) {
    return CreateFontW(-h, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                       FIXED_PITCH | FF_MODERN, L"Consolas");
}

// 探测字体是否真实存在：创建后回读实际字体名比对
static bool fontAvailable(const wchar_t* name) {
    HDC dc = GetDC(nullptr);
    if (!dc) return false;
    HFONT f = CreateFontW(-16, 0, 0, 0, 400, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, name);
    HGDIOBJ old = SelectObject(dc, f);
    wchar_t got[LF_FACESIZE] = {0};
    GetTextFaceW(dc, LF_FACESIZE, got);
    SelectObject(dc, old);
    DeleteObject(f);
    ReleaseDC(nullptr, dc);
    return _wcsicmp(got, name) == 0;
}

// 该字体是否真的含有中文字形（U+4E2D「中」）——防止选到只有名字、没有字形的空壳字体
static bool hasCJK(const std::wstring& face) {
    HDC dc = GetDC(nullptr);
    if (!dc) return false;
    HFONT f = makeFont(16, 400, face);
    HGDIOBJ old = SelectObject(dc, f);
    const wchar_t ch = 0x4E2D;  // 中
    WORD idx = 0;
    DWORD r = GetGlyphIndicesW(dc, &ch, 1, &idx, GGI_MARK_NONEXISTING_GLYPHS);
    SelectObject(dc, old);
    DeleteObject(f);
    ReleaseDC(nullptr, dc);
    return r != GDI_ERROR && idx != 0xFFFF;
}

// 按优先级挑一个「存在且真有中文字形」的界面字体；都不可用则回落 MS Shell Dlg
static std::wstring pickUIFont() {
    static const wchar_t* cands[] = {
        L"Microsoft YaHei UI", L"Microsoft YaHei", L"\u5fae\u8f6f\u96c5\u9ed1",  // 微软雅黑
        L"PingFang SC", L"Noto Sans CJK SC", L"Source Han Sans SC",
        L"SimSun", L"\u5b8b\u4f53",                                              // 宋体
        L"Segoe UI", L"Tahoma"};
    for (const wchar_t* c : cands)
        if (fontAvailable(c) && hasCJK(c)) return c;
    for (const wchar_t* c : cands)
        if (fontAvailable(c)) return c;
    return L"MS Shell Dlg";
}

// ---------- 布局 ----------
static void computeLayout(HWND hwnd) {
    RECT cr;
    GetClientRect(hwnd, &cr);
    int W = cr.right, H = cr.bottom;
    int leftW = 300;
    int pad = 14;

    g_tabRects.clear();
    int tx = pad, ty = 46 + pad, tabH = 26;
    std::vector<std::wstring> tabs{L"全班"};
    auto gs = g_roster.groupList();
    for (auto& g : gs) tabs.push_back(g);
    for (size_t i = 0; i < tabs.size(); i++) {
        int w = 24 + (int)tabs[i].size() * 9;
        if (tx + w > leftW - pad && i > 0) {
            tx = pad;
            ty += tabH + 6;
        }
        RECT r{tx, ty, tx + w, ty + tabH};
        g_tabRects.push_back(r);
        tx += w + 6;
    }
    int listTop = ty + tabH + 10;
    g_listRect = RECT{pad, listTop, leftW - pad, H - 26 - 8};

    int rx = leftW + 12;
    int rw = W - rx - pad;
    g_cardRect = RECT{rx, 46 + pad, rx + rw, 46 + pad + 200};
    int btnH = 96;
    g_histRect = RECT{rx, g_cardRect.bottom + 12, rx + rw, H - 26 - btnH - 12};

    g_buttons.clear();
    struct Def { int id; const wchar_t* label; };
    std::vector<Def> defs = {
        {B_IMPORT, L"导入名单 Ctrl+O"}, {B_SAVE, L"保存 Ctrl+S"}, {B_PICK, L"抽 1 人 空格"},
        {B_PICKGROUP, L"抽一组 G"},     {B_NOREPEAT, L"防重复 N"}, {B_RESET, L"重置 R"},
        {B_ADD, L"加人 A"},             {B_GROUP, L"改组 M"},       {B_DEL, L"删除 Del"},
        {B_HIST, L"导出记录"},          {B_HELP, L"帮助 H"},
    };
    int cols = 6;
    int bw = (rw - (cols - 1) * 8) / cols;
    int bh = 38;
    int by = H - 26 - btnH + 6;
    for (size_t i = 0; i < defs.size(); i++) {
        int c = (int)i % cols, row = (int)i / cols;
        int bx = rx + c * (bw + 8);
        int byy = by + row * (bh + 8);
        g_buttons.push_back(Btn{defs[i].id, defs[i].label, RECT{bx, byy, bx + bw, byy + bh}});
    }
}

// ---------- 绘制工具 ----------
static void fillRect(HDC dc, RECT r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void frameRect(HDC dc, RECT r, COLORREF c) {  // 自己画四边，避免 FrameRect 参数差异
    HBRUSH b = CreateSolidBrush(c);
    RECT a{r.left, r.top, r.right, r.top + 1};
    RECT d{r.left, r.bottom - 1, r.right, r.bottom};
    RECT e{r.left, r.top, r.left + 1, r.bottom};
    RECT f{r.right - 1, r.top, r.right, r.bottom};
    FillRect(dc, &a, b);
    FillRect(dc, &d, b);
    FillRect(dc, &e, b);
    FillRect(dc, &f, b);
    DeleteObject(b);
}

static void roundRect(HDC dc, RECT r, int radius, COLORREF fill, COLORREF border) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HBRUSH oldB = (HBRUSH)SelectObject(dc, br);
    HPEN oldP = (HPEN)SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldB);
    SelectObject(dc, oldP);
    DeleteObject(br);
    DeleteObject(pen);
}

static void text(HDC dc, const std::wstring& s, RECT r, COLORREF color, HFONT f, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    HFONT old = (HFONT)SelectObject(dc, f);
    RECT rr = r;
    DrawTextW(dc, s.c_str(), (int)s.size(), &rr, fmt);
    SelectObject(dc, old);
}

// ---------- 文件对话框 ----------
static bool openFileDlg(HWND hwnd, std::wstring& out, bool save) {
    wchar_t buf[MAX_PATH] = {0};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    if (save) {
        ofn.lpstrFilter = L"CSV 名单 (*.csv)\0*.csv\0所有文件\0*.*\0";
        ofn.lpstrDefExt = L"csv";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&ofn)) return false;
    } else {
        ofn.lpstrFilter = L"名单文件 (*.csv;*.txt;*.md;*.xlsx)\0*.csv;*.txt;*.md;*.xlsx\0所有文件\0*.*\0";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&ofn)) return false;
    }
    out = buf;
    return true;
}

// ---------- 输入对话框 ----------
struct DlgCtx {
    std::vector<std::wstring> labels;
    std::vector<HWND> edits;
    bool ok = false;
    bool done = false;
};

static LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    DlgCtx* ctx = (DlgCtx*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT* cs = (CREATESTRUCT*)lp;
            ctx = (DlgCtx*)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)ctx);
            int y = 16;
            for (size_t i = 0; i < ctx->labels.size(); i++) {
                HWND st = CreateWindowExW(0, L"STATIC", ctx->labels[i].c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
                                          16, y + 3, 90, 20, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
                SendMessage(st, WM_SETFONT, (WPARAM)g_fBody, TRUE);
                HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 110, y, 220, 24,
                                          hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
                SendMessage(ed, WM_SETFONT, (WPARAM)g_fBody, TRUE);
                ctx->edits.push_back(ed);
                y += 32;
            }
            HWND ok = CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      110, y + 6, 100, 28, hwnd, (HMENU)1, GetModuleHandle(nullptr), nullptr);
            HWND no = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      220, y + 6, 100, 28, hwnd, (HMENU)2, GetModuleHandle(nullptr), nullptr);
            SendMessage(ok, WM_SETFONT, (WPARAM)g_fBody, TRUE);
            SendMessage(no, WM_SETFONT, (WPARAM)g_fBody, TRUE);
            SetFocus(ctx->edits.empty() ? ok : ctx->edits[0]);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wp;
            SetBkColor(dc, C_PANEL2);
            SetTextColor(dc, C_TEXT);
            return (LRESULT)CreateSolidBrush(C_PANEL2);
        }
        case WM_COMMAND:
            if (LOWORD(wp) == 1) {
                ctx->ok = true;
                ctx->done = true;
                DestroyWindow(hwnd);
            } else if (LOWORD(wp) == 2) {
                ctx->done = true;
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool inputDialog(HWND parent, const std::wstring& title, const std::vector<std::wstring>& labels,
                        std::vector<std::wstring>& out) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DlgProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"PickerDlgCls";
    wc.hbrBackground = CreateSolidBrush(C_PANEL);
    RegisterClassW(&wc);

    DlgCtx ctx;
    ctx.labels = labels;
    int h = 70 + (int)labels.size() * 32 + 44;
    HWND dlgHandle = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PickerDlgCls", title.c_str(),
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               CW_USEDEFAULT, CW_USEDEFAULT, 360, h, parent, nullptr,
                               GetModuleHandle(nullptr), &ctx);
    (void)dlgHandle;
    EnableWindow(parent, FALSE);
    MSG msg;
    while (!ctx.done && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (ctx.ok) {
        out.clear();
        for (HWND e : ctx.edits) {
            wchar_t buf[256] = {0};
            GetWindowTextW(e, buf, 256);
            out.push_back(trim(std::wstring(buf)));
        }
        return true;
    }
    return false;
}

// ---------- 弹窗：给系统 MessageBox 也套上中文字体 ----------
static HHOOK g_cbtHook = nullptr;

static BOOL CALLBACK SetFontEnumProc(HWND c, LPARAM l) {
    SendMessageW(c, WM_SETFONT, (WPARAM)l, TRUE);
    return TRUE;
}
static LRESULT CALLBACK CbtProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HCBT_ACTIVATE) {
        HWND h = (HWND)wp;
        SendMessageW(h, WM_SETFONT, (WPARAM)g_fBody, TRUE);
        EnumChildWindows(h, SetFontEnumProc, (LPARAM)g_fBody);
        UnhookWindowsHookEx(g_cbtHook);
        g_cbtHook = nullptr;
    }
    return 0;
}
static int msgBox(HWND hwnd, const std::wstring& body, const std::wstring& title, UINT flags) {
    g_cbtHook = SetWindowsHookExW(WH_CBT, CbtProc, nullptr, GetCurrentThreadId());
    int r = MessageBoxW(hwnd, body.c_str(), title.c_str(), flags);
    if (g_cbtHook) {
        UnhookWindowsHookEx(g_cbtHook);
        g_cbtHook = nullptr;
    }
    return r;
}

// ---------- 帮助窗口（自绘 + 可滚动，字体完全可控） ----------
static HWND g_helpWnd = nullptr;
static std::wstring g_helpText;
static int g_helpScroll = 0;
static int g_helpMax = 0;

static LRESULT CALLBACK HelpProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT cr;
            GetClientRect(hwnd, &cr);
            HBRUSH bg = CreateSolidBrush(C_BG);
            FillRect(dc, &cr, bg);
            DeleteObject(bg);
            SetBkMode(dc, TRANSPARENT);

            RECT tr{20, 14, cr.right - 20, 46};
            HGDIOBJ of = SelectObject(dc, g_fTitle);
            SetTextColor(dc, C_ACCENT);
            DrawTextW(dc, L"\u5e2e\u52a9 / help", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);  // 帮助 / help

            RECT lineR{20, 46, cr.right - 20, 48};
            HBRUSH lb = CreateSolidBrush(C_BORDER);
            FillRect(dc, &lineR, lb);
            DeleteObject(lb);

            POINT org{0, 0};
            SetViewportOrgEx(dc, 0, -g_helpScroll, &org);
            RECT body{20, 62 + g_helpScroll, cr.right - 20, 62 + g_helpScroll + 4000};
            SelectObject(dc, g_fBody);
            SetTextColor(dc, C_TEXT);
            DrawTextW(dc, g_helpText.c_str(), -1, &body, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            SetViewportOrgEx(dc, org.x, org.y, nullptr);
            SelectObject(dc, of);

            RECT fr{0, cr.bottom - 34, cr.right, cr.bottom};
            HBRUSH fb = CreateSolidBrush(C_PANEL);
            FillRect(dc, &fr, fb);
            DeleteObject(fb);
            SelectObject(dc, g_fSmall);
            SetTextColor(dc, C_DIM);
            DrawTextW(dc, L"Esc / \u70b9\u51fb\u4efb\u610f\u5904\u5173\u95ed\u3000\u00b7\u3000\u6eda\u8f6e\u7ffb\u9875", -1,
                      &fr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);  // Esc / 点击任意处关闭 · 滚轮翻页
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            g_helpScroll -= (delta / WHEEL_DELTA) * 40;
            RECT cr;
            GetClientRect(hwnd, &cr);
            int viewH = cr.bottom - 62 - 34;
            int maxS = g_helpMax > viewH ? g_helpMax - viewH : 0;
            if (g_helpScroll < 0) g_helpScroll = 0;
            if (g_helpScroll > maxS) g_helpScroll = maxS;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE || wp == VK_RETURN || wp == 'H') {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            g_helpWnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void showHelp(HWND parent, const std::wstring& body) {
    g_helpText = body;
    g_helpScroll = 0;
    if (g_helpWnd) {
        SetForegroundWindow(g_helpWnd);
        InvalidateRect(g_helpWnd, nullptr, FALSE);
        return;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = HelpProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"PickerHelpCls";
    wc.hbrBackground = CreateSolidBrush(C_BG);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    g_helpWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PickerHelpCls", L"\u5e2e\u52a9 / help",  // 帮助 / help
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 680, 560, parent, nullptr,
                                GetModuleHandle(nullptr), nullptr);
    if (!g_helpWnd) {
        msgBox(parent, body, L"\u5e2e\u52a9 / help", MB_OK | MB_ICONINFORMATION);
        return;
    }
    SendMessageW(g_helpWnd, WM_SETFONT, (WPARAM)g_fBody, TRUE);
    // 量一下文本实际高度，供滚动用
    HDC dc = GetDC(g_helpWnd);
    HGDIOBJ of = SelectObject(dc, g_fBody);
    RECT mr{20, 62, 660, 62 + 4000};
    DrawTextW(dc, g_helpText.c_str(), -1, &mr, DT_LEFT | DT_WORDBREAK | DT_CALCRECT);
    g_helpMax = mr.bottom - mr.top;
    SelectObject(dc, of);
    ReleaseDC(g_helpWnd, dc);
}

// ---------- 自绘确认框（不依赖系统 MessageBox，中文渲染完全可控） ----------
struct ConfirmCtx {
    std::wstring body, b1, b2;
    int result = 0;
    bool done = false;
    int hover = 0;
};
static ConfirmCtx g_conf;
static HWND g_confWnd = nullptr;
static RECT g_cb1{0, 0, 0, 0}, g_cb2{0, 0, 0, 0};

static LRESULT CALLBACK ConfirmProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            RECT cr;
            GetClientRect(hwnd, &cr);
            int bw = 130, bh = 34, pad = 22;
            g_cb2 = RECT{cr.right - pad - bw, cr.bottom - pad - bh, cr.right - pad, cr.bottom - pad};
            g_cb1 = RECT{g_cb2.left - 10 - bw, g_cb2.top, g_cb2.left - 10, g_cb2.bottom};
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT cr;
            GetClientRect(hwnd, &cr);
            SetBkMode(dc, TRANSPARENT);

            SelectObject(dc, g_fBody);
            SetTextColor(dc, C_TEXT);
            RECT br{24, 20, cr.right - 24, g_cb1.top - 14};
            DrawTextW(dc, g_conf.body.c_str(), -1, &br, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

            RECT lr{0, g_cb1.top - 12, cr.right, g_cb1.top - 11};
            HBRUSH lb = CreateSolidBrush(C_BORDER);
            FillRect(dc, &lr, lb);
            DeleteObject(lb);

            auto drawBtn = [&](RECT r, const std::wstring& label, bool hot) {
                HBRUSH fb = CreateSolidBrush(hot ? RGB(26, 34, 46) : C_PANEL2);
                FillRect(dc, &r, fb);
                DeleteObject(fb);
                SelectObject(dc, GetStockObject(NULL_BRUSH));
                HPEN p = CreatePen(PS_SOLID, 1, hot ? C_ACCENT : C_BORDER);
                HGDIOBJ op = SelectObject(dc, p);
                RoundRect(dc, r.left, r.top, r.right, r.bottom, 6, 6);
                SelectObject(dc, op);
                DeleteObject(p);
                SelectObject(dc, g_fBody);
                SetTextColor(dc, hot ? C_ACCENT : C_TEXT);
                DrawTextW(dc, label.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            };
            drawBtn(g_cb1, g_conf.b1, g_conf.hover == 1);
            drawBtn(g_cb2, g_conf.b2, g_conf.hover == 2);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int h = PtInRect(&g_cb1, pt) ? 1 : (PtInRect(&g_cb2, pt) ? 2 : 0);
            if (h != g_conf.hover) {
                g_conf.hover = h;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (PtInRect(&g_cb1, pt)) g_conf.result = 1;
            else if (PtInRect(&g_cb2, pt)) g_conf.result = 2;
            else return 0;
            g_conf.done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                g_conf.result = 0;
                g_conf.done = true;
                DestroyWindow(hwnd);
            } else if (wp == '1' || wp == VK_NUMPAD1) {
                g_conf.result = 1;
                g_conf.done = true;
                DestroyWindow(hwnd);
            } else if (wp == '2' || wp == VK_NUMPAD2) {
                g_conf.result = 2;
                g_conf.done = true;
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_CLOSE:
            g_conf.done = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            g_conf.done = true;
            g_confWnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// 返回 1 = 第一个按钮，2 = 第二个按钮，0 = 取消
static int confirmDialog(HWND parent, const std::wstring& title, const std::wstring& body,
                         const std::wstring& b1, const std::wstring& b2) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = ConfirmProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"PickerConfirmCls";
    wc.hbrBackground = CreateSolidBrush(C_PANEL);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    g_conf = ConfirmCtx{};
    g_conf.body = body;
    g_conf.b1 = b1;
    g_conf.b2 = b2;
    g_confWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PickerConfirmCls", title.c_str(),
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 460, 210, parent, nullptr,
                                GetModuleHandle(nullptr), nullptr);
    if (!g_confWnd) return 1;  // 万一创建失败，按第一个按钮处理
    SendMessageW(g_confWnd, WM_SETFONT, (WPARAM)g_fBody, TRUE);
    SetForegroundWindow(g_confWnd);

    MSG msg;
    while (!g_conf.done && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_confWnd) {
        DestroyWindow(g_confWnd);
        g_confWnd = nullptr;
    }
    return g_conf.result;
}

// ---------- 动作 ----------
static void doImport(HWND hwnd) {
    std::wstring path;
    if (!openFileDlg(hwnd, path, false)) return;
    int ret = confirmDialog(hwnd, L"\u5bfc\u5165\u65b9\u5f0f",  // 导入方式
                            L"已经有一份名单在用了。这份要怎么用？\n\n"
                            L"覆盖 —— 丢掉旧名单，只留这份\n"
                            L"追加 —— 保留旧名单，把这份接到后面\n\n"
                            L"（键盘 1 / 2 也可选，Esc 取消）",
                            L"\u8986\u76d6", L"\u8ffd\u52a0");  // 覆盖 / 追加
    if (ret == 0) return;
    std::wstring msg;
    if (g_roster.importFile(wide_to_utf8(path), ret == 2, msg)) {
        g_scope = -1;
        g_sel = -1;
        g_scroll = 0;
        g_status = msg + L" · 按空格开抽";
    } else {
        g_status = L"导入失败：" + msg;
    }
    computeLayout(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void doSave(HWND hwnd) {
    std::wstring path;
    if (!openFileDlg(hwnd, path, true)) return;
    std::wstring msg;
    g_roster.exportCsv(wide_to_utf8(path), msg);
    g_status = msg;
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void doPick(HWND hwnd) {
    PickResult r = g_roster.pick(g_scope);
    if (!r.ok) {
        g_status = L"当前范围内没有可抽的人（先导入名单）";
    } else {
        g_last = r;
        g_hasLast = true;
        g_status = L"抽中：" + displayName(r.person) + (r.person.group.empty() ? L"" : L" [" + r.person.group + L"]");
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void doPickGroup(HWND hwnd) {
    PickResult r = g_roster.pickGroup();
    if (!r.ok) {
        g_status = L"还没有分组信息";
    } else {
        g_last = r;
        g_hasLast = true;
        g_status = L"抽中组：" + r.person.group;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void doAdd(HWND hwnd) {
    std::vector<std::wstring> vals;
    if (!inputDialog(hwnd, L"添加成员", {L"姓名", L"英文名(可空)", L"组别", L"学号(可空)"}, vals)) return;
    if (vals.size() < 3 || vals[0].empty()) return;
    Person p{vals[0], vals.size() > 1 ? vals[1] : L"", vals[2], vals.size() > 3 ? vals[3] : L""};
    g_roster.add(p);
    g_status = L"已添加：" + displayName(p) + L" [" + p.group + L"]";
    computeLayout(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void doChangeGroup(HWND hwnd) {
    if (g_sel < 0 || g_sel >= (int)g_roster.people.size()) {
        g_status = L"先在左侧点选一个人";
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    std::vector<std::wstring> vals;
    if (!inputDialog(hwnd, L"变更分组", {L"目标组名"}, vals)) return;
    if (vals.empty() || vals[0].empty()) return;
    g_roster.setGroup(g_sel, vals[0]);
    g_status = L"已将 " + displayName(g_roster.people[g_sel]) + L" 移到 " + vals[0];
    computeLayout(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void doDelete(HWND hwnd) {
    if (g_sel < 0 || g_sel >= (int)g_roster.people.size()) return;
    std::wstring n = displayName(g_roster.people[g_sel]);
    g_roster.removeAt(g_sel);
    g_sel = -1;
    g_status = L"已删除：" + n;
    computeLayout(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void doExportHistory(HWND hwnd) {
    std::wstring path;
    if (!openFileDlg(hwnd, path, true)) return;
    std::wstring msg;
    g_roster.exportHistory(wide_to_utf8(path), msg);
    g_status = msg;
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void doHelp(HWND hwnd) {
    std::wstring s =
        L"班级随机抽人工具\n"
        L"----------------------------\n"
        L"快捷键：\n"
        L"  空格      随机抽 1 人\n"
        L"  G         随机抽 1 个组\n"
        L"  N         开关「防重复」（本轮不重名，抽完自动重置）\n"
        L"  R         重置已抽记录\n"
        L"  A         添加成员\n"
        L"  M         变更所选成员的分组\n"
        L"  Del       删除所选成员\n"
        L"  Ctrl+O    导入名单（csv/txt/md/xlsx）\n"
        L"  Ctrl+S    保存名单为 csv\n"
        L"  H         本帮助\n\n"
        L"名单格式（推荐 csv，UTF-8）：\n"
        L"  姓名,组别,学号\n"
        L"  张三,一组,20260101\n"
        L"  · 首行可省略，程序会自动识别表头\n"
        L"  · .xlsx 直接读取（内置解压，无需额外依赖）\n"
        L"  · .md 支持 markdown 表格\n\n"
        L"随机源：Windows 系统级密码学随机数（BCryptGenRandom）+ 拒绝采样，\n"
        L"消除取模偏差；每次抽取记录 seed，可导出留证。\n\n"
        L"界面字体：" + g_uiFace + L"\n\n" +
        std::wstring(kSignature);
    showHelp(hwnd, s);
}

// ---------- 主窗口绘制 ----------
static void paint(HWND hwnd) {
    RECT cr;
    GetClientRect(hwnd, &cr);
    int W = cr.right, H = cr.bottom;
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    HDC dc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
    HBITMAP old = (HBITMAP)SelectObject(dc, bmp);

    fillRect(dc, cr, C_BG);

    // 顶栏
    RECT hdr{0, 0, W, 46};
    fillRect(dc, hdr, C_PANEL);
    text(dc, L"RANDOM PICKER", RECT{16, 0, 300, 46}, C_ACCENT, g_fTitle);
    text(dc, L"班级随机抽人", RECT{150, 0, 400, 46}, C_DIM, g_fBody);
    text(dc, L"byHry", RECT{W - 130, 0, W - 70, 46}, RGB(70, 78, 92), g_fSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    // CTW 标（真徽标，PNG 带透明，GDI+ 绘制）
    if (g_logo) {
        Gdiplus::Graphics gx(dc);
        gx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        int lh = 36, lw = (int)(lh * ((double)g_logo->GetWidth() / g_logo->GetHeight()));
        gx.DrawImage(g_logo, W - 16 - lw, (46 - lh) / 2, lw, lh);
    }

    // 左栏
    RECT left{0, 46, 300, H - 26};
    fillRect(dc, left, C_PANEL);
    frameRect(dc, left, C_BORDER);

    auto gs = g_roster.groupList();
    for (size_t i = 0; i < g_tabRects.size(); i++) {
        bool active = ((int)i - 1 == g_scope) || (i == 0 && g_scope == -1);
        RECT r = g_tabRects[i];
        roundRect(dc, r, 4, active ? RGB(0, 60, 45) : C_PANEL2, active ? C_ACCENT : C_BORDER);
        text(dc, i == 0 ? L"全班" : gs[i - 1], r, active ? C_ACCENT : C_DIM, g_fSmall,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // 名单列表
    int rowH = 26;
    int top = g_listRect.top;
    int bottom = g_listRect.bottom;
    int visible = (bottom - top) / rowH;
    int total = (int)g_roster.people.size();
    int maxScroll = total > visible ? total - visible : 0;
    if (g_scroll > maxScroll) g_scroll = maxScroll;
    if (g_scroll < 0) g_scroll = 0;

    for (int k = 0; k < visible && g_scroll + k < total; k++) {
        int idx = g_scroll + k;
        const Person& p = g_roster.people[idx];
        RECT r{g_listRect.left, top + k * rowH, g_listRect.right, top + (k + 1) * rowH};
        bool drawnFlag = g_roster.drawn.count(p.name) > 0;
        bool inScope = g_scope < 0 || (g_scope < (int)gs.size() && p.group == gs[g_scope]);
        if (idx == g_sel) {
            fillRect(dc, r, RGB(28, 35, 47));
            frameRect(dc, r, C_ACCENT2);
        } else if (!inScope) {
            fillRect(dc, r, RGB(15, 18, 24));
        }
        COLORREF nc = drawnFlag ? RGB(80, 88, 100) : (inScope ? C_TEXT : RGB(90, 98, 110));
        wchar_t num[16];
        swprintf(num, 16, L"%02d", idx + 1);
        RECT nr{r.left + 6, r.top, r.left + 30, r.bottom};
        text(dc, num, nr, C_DIM, g_fSmall);
        RECT nameR{r.left + 32, r.top, r.right - 70, r.bottom};
        text(dc, displayName(p), nameR, nc, g_fBody);
        RECT grpR{r.right - 68, r.top, r.right - 4, r.bottom};
        text(dc, p.group.empty() ? L"-" : p.group, grpR, drawnFlag ? RGB(70, 78, 90) : C_ACCENT, g_fSmall,
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    if (total == 0) {
        RECT r{g_listRect.left, top + 8, g_listRect.right, top + 40};
        text(dc, L"（名单为空，Ctrl+O 导入）", r, C_DIM, g_fBody);
    }

    // 结果卡
    roundRect(dc, g_cardRect, 10, C_PANEL, g_hasLast ? C_ACCENT : C_BORDER);
    if (g_hasLast) {
        RECT br{g_cardRect.left + 20, g_cardRect.top + 18, g_cardRect.right - 20, g_cardRect.top + 48};
        text(dc, g_last.isGroup ? L"抽中的组" : L"抽中的人", br, C_DIM, g_fSmall);
        RECT nr{g_cardRect.left + 20, g_cardRect.top + 44, g_cardRect.right - 20, g_cardRect.top + 120};
        text(dc, displayName(g_last.person), nr, C_ACCENT, g_fBig, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT gr{g_cardRect.left + 20, g_cardRect.top + 120, g_cardRect.right - 20, g_cardRect.top + 150};
        text(dc, g_last.isGroup ? L"" : (L"组别：" + g_last.person.group), gr, C_TEXT, g_fBody);
        RECT sr{g_cardRect.left + 20, g_cardRect.top + 152, g_cardRect.right - 20, g_cardRect.top + 180};
        text(dc, L"seed " + g_last.seed, sr, RGB(80, 88, 100), g_fSmall);
    } else {
        RECT br{g_cardRect.left + 20, g_cardRect.top + 20, g_cardRect.right - 20, g_cardRect.bottom - 20};
        text(dc, L"按 空格 开始抽取", br, C_DIM, g_fBody);
    }

    // 历史
    roundRect(dc, g_histRect, 10, C_PANEL, C_BORDER);
    RECT hr{g_histRect.left + 14, g_histRect.top + 10, g_histRect.right - 14, g_histRect.top + 30};
    text(dc, L"抽取记录（最近 6 条，可导出）", hr, C_DIM, g_fSmall);
    int n = (int)g_roster.history.size();
    for (int i = 0; i < 6 && i < n; i++) {
        RECT r{g_histRect.left + 14, g_histRect.top + 34 + i * 22, g_histRect.right - 14,
               g_histRect.top + 56 + i * 22};
        text(dc, g_roster.history[n - 1 - i], r, RGB(150, 158, 170), g_fSmall);
    }

    // 按钮
    for (const Btn& b : g_buttons) {
        bool on = (b.id == B_NOREPEAT && g_roster.noRepeat);
        bool hover = (g_hover == b.id);
        COLORREF bg = on ? RGB(0, 60, 45) : (hover ? C_PANEL2 : RGB(21, 26, 34));
        COLORREF bd = on ? C_ACCENT : (hover ? RGB(70, 78, 92) : C_BORDER);
        roundRect(dc, b.r, 6, bg, bd);
        text(dc, b.label, b.r, on ? C_ACCENT : (hover ? C_TEXT : RGB(180, 188, 200)), g_fSmall,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // 状态栏
    RECT sb{0, H - 26, W, H};
    fillRect(dc, sb, C_PANEL);
    frameRect(dc, sb, C_BORDER);
    wchar_t cnt[128];
    swprintf(cnt, 128, L"共 %d 人 · %d 组 · 可抽 %d 人 · 防重复:%s", (int)g_roster.people.size(),
             (int)g_roster.groupList().size(), g_roster.countInScope(g_scope),
             g_roster.noRepeat ? L"开" : L"关");
    text(dc, cnt, RECT{W - 330, H - 26, W - 12, H}, C_DIM, g_fSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    text(dc, g_status, RECT{16, H - 26, W - 340, H}, RGB(160, 168, 180), g_fSmall);

    BitBlt(hdc, 0, 0, W, H, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

// ---------- 主窗口过程 ----------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            computeLayout(hwnd);
            return 0;
        case WM_SIZE:
            computeLayout(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            paint(hwnd);
            return 0;
        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int hover = -1;
            for (const Btn& b : g_buttons) {
                if (x >= b.r.left && x <= b.r.right && y >= b.r.top && y <= b.r.bottom) hover = b.id;
            }
            if (hover != g_hover) {
                g_hover = hover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int d = GET_WHEEL_DELTA_WPARAM(wp);
            g_scroll += (d > 0 ? -3 : 3);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            for (const Btn& b : g_buttons) {
                if (x >= b.r.left && x <= b.r.right && y >= b.r.top && y <= b.r.bottom) {
                    switch (b.id) {
                        case B_IMPORT: doImport(hwnd); return 0;
                        case B_SAVE: doSave(hwnd); return 0;
                        case B_PICK: doPick(hwnd); return 0;
                        case B_PICKGROUP: doPickGroup(hwnd); return 0;
                        case B_NOREPEAT:
                            g_roster.noRepeat = !g_roster.noRepeat;
                            g_status = g_roster.noRepeat ? L"防重复：开（本轮不重名）" : L"防重复：关";
                            InvalidateRect(hwnd, nullptr, FALSE);
                            return 0;
                        case B_RESET:
                            g_roster.resetDrawn();
                            g_status = L"已重置抽取记录";
                            InvalidateRect(hwnd, nullptr, FALSE);
                            return 0;
                        case B_ADD: doAdd(hwnd); return 0;
                        case B_GROUP: doChangeGroup(hwnd); return 0;
                        case B_DEL: doDelete(hwnd); return 0;
                        case B_HIST: doExportHistory(hwnd); return 0;
                        case B_HELP: doHelp(hwnd); return 0;
                    }
                }
            }
            for (size_t i = 0; i < g_tabRects.size(); i++) {
                RECT r = g_tabRects[i];
                if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
                    g_scope = (int)i - 1;
                    g_status = (g_scope == -1) ? L"范围：全班" : L"范围：" + g_roster.groupList()[g_scope];
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            if (x >= g_listRect.left && x <= g_listRect.right && y >= g_listRect.top && y <= g_listRect.bottom) {
                int rowH = 26;
                int idx = g_scroll + (y - g_listRect.top) / rowH;
                g_sel = (idx >= 0 && idx < (int)g_roster.people.size()) ? idx : -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_KEYDOWN: {
            bool ctrl = GetKeyState(VK_CONTROL) < 0;
            switch (wp) {
                case VK_SPACE: doPick(hwnd); return 0;
                case 'G': doPickGroup(hwnd); return 0;
                case 'N':
                    g_roster.noRepeat = !g_roster.noRepeat;
                    g_status = g_roster.noRepeat ? L"防重复：开" : L"防重复：关";
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case 'R':
                    g_roster.resetDrawn();
                    g_status = L"已重置抽取记录";
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case 'A': doAdd(hwnd); return 0;
                case 'M': doChangeGroup(hwnd); return 0;
                case 'H': doHelp(hwnd); return 0;
                case VK_DELETE: doDelete(hwnd); return 0;
                case 'O':
                    if (ctrl) doImport(hwnd);
                    return 0;
                case 'S':
                    if (ctrl) doSave(hwnd);
                    return 0;
                case VK_ESCAPE:
                    DestroyWindow(hwnd);
                    return 0;
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    loadLogo(hInst);
    g_uiFace = pickUIFont();
    g_fTitle = makeFont(16, 500, g_uiFace);
    g_fBody = makeFont(15, 400, g_uiFace);
    g_fBig = makeFont(46, 500, g_uiFace);
    g_fSmall = makeFont(13, 400, g_uiFace);
    g_fMono = makeMonoFont(13, 400);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"PickerMainCls";
    wc.hbrBackground = CreateSolidBrush(C_BG);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(kResIcon), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"PickerMainCls", L"随机抽人 · byHry · CTW",
                                WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, CW_USEDEFAULT, CW_USEDEFAULT,
                                980, 660, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    SendMessageW(hwnd, WM_SETICON, ICON_BIG,
                 (LPARAM)LoadImageW(hInst, MAKEINTRESOURCEW(kResIcon), IMAGE_ICON, 32, 32, 0));
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
                 (LPARAM)LoadImageW(hInst, MAKEINTRESOURCEW(kResIcon), IMAGE_ICON, 16, 16, 0));
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_logo) { delete g_logo; g_logo = nullptr; }
    if (g_gdipToken) Gdiplus::GdiplusShutdown(g_gdipToken);
    return (int)msg.wParam;
}
