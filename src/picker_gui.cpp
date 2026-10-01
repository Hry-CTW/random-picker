// 班级随机抽人 · GUI（Win32 原生，零依赖，mingw-w64 交叉编译）
// v2：两套主题（极客深色 / ins 浅色）、抽 N 人、按组抽选、6 组结构、老师现场可维护
#include <windows.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <objidl.h>

#include <algorithm>
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

// ---------- 主题 ----------
struct Pal {
    COLORREF bg, panel, panel2, text, dim, dim2;
    COLORREF accent, accent2, border, sel, rowAlt, onAccent, hot;
    int radius;
};

// 极客风：深色 + 薄荷绿
static const Pal kGeek = {
    RGB(11, 14, 20),   RGB(17, 21, 28),   RGB(23, 28, 37),   // bg panel panel2
    RGB(230, 237, 243), RGB(110, 118, 129), RGB(150, 158, 170), // text dim dim2
    RGB(0, 229, 160),  RGB(124, 92, 255),  RGB(38, 44, 56),  // accent accent2 border
    RGB(28, 35, 47),   RGB(15, 18, 24),    RGB(4, 20, 15),   // sel rowAlt onAccent
    RGB(26, 34, 46),   6                                     // hot radius
};

// ins 风：浅色 + 圆角 + 低饱和蓝紫（不撞色，主按钮靠面积和留白突出）
static const Pal kIns = {
    RGB(247, 247, 249), RGB(255, 255, 255), RGB(240, 240, 244),
    RGB(27, 27, 31),    RGB(138, 138, 148), RGB(120, 120, 132),
    RGB(76, 111, 255),  RGB(255, 106, 148), RGB(226, 226, 234),
    RGB(233, 238, 255), RGB(250, 250, 252), RGB(255, 255, 255),
    RGB(240, 243, 255),  14
};

static int g_theme = 0;  // 0=极客 1=ins
static Pal g_p = kGeek;
static const wchar_t* themeName() { return g_theme ? L"ins" : L"极客"; }
static void applyTheme(int t) { g_theme = t ? 1 : 0; g_p = g_theme ? kIns : kGeek; }

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
static int g_pickN = 2;               // 抽 N 人的 N
static int g_hover = -1;
static int g_hoverBig = -1;
static int g_hoverTab = -1;

struct LastPick {
    bool ok = false;
    bool isGroup = false;
    std::vector<Person> list;
    std::wstring seed;
    std::wstring where;
};
static LastPick g_last;
static std::wstring g_status = L"就绪 · 空格=抽 1 人  G=抽组  T=换风格  Ctrl+O=导入  H=帮助";

// 误删撤销：只保留最近一次删除，够老师救急，不搞多级栈
static bool g_hasUndo = false;
static Person g_undoPerson;
static int g_undoIndex = -1;

enum {
    B_IMPORT = 1, B_SAVE, B_NOREPEAT, B_RESET, B_ADD, B_MOVE, B_RENAME,
    B_DEL, B_UNDO, B_INIT6, B_HIST, B_HELP,
    BIG_PICK1 = 100, BIG_PICKGROUP, BIG_PICKN, BIG_NDROP, BIG_PICKALL = 110
};

struct Btn {
    int id;
    const wchar_t* label;
    RECT r;
};

static std::vector<Btn> g_buttons;    // 底部一排常规按钮
static std::vector<Btn> g_big;        // 顶部主按钮（抽 1 人 / 抽 1 组 / 抽 N 人）
static std::vector<RECT> g_tabRects;
static RECT g_listRect, g_cardRect, g_histRect, g_nEditRect, g_nDropRect, g_themeRect;
static HWND g_hEditN = nullptr;

static HFONT g_fTitle = nullptr, g_fBody = nullptr, g_fBig = nullptr;
static HFONT g_fMid = nullptr, g_fSmall = nullptr, g_fMono = nullptr;
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
    int leftW = 320;
    int pad = 14;

    // 左栏：组页签 + 名单
    g_tabRects.clear();
    int tx = pad, ty = 46 + pad, tabH = 28;
    std::vector<std::wstring> tabs{L"全班"};
    auto gs = g_roster.groupList();
    for (auto& g : gs) tabs.push_back(g);
    for (size_t i = 0; i < tabs.size(); i++) {
        int w = 26 + (int)tabs[i].size() * 10;
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

    // 右栏
    int rx = leftW + 12;
    int rw = W - rx - pad;

    // 主按钮行：抽 1 人（大）+ 抽 1 组
    g_big.clear();
    int bigY = 46 + pad, bigH = 58;
    int w1 = (int)(rw * 0.58), w2 = rw - w1 - 10;
    g_big.push_back(Btn{BIG_PICK1, L"抽 1 人  空格", RECT{rx, bigY, rx + w1, bigY + bigH}});
    g_big.push_back(Btn{BIG_PICKGROUP, L"抽 1 组  G", RECT{rx + w1 + 10, bigY, rx + rw, bigY + bigH}});

    // 第二行：抽 N 人（数字框 + 下拉 + 按钮）
    int ny = bigY + bigH + 10, nh = 40;
    g_nEditRect = RECT{rx, ny, rx + 66, ny + nh};
    g_nDropRect = RECT{rx + 66 + 6, ny, rx + 66 + 6 + 34, ny + nh};
    g_big.push_back(Btn{BIG_NDROP, L"\u25be", g_nDropRect});  // ▾
    int nbx = rx + 66 + 6 + 34 + 8;
    RECT pickNR{rx + 66 + 6 + 34 + 8, ny, rx + rw - 96, ny + nh};
    g_big.push_back(Btn{BIG_PICKN, L"\u62bd N \u4eba", pickNR});  // 抽 N 人
    RECT allR{rx + rw - 88, ny, rx + rw, ny + nh};
    g_big.push_back(Btn{BIG_PICKALL, L"\u5168\u90e8", allR});   // 全部

    // 结果卡
    int cardTop = ny + nh + 12;
    g_cardRect = RECT{rx, cardTop, rx + rw, cardTop + 216};

    // 历史卡 + 底部按钮
    int rows = 2, bh = 36;
    int btnAreaH = rows * bh + (rows - 1) * 8;
    g_histRect = RECT{rx, g_cardRect.bottom + 12, rx + rw, H - 26 - btnAreaH - 12};

    g_buttons.clear();
    struct Def { int id; const wchar_t* label; };
    std::vector<Def> defs = {
        {B_IMPORT, L"导入名单 Ctrl+O"}, {B_SAVE, L"保存 Ctrl+S"},
        {B_NOREPEAT, L"防重复 N"},      {B_RESET, L"重置 R"},
        {B_ADD, L"加人 A"},             {B_MOVE, L"移组 M"},
        {B_RENAME, L"组改名 F2"},       {B_DEL, L"删除 Del"},
        {B_UNDO, L"撤销 Ctrl+Z"},       {B_INIT6, L"一键 6 组"},
        {B_HIST, L"导出记录"},          {B_HELP, L"帮助 H"},
    };
    int cols = 6;
    int bw = (rw - (cols - 1) * 8) / cols;
    int by = H - 26 - btnAreaH;
    for (size_t i = 0; i < defs.size(); i++) {
        int c = (int)i % cols, row = (int)i / cols;
        int bx = rx + c * (bw + 8);
        int byy = by + row * (bh + 8);
        g_buttons.push_back(Btn{defs[i].id, defs[i].label, RECT{bx, byy, bx + bw, byy + bh}});
    }

    // 顶栏主题切换
    g_themeRect = RECT{W - 190, 11, W - 78, 35};

    if (g_hEditN)
        MoveWindow(g_hEditN, g_nEditRect.left, g_nEditRect.top + 4,
                   g_nEditRect.right - g_nEditRect.left, g_nEditRect.bottom - g_nEditRect.top - 8, TRUE);
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

// 编辑框/静态文本的背景刷：缓存，别每次 WM_CTLCOLOR 都新建（GDI 对象会累积）
static HBRUSH g_brEdit = nullptr;
static COLORREF g_brEditColor = 0xFFFFFFFF;
static HBRUSH editBrush(COLORREF c) {
    if (!g_brEdit || g_brEditColor != c) {
        if (g_brEdit) DeleteObject(g_brEdit);
        g_brEdit = CreateSolidBrush(c);
        g_brEditColor = c;
    }
    return g_brEdit;
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

// ---------- 自绘下拉浮层（组列表 / 人数列表都用它）----------
struct PopCtx {
    std::vector<std::wstring> items;
    int chosen = -1;
    bool done = false;
    int hover = -1;
    int scroll = 0;
};
static PopCtx g_pop;
static HWND g_popWnd = nullptr;
static const int kPopRow = 28;

static LRESULT CALLBACK PopProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT cr;
            GetClientRect(hwnd, &cr);
            fillRect(dc, cr, g_p.panel);
            SetBkMode(dc, TRANSPARENT);
            for (size_t i = 0; i < g_pop.items.size(); i++) {
                RECT r{1, 1 + (int)i * kPopRow, cr.right - 1, 1 + (int)(i + 1) * kPopRow};
                if ((int)i == g_pop.hover) fillRect(dc, r, g_p.sel);
                text(dc, g_pop.items[i], RECT{r.left + 12, r.top, r.right - 8, r.bottom},
                     (int)i == g_pop.hover ? g_p.accent : g_p.text, g_fBody);
            }
            frameRect(dc, cr, g_p.accent);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int h = (pt.y - 1) / kPopRow;
            if (h < 0 || h >= (int)g_pop.items.size()) h = -1;
            if (h != g_pop.hover) {
                g_pop.hover = h;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int h = (pt.y - 1) / kPopRow;
            if (h >= 0 && h < (int)g_pop.items.size()) g_pop.chosen = h;
            g_pop.done = true;
            ReleaseCapture();
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                g_pop.chosen = -1;
                g_pop.done = true;
                ReleaseCapture();
                DestroyWindow(hwnd);
            } else if (wp == VK_UP) {
                if (g_pop.hover > 0) { g_pop.hover--; InvalidateRect(hwnd, nullptr, FALSE); }
            } else if (wp == VK_DOWN) {
                if (g_pop.hover + 1 < (int)g_pop.items.size()) { g_pop.hover++; InvalidateRect(hwnd, nullptr, FALSE); }
            } else if (wp == VK_RETURN) {
                g_pop.chosen = g_pop.hover;
                g_pop.done = true;
                ReleaseCapture();
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_DESTROY:
            g_pop.done = true;
            g_popWnd = nullptr;
            return 0;  // 注意：这里绝不能 PostQuitMessage，否则主程序会被一起带走
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// 在 anchor 下方弹出列表，返回选中下标（-1 = 取消）
static int popupList(HWND parent, RECT anchor, const std::vector<std::wstring>& items) {
    if (items.empty()) return -1;
    WNDCLASSW wc{};
    wc.lpfnWndProc = PopProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"PickerPopCls";
    wc.hbrBackground = CreateSolidBrush(g_p.panel);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    g_pop = PopCtx{};
    g_pop.items = items;

    POINT pt{anchor.left, anchor.bottom + 2};
    ClientToScreen(parent, &pt);
    int h = (int)items.size() * kPopRow + 2;
    int maxH = 320;
    if (h > maxH) h = maxH;
    int w = anchor.right - anchor.left;
    if (w < 150) w = 150;

    g_popWnd = CreateWindowExW(WS_EX_TOPMOST, L"PickerPopCls", L"", WS_POPUP | WS_BORDER | WS_VISIBLE,
                               pt.x, pt.y, w, h, parent, nullptr, GetModuleHandle(nullptr), nullptr);
    if (!g_popWnd) return -1;
    SetCapture(g_popWnd);
    MSG msg;
    while (!g_pop.done && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_popWnd) {
        DestroyWindow(g_popWnd);
        g_popWnd = nullptr;
    }
    return g_pop.chosen;
}

// ---------- 输入对话框 ----------
struct DlgCtx {
    std::vector<std::wstring> labels;
    std::vector<std::wstring> defaults;
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
                HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                                          i < ctx->defaults.size() ? ctx->defaults[i].c_str() : L"",
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
            SetBkColor(dc, g_p.panel2);
            SetTextColor(dc, g_p.text);
            return (LRESULT)editBrush(g_p.panel2);
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
            return 0;  // 不 PostQuitMessage：子窗口销毁不该终止主程序
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool inputDialog(HWND parent, const std::wstring& title, const std::vector<std::wstring>& labels,
                        std::vector<std::wstring>& out, const std::vector<std::wstring>& defaults = {}) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DlgProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"PickerDlgCls";
    wc.hbrBackground = CreateSolidBrush(g_p.panel);
    RegisterClassW(&wc);

    DlgCtx ctx;
    ctx.labels = labels;
    ctx.defaults = defaults;
    int h = 70 + (int)labels.size() * 32 + 44;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PickerDlgCls", title.c_str(),
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               CW_USEDEFAULT, CW_USEDEFAULT, 360, h, parent, nullptr,
                               GetModuleHandle(nullptr), &ctx);
    if (!dlg) return false;
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

// ---------- 帮助窗口（自绘 + 可滚动，字体完全可控）----------
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
            fillRect(dc, cr, g_p.bg);
            SetBkMode(dc, TRANSPARENT);

            RECT tr{20, 14, cr.right - 20, 46};
            SelectObject(dc, g_fTitle);
            SetTextColor(dc, g_p.accent);
            DrawTextW(dc, L"\u5e2e\u52a9 / help", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);  // 帮助 / help

            RECT lineR{20, 46, cr.right - 20, 48};
            fillRect(dc, lineR, g_p.border);

            POINT org{0, 0};
            SetViewportOrgEx(dc, 0, -g_helpScroll, &org);
            RECT body{20, 62 + g_helpScroll, cr.right - 20, 62 + g_helpScroll + 4000};
            SelectObject(dc, g_fBody);
            SetTextColor(dc, g_p.text);
            DrawTextW(dc, g_helpText.c_str(), -1, &body, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            SetViewportOrgEx(dc, org.x, org.y, nullptr);

            RECT fr{0, cr.bottom - 34, cr.right, cr.bottom};
            fillRect(dc, fr, g_p.panel);
            SelectObject(dc, g_fSmall);
            SetTextColor(dc, g_p.dim);
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
    wc.hbrBackground = CreateSolidBrush(g_p.bg);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    g_helpWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PickerHelpCls", L"\u5e2e\u52a9 / help",  // 帮助 / help
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 680, 600, parent, nullptr,
                                GetModuleHandle(nullptr), nullptr);
    if (!g_helpWnd) {
        msgBox(parent, body, L"\u5e2e\u52a9 / help", MB_OK | MB_ICONINFORMATION);
        return;
    }
    SendMessageW(g_helpWnd, WM_SETFONT, (WPARAM)g_fBody, TRUE);
    // 量一下文本实际高度，供滚动用
    HDC dc = GetDC(g_helpWnd);
    SelectObject(dc, g_fBody);
    RECT mr{20, 62, 660, 62 + 4000};
    DrawTextW(dc, g_helpText.c_str(), -1, &mr, DT_LEFT | DT_WORDBREAK | DT_CALCRECT);
    g_helpMax = mr.bottom - mr.top;
    ReleaseDC(g_helpWnd, dc);
}

// ---------- 自绘确认框（不依赖系统 MessageBox，中文渲染完全可控）----------
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
            SetTextColor(dc, g_p.text);
            RECT br{24, 20, cr.right - 24, g_cb1.top - 14};
            DrawTextW(dc, g_conf.body.c_str(), -1, &br, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

            fillRect(dc, RECT{0, g_cb1.top - 12, cr.right, g_cb1.top - 11}, g_p.border);

            auto drawBtn = [&](RECT r, const std::wstring& label, bool hot) {
                fillRect(dc, r, hot ? g_p.hot : g_p.panel2);
                SelectObject(dc, GetStockObject(NULL_BRUSH));
                HPEN p = CreatePen(PS_SOLID, 1, hot ? g_p.accent : g_p.border);
                HGDIOBJ op = SelectObject(dc, p);
                RoundRect(dc, r.left, r.top, r.right, r.bottom, 6, 6);
                SelectObject(dc, op);
                DeleteObject(p);
                SelectObject(dc, g_fBody);
                SetTextColor(dc, hot ? g_p.accent : g_p.text);
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
    wc.hbrBackground = CreateSolidBrush(g_p.panel);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    g_conf = ConfirmCtx{};
    g_conf.body = body;
    g_conf.b1 = b1;
    g_conf.b2 = b2;
    g_confWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PickerConfirmCls", title.c_str(),
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 470, 220, parent, nullptr,
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

static void notice(HWND hwnd, const std::wstring& body) {
    confirmDialog(hwnd, L"\u63d0\u793a", body, L"\u597d", L"\u5173\u95ed");  // 提示 / 好 / 关闭
}

// ---------- 动作 ----------
static std::wstring scopeName() {
    if (g_scope < 0) return L"全班";
    auto gs = g_roster.groupList();
    if (g_scope < (int)gs.size()) return gs[g_scope];
    return L"全班";
}

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
        g_last = LastPick{};
        g_last.ok = true;
        g_last.isGroup = false;
        g_last.list.push_back(r.person);
        g_last.seed = r.seed;
        g_last.where = scopeName();
        g_status = L"抽中：" + displayName(r.person) + L" · " + g_last.where;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void doPickGroup(HWND hwnd) {
    PickResult r = g_roster.pickGroup();
    if (!r.ok) {
        g_status = L"还没有分组信息，先点「一键 6 组」或导入带组别的名单";
    } else {
        g_last = LastPick{};
        g_last.ok = true;
        g_last.isGroup = true;
        g_last.list.push_back(r.person);
        g_last.seed = r.seed;
        g_last.where = L"全班";
        g_status = L"抽中组：" + r.person.group;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void doPickN(HWND hwnd, int n) {
    MultiResult r = g_roster.pickMulti(g_scope, n);
    if (!r.ok) {
        notice(hwnd, r.msg);
        g_status = r.msg;
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    g_last = LastPick{};
    g_last.ok = true;
    g_last.isGroup = false;
    g_last.list = r.people;
    g_last.seed = r.seeds;
    g_last.where = scopeName();
    std::wstring names;
    for (size_t i = 0; i < r.people.size(); i++) {
        if (i) names += L"、";
        names += displayName(r.people[i]);
    }
    g_status = L"抽中 " + std::to_wstring(r.people.size()) + L" 人（" + g_last.where + L"）：" + names;
    InvalidateRect(hwnd, nullptr, FALSE);
}

static int readN(HWND) {
    if (!g_hEditN) return g_pickN;
    wchar_t buf[32] = {0};
    GetWindowTextW(g_hEditN, buf, 32);
    int v = _wtoi(buf);
    if (v <= 0) return g_pickN;
    return v;
}

static void setN(int n) {
    if (n < 1) n = 1;
    if (n > 99) n = 99;
    g_pickN = n;
    if (g_hEditN) {
        wchar_t buf[16];
        wsprintfW(buf, L"%d", n);
        SetWindowTextW(g_hEditN, buf);
    }
}

static void doPickAll(HWND hwnd) {
    doPickN(hwnd, g_roster.countInScope(g_scope));
}

static void doAdd(HWND hwnd) {
    auto gs = g_roster.groupList();
    std::vector<std::wstring> vals;
    std::wstring dflt = (g_scope >= 0 && g_scope < (int)gs.size()) ? gs[g_scope] : L"";
    if (!inputDialog(hwnd, L"添加成员", {L"姓名", L"英文名(可空)", L"组别", L"学号(可空)"}, vals,
                     {L"", L"", dflt, L""}))
        return;
    if (vals.size() < 3 || vals[0].empty()) return;
    Person p{vals[0], vals.size() > 1 ? vals[1] : L"", vals[2], vals.size() > 3 ? vals[3] : L""};
    g_roster.add(p);
    g_status = L"已添加：" + displayName(p) + L" [" + (p.group.empty() ? L"未分组" : p.group) + L"]";
    computeLayout(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// 移组：弹下拉选目标组，比手打组名短得多；最后一项是「新建组…」
static void doMove(HWND hwnd) {
    if (g_sel < 0 || g_sel >= (int)g_roster.people.size()) {
        g_status = L"先在左侧点一个人，再点「移组」";
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    auto gs = g_roster.groupList();
    std::vector<std::wstring> items = gs;
    items.push_back(L"\uff0b \u65b0\u5efa\u7ec4\u2026");  // ＋ 新建组…
    int k = popupList(hwnd, g_listRect, items);
    std::wstring target;
    if (k >= 0 && k < (int)gs.size()) {
        target = gs[k];
    } else if (k == (int)gs.size()) {
        std::vector<std::wstring> vals;
        if (!inputDialog(hwnd, L"新建组", {L"组名"}, vals)) return;
        if (vals.empty() || vals[0].empty()) return;
        target = vals[0];
        if (std::find(gs.begin(), gs.end(), target) == gs.end()) g_roster.groups.push_back(target);
    } else {
        return;
    }
    g_roster.setGroup(g_sel, target);
    g_status = L"已把 " + displayName(g_roster.people[g_sel]) + L" 移到 " + target;
    computeLayout(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void doRenameGroup(HWND hwnd) {
    auto gs = g_roster.groupList();
    if (g_scope < 0 || g_scope >= (int)gs.size()) {
        g_status = L"先点上面的组名页签选中一个组，再改名（双击组名也行）";
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    std::wstring oldName = gs[g_scope];
    std::vector<std::wstring> vals;
    if (!inputDialog(hwnd, L"组改名", {L"新组名"}, vals, {oldName})) return;
    if (vals.empty() || vals[0].empty() || vals[0] == oldName) return;
    std::wstring msg;
    if (g_roster.renameGroup(oldName, vals[0], msg)) {
        g_status = msg;
    } else {
        notice(hwnd, msg);
        g_status = msg;
    }
    computeLayout(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void doDelete(HWND hwnd) {
    if (g_sel < 0 || g_sel >= (int)g_roster.people.size()) return;
    std::wstring n = displayName(g_roster.people[g_sel]);
    int r = confirmDialog(hwnd, L"\u5220\u9664\u786e\u8ba4",  // 删除确认
                          L"要把「" + n + L"」从名单里删掉吗？\n\n删错了别慌，点「撤销」就能放回来。",
                          L"\u5220\u9664", L"\u4e0d\u5220");  // 删除 / 不删
    if (r != 1) return;
    g_undoPerson = g_roster.people[g_sel];
    g_undoIndex = g_sel;
    g_hasUndo = true;
    g_roster.removeAt(g_sel);
    g_sel = -1;
    g_status = L"已删除：" + n + L"（Ctrl+Z 撤销）";
    computeLayout(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void doUndo(HWND hwnd) {
    if (!g_hasUndo) {
        g_status = L"没有可撤销的操作";
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    int at = g_undoIndex < (int)g_roster.people.size() ? g_undoIndex : (int)g_roster.people.size();
    g_roster.people.insert(g_roster.people.begin() + at, g_undoPerson);
    g_hasUndo = false;
    g_status = L"已撤销删除：" + displayName(g_undoPerson);
    computeLayout(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// 一键 6 组：
//  · 名单还没分组 → 直接轮流分成 6 组（最常见，一步到位）
//  · 已经有分组 → 先问清「重排」还是「只补空组」，绝不偷偷覆盖老师的现有分组
static void doInit6(HWND hwnd) {
    auto gs = g_roster.groupList();
    int real = 0;
    std::wstring sample;
    for (auto& g : gs) {
        if (g == L"未分组") continue;
        real++;
        if (sample.size() < 24) sample += (sample.empty() ? L"" : L"、") + g;
    }
    std::wstring msg;
    bool changed = false;
    if (real == 0) {
        changed = g_roster.regroupAll(6, msg);
    } else {
        int r = confirmDialog(hwnd, L"\u4e00\u952e 6 \u7ec4",  // 一键 6 组
                              L"名单里现在有 " + std::to_wstring(real) + L" 个组（" + sample + L"）。\n\n"
                              L"重排 —— 全班按名单顺序轮流分成第 1~6 组，原来的分组会被覆盖\n"
                              L"只补空组 —— 保留现在的分组，只把不足的空组补到 6 个\n\n"
                              L"（键盘 1 / 2 也可选，Esc 取消）",
                              L"\u91cd\u6392", L"\u53ea\u8865\u7a7a\u7ec4");  // 重排 / 只补空组
        if (r == 1)
            changed = g_roster.regroupAll(6, msg);
        else if (r == 2)
            changed = g_roster.initGroups(6, msg);
        else
            return;
    }
    if (changed) {
        g_status = msg;
        computeLayout(hwnd);
    } else {
        notice(hwnd, msg);
        g_status = msg;
    }
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

static void doToggleTheme(HWND hwnd) {
    applyTheme(g_theme ? 0 : 1);
    g_status = L"外观已切换为 " + std::wstring(themeName()) + L" 风（按 T 或点右上角可换回来）";
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void doHelp(HWND hwnd) {
    std::wstring s =
        L"班级随机抽人工具  v2\n"
        L"----------------------------\n"
        L"最常用的三件事：\n"
        L"  抽 1 人   右上角大按钮 / 空格\n"
        L"  抽 1 组   右上角大按钮 / G\n"
        L"  抽 N 人   在第二行填人数（右边 ▾ 也能选），点「抽 N 人」；点「全部」= 把范围里的人一次抽完\n"
        L"  · 先点左边的组名页签，就只在这个组里抽；点「全班」就是全班抽\n\n"
        L"快捷键：\n"
        L"  空格      抽 1 人\n"
        L"  G         抽 1 个组\n"
        L"  N         开关「防重复」（本轮不重名，抽完自动重置）\n"
        L"  R         重置已抽记录\n"
        L"  T         切换外观（极客深色 / ins 浅色）\n"
        L"  A         添加成员\n"
        L"  M         把选中的人移到别的组（弹列表选，不用打字）\n"
        L"  F2        给当前组改名（双击组名也可以）\n"
        L"  Del       删除选中的人（会先问一次，删错可 Ctrl+Z 撤销）\n"
        L"  Ctrl+Z    撤销上一次删除\n"
        L"  Ctrl+O    导入名单（csv/txt/md/xlsx）\n"
        L"  Ctrl+S    保存名单为 csv\n"
        L"  H         本帮助\n\n"
        L"分组怎么弄：\n"
        L"  点「一键 6 组」—— 自动生成第 1~6 组，没分组的同学会平均分进去\n"
        L"  双击组名 / 按 F2 —— 改组名，组员自动跟着改\n"
        L"  选中人 +「移组」—— 弹列表选目标组，一步到位\n\n"
        L"名单格式（推荐 csv，UTF-8）：\n"
        L"  姓名,组别,学号\n"
        L"  张三,一组,20260101\n"
        L"  · 首行可省略，程序会自动识别表头\n"
        L"  · .xlsx 直接读取（内置解压，无需额外依赖）\n"
        L"  · .md 支持 markdown 表格\n\n"
        L"随机源：Windows 系统级密码学随机数（BCryptGenRandom）+ 拒绝采样，\n"
        L"消除取模偏差；每次抽取记录 seed，可导出留证。\n\n"
        L"界面字体：" + g_uiFace + L"\n" +
        L"当前外观：" + std::wstring(themeName()) + L"\n\n" +
        std::wstring(kSignature);
    showHelp(hwnd, s);
}

// ---------- 主窗口绘制 ----------
static void drawMainButton(HDC dc, const Btn& b, bool hover, bool primary) {
    if (primary) {
        roundRect(dc, b.r, g_p.radius, g_p.accent, g_p.accent);
        text(dc, b.label, b.r, g_p.onAccent, g_fTitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        roundRect(dc, b.r, g_p.radius, hover ? g_p.hot : g_p.panel2, g_p.border);
        text(dc, b.label, b.r, g_p.text, g_fBody, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

static void paint(HWND hwnd) {
    RECT cr;
    GetClientRect(hwnd, &cr);
    int W = cr.right, H = cr.bottom;
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    HDC dc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
    HBITMAP old = (HBITMAP)SelectObject(dc, bmp);

    fillRect(dc, cr, g_p.bg);

    // 顶栏
    RECT hdr{0, 0, W, 46};
    fillRect(dc, hdr, g_p.panel);
    text(dc, L"RANDOM PICKER", RECT{16, 0, 300, 46}, g_p.accent, g_fTitle);
    text(dc, L"班级随机抽人", RECT{150, 0, 400, 46}, g_p.dim, g_fBody);
    // 主题切换
    roundRect(dc, g_themeRect, 8, g_p.panel2, g_p.border);
    std::wstring tlabel = std::wstring(L"\u98ce\u683c\uff1a") + themeName() + L" \u21c4";  // 风格：xx ⇄
    text(dc, tlabel, g_themeRect, g_p.text, g_fSmall, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, L"byHry", RECT{W - 78, 0, W - 54, 46}, g_p.dim, g_fSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    // CTW 标（真徽标，PNG 带透明，GDI+ 绘制）
    if (g_logo) {
        Gdiplus::Graphics gx(dc);
        gx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        int lh = 36, lw = (int)(lh * ((double)g_logo->GetWidth() / g_logo->GetHeight()));
        gx.DrawImage(g_logo, W - 16 - lw, (46 - lh) / 2, lw, lh);
    }

    // 左栏
    RECT left{0, 46, 320, H - 26};
    fillRect(dc, left, g_p.panel);
    frameRect(dc, left, g_p.border);

    auto gs = g_roster.groupList();
    for (size_t i = 0; i < g_tabRects.size(); i++) {
        bool active = ((int)i - 1 == g_scope) || (i == 0 && g_scope == -1);
        RECT r = g_tabRects[i];
        roundRect(dc, r, g_p.radius, active ? g_p.sel : g_p.panel2, active ? g_p.accent : g_p.border);
        std::wstring label = i == 0 ? L"全班" : gs[i - 1];
        int cnt = (i == 0) ? (int)g_roster.people.size() : g_roster.countInGroup(gs[i - 1]);
        label += L" " + std::to_wstring(cnt);
        text(dc, label, r, active ? g_p.accent : g_p.dim, g_fSmall, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
        bool inScope = g_scope < 0 || (g_scope < (int)gs.size() &&
                                       (p.group.empty() ? L"未分组" : p.group) == gs[g_scope]);
        if (idx == g_sel) {
            fillRect(dc, r, g_p.sel);
            frameRect(dc, r, g_p.accent2);
        } else if (!inScope) {
            fillRect(dc, r, g_p.rowAlt);
        }
        COLORREF nc = drawnFlag ? g_p.dim : (inScope ? g_p.text : g_p.dim2);
        wchar_t num[16];
        wsprintfW(num, L"%02d", idx + 1);
        RECT nr{r.left + 6, r.top, r.left + 30, r.bottom};
        text(dc, num, nr, g_p.dim, g_fSmall);
        RECT nameR{r.left + 32, r.top, r.right - 76, r.bottom};
        text(dc, displayName(p), nameR, nc, g_fBody,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT grpR{r.right - 72, r.top, r.right - 4, r.bottom};
        text(dc, p.group.empty() ? L"-" : p.group, grpR, drawnFlag ? g_p.dim : g_p.accent, g_fSmall,
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (total == 0) {
        RECT r{g_listRect.left, top + 8, g_listRect.right, top + 40};
        text(dc, L"（名单为空，Ctrl+O 导入）", r, g_p.dim, g_fBody);
    }

    // 主按钮
    for (const Btn& b : g_big) {
        if (b.id == BIG_NDROP) {
            roundRect(dc, b.r, g_p.radius, g_hoverBig == b.id ? g_p.hot : g_p.panel2, g_p.border);
            text(dc, b.label, b.r, g_p.text, g_fBody, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (b.id == BIG_PICKALL) {
            drawMainButton(dc, b, g_hoverBig == b.id, false);
        } else {
            drawMainButton(dc, b, g_hoverBig == b.id, true);
        }
    }

    // 结果卡
    roundRect(dc, g_cardRect, g_p.radius, g_p.panel, g_last.ok ? g_p.accent : g_p.border);
    if (g_last.ok) {
        RECT br{g_cardRect.left + 20, g_cardRect.top + 16, g_cardRect.right - 20, g_cardRect.top + 42};
        std::wstring title = g_last.isGroup ? L"抽中的组"
                                            : (g_last.list.size() > 1
                                                   ? (L"抽中的 " + std::to_wstring(g_last.list.size()) + L" 人")
                                                   : L"抽中的人");
        text(dc, title + L" · 范围：" + g_last.where, br, g_p.dim, g_fSmall);

        if (g_last.list.size() == 1) {
            RECT nr{g_cardRect.left + 20, g_cardRect.top + 40, g_cardRect.right - 20, g_cardRect.top + 100};
            text(dc, displayName(g_last.list[0]), nr, g_p.accent, g_fBig,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (!g_last.isGroup) {
                RECT gr{g_cardRect.left + 20, g_cardRect.top + 104, g_cardRect.right - 20, g_cardRect.top + 132};
                text(dc, L"组别：" + (g_last.list[0].group.empty() ? L"未分组" : g_last.list[0].group),
                     gr, g_p.text, g_fBody);
            }
        } else {
            int maxRows = 5;
            for (size_t i = 0; i < g_last.list.size() && i < (size_t)maxRows; i++) {
                RECT nr{g_cardRect.left + 20, g_cardRect.top + 40 + (int)i * 28,
                        g_cardRect.right - 20, g_cardRect.top + 68 + (int)i * 28};
                std::wstring line = displayName(g_last.list[i]);
                if (!g_last.list[i].group.empty()) line += L"  [" + g_last.list[i].group + L"]";
                text(dc, line, nr, g_p.accent, g_fMid, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
            if (g_last.list.size() > (size_t)maxRows) {
                RECT mr{g_cardRect.left + 20, g_cardRect.top + 40 + maxRows * 28,
                        g_cardRect.right - 20, g_cardRect.top + 68 + maxRows * 28};
                text(dc, L"…还有 " + std::to_wstring(g_last.list.size() - maxRows) + L" 人（导出记录看全部）",
                     mr, g_p.dim, g_fSmall);
            }
        }
        RECT sr{g_cardRect.left + 20, g_cardRect.bottom - 46, g_cardRect.right - 20, g_cardRect.bottom - 16};
        text(dc, L"seed " + g_last.seed, sr, g_p.dim, g_fMono,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        RECT br{g_cardRect.left + 20, g_cardRect.top + 20, g_cardRect.right - 20, g_cardRect.bottom - 20};
        text(dc, L"按 空格 抽 1 人，或者上面填人数抽 N 人", br, g_p.dim, g_fBody);
    }

    // 历史
    roundRect(dc, g_histRect, g_p.radius, g_p.panel, g_p.border);
    RECT hr{g_histRect.left + 14, g_histRect.top + 10, g_histRect.right - 14, g_histRect.top + 30};
    text(dc, L"抽取记录（最近的在上，可导出留证）", hr, g_p.dim, g_fSmall);
    int n = (int)g_roster.history.size();
    int rows = (g_histRect.bottom - g_histRect.top - 36) / 22;
    if (rows < 1) rows = 1;
    for (int i = 0; i < rows && i < n; i++) {
        RECT r{g_histRect.left + 14, g_histRect.top + 34 + i * 22, g_histRect.right - 14,
               g_histRect.top + 56 + i * 22};
        text(dc, g_roster.history[n - 1 - i], r, g_p.dim2, g_fSmall,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    // 底部按钮
    for (const Btn& b : g_buttons) {
        bool on = (b.id == B_NOREPEAT && g_roster.noRepeat);
        bool hover = (g_hover == b.id);
        COLORREF bg = on ? g_p.sel : (hover ? g_p.hot : g_p.panel2);
        COLORREF bd = on ? g_p.accent : g_p.border;
        roundRect(dc, b.r, g_p.radius, bg, bd);
        text(dc, b.label, b.r, on ? g_p.accent : (hover ? g_p.text : g_p.dim2), g_fSmall,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // 状态栏
    RECT sb{0, H - 26, W, H};
    fillRect(dc, sb, g_p.panel);
    frameRect(dc, sb, g_p.border);
    wchar_t cnt[160];
    wsprintfW(cnt, L"共 %d 人 · %d 组 · 当前可抽 %d 人 · 防重复:%s · %s", (int)g_roster.people.size(),
              (int)g_roster.groupList().size(), g_roster.countInScope(g_scope),
              g_roster.noRepeat ? L"开" : L"关", themeName());
    text(dc, cnt, RECT{W - 400, H - 26, W - 12, H}, g_p.dim, g_fSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    text(dc, g_status, RECT{16, H - 26, W - 410, H}, g_p.dim2, g_fSmall,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    BitBlt(hdc, 0, 0, W, H, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

// ---------- 命中测试 ----------
struct Hit { int big, btn, tab; };
static Hit hitTest(int x, int y) {
    Hit h{-1, -1, -1};
    for (const Btn& b : g_big)
        if (x >= b.r.left && x <= b.r.right && y >= b.r.top && y <= b.r.bottom) h.big = b.id;
    for (const Btn& b : g_buttons)
        if (x >= b.r.left && x <= b.r.right && y >= b.r.top && y <= b.r.bottom) h.btn = b.id;
    for (size_t i = 0; i < g_tabRects.size(); i++) {
        RECT r = g_tabRects[i];
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) h.tab = (int)i;
    }
    return h;
}

// ---------- 主窗口过程 ----------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            applyTheme(g_theme);
            computeLayout(hwnd);
            g_hEditN = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"2",
                                       WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
                                       g_nEditRect.left, g_nEditRect.top + 4, 66, 32, hwnd, nullptr,
                                       GetModuleHandle(nullptr), nullptr);
            if (g_hEditN) SendMessageW(g_hEditN, WM_SETFONT, (WPARAM)g_fBody, TRUE);
            return 0;
        case WM_SIZE:
            computeLayout(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wp;
            SetBkColor(dc, g_p.panel2);
            SetTextColor(dc, g_p.text);
            return (LRESULT)editBrush(g_p.panel2);
        }
        case WM_PAINT:
            paint(hwnd);
            return 0;
        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            Hit h = hitTest(x, y);
            if (h.big != g_hoverBig || h.btn != g_hover || h.tab != g_hoverTab) {
                g_hoverBig = h.big;
                g_hover = h.btn;
                g_hoverTab = h.tab;
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
        case WM_LBUTTONDBLCLK: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            Hit h = hitTest(x, y);
            if (h.tab > 0) {  // 双击组名 = 改名
                g_scope = h.tab - 1;
                doRenameGroup(hwnd);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            SetFocus(hwnd);  // 把焦点从人数输入框收回来，方便接着用快捷键
            Hit h = hitTest(x, y);
            if (h.big >= 0) {
                switch (h.big) {
                    case BIG_PICK1: doPick(hwnd); return 0;
                    case BIG_PICKGROUP: doPickGroup(hwnd); return 0;
                    case BIG_PICKN: {
                        int n = readN(hwnd);
                        setN(n);
                        doPickN(hwnd, n);
                        return 0;
                    }
                    case BIG_PICKALL: doPickAll(hwnd); return 0;
                    case BIG_NDROP: {
                        std::vector<std::wstring> items;
                        for (int i = 1; i <= 12; i++) items.push_back(std::to_wstring(i));
                        int k = popupList(hwnd, g_nDropRect, items);
                        if (k >= 0) setN(k + 1);
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                }
            }
            if (h.btn >= 0) {
                switch (h.btn) {
                    case B_IMPORT: doImport(hwnd); return 0;
                    case B_SAVE: doSave(hwnd); return 0;
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
                    case B_MOVE: doMove(hwnd); return 0;
                    case B_RENAME: doRenameGroup(hwnd); return 0;
                    case B_DEL: doDelete(hwnd); return 0;
                    case B_UNDO: doUndo(hwnd); return 0;
                    case B_INIT6: doInit6(hwnd); return 0;
                    case B_HIST: doExportHistory(hwnd); return 0;
                    case B_HELP: doHelp(hwnd); return 0;
                }
            }
            if (x >= g_themeRect.left && x <= g_themeRect.right && y >= g_themeRect.top &&
                y <= g_themeRect.bottom) {
                doToggleTheme(hwnd);
                return 0;
            }
            if (h.tab >= 0) {
                g_scope = h.tab - 1;
                g_status = (g_scope == -1) ? L"范围：全班" : L"范围：" + g_roster.groupList()[g_scope];
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
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
                    g_status = g_roster.noRepeat ? L"防重复：开（本轮不重名）" : L"防重复：关";
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case 'R':
                    g_roster.resetDrawn();
                    g_status = L"已重置抽取记录";
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case 'T': doToggleTheme(hwnd); return 0;
                case 'A': doAdd(hwnd); return 0;
                case 'M': doMove(hwnd); return 0;
                case 'H': doHelp(hwnd); return 0;
                case VK_F2: doRenameGroup(hwnd); return 0;
                case VK_DELETE: doDelete(hwnd); return 0;
                case 'O':
                    if (ctrl) doImport(hwnd);
                    return 0;
                case 'S':
                    if (ctrl) doSave(hwnd);
                    return 0;
                case 'Z':
                    if (ctrl) doUndo(hwnd);
                    return 0;
                case VK_RETURN: {
                    int n = readN(hwnd);
                    setN(n);
                    doPickN(hwnd, n);
                    return 0;
                }
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
    g_fTitle = makeFont(16, 600, g_uiFace);
    g_fBody = makeFont(15, 400, g_uiFace);
    g_fBig = makeFont(44, 500, g_uiFace);
    g_fMid = makeFont(22, 500, g_uiFace);
    g_fSmall = makeFont(13, 400, g_uiFace);
    g_fMono = makeMonoFont(13, 400);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"PickerMainCls";
    wc.hbrBackground = CreateSolidBrush(g_p.bg);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(kResIcon), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"PickerMainCls", L"随机抽人 · byHry · CTW",
                                WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, CW_USEDEFAULT, CW_USEDEFAULT,
                                1040, 720, nullptr, nullptr, hInst, nullptr);
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
