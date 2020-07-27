/* Copyright 2020 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "utils/BaseUtil.h"
#include "utils/ScopedWin.h"
#include "utils/Dpi.h"
#include "utils/WinUtil.h"
#include "utils/Log.h"

#include "wingui/WinGui.h"
#include "wingui/Layout.h"
#include "wingui/Window.h"
#include "wingui/TabsCtrl.h"

// TODO: temporary
#include "AppColors.h"

#define COL_BLACK RGB(0x00, 0x00, 0x00)
#define COL_WHITE RGB(0xff, 0xff, 0xff)
#define COL_RED RGB(0xff, 0x00, 0x00)
#define COL_LIGHT_GRAY RGB(0xde, 0xde, 0xde)
#define COL_LIGHTER_GRAY RGB(0xee, 0xee, 0xee)
#define COL_DARK_GRAY RGB(0x42, 0x42, 0x42)

// desired space between top of the text in tab and top of the tab
#define PADDING_TOP 4
// desired space between bottom of the text in tab and bottom of the tab
#define PADDING_BOTTOM 4

// space to the left of tab label
#define PADDING_LEFT 8
// empty space to the righ of tab label
#define PADDING_RIGHT 8

// TODO: implement a max width for the tab

enum class Tab {
    Selected = 0,
    Background = 1,
    Highlighted = 2,
};

static str::WStr wstrFromUtf8(const str::Str& str) {
    AutoFreeWstr s = strconv::Utf8ToWstr(str.Get());
    return str::WStr(s.AsView());
}

TabItem::TabItem(const std::string_view title, const std::string_view toolTip) {
    this->title = title;
    this->toolTip = toolTip;
}

class TabItemInfo {
  public:
    str::WStr title;
    str::WStr toolTip;

    SIZE titleSize;
    // area for this tab item inside the tab window
    RECT tabRect;
    POINT titlePos;
    RECT closeRect;
    HWND hwndTooltip;
};

class TabsCtrlPrivate {
  public:
    TabsCtrlPrivate(HWND hwnd) {
        this->hwnd = hwnd;
    }
    ~TabsCtrlPrivate() {
        DeleteObject(font);
    }

    HWND hwnd = nullptr;
    HFONT font = nullptr;
    // TODO: logFont is not used anymore, keep it for debugging?
    LOGFONTW logFont{}; // info that corresponds to font
    TEXTMETRIC fontMetrics{};
    int fontDy = 0;
    SIZE size{};                // current size of the control's window
    SIZE idealSize{};           // ideal size as calculated during layout
    int tabIdxUnderCursor = -1; // -1 if none under cursor
    bool isCursorOverClose = false;

    TabsCtrlState* state = nullptr;

    // each TabItemInfo orresponds to TabItem from state->tabs, same order
    Vec<TabItemInfo*> tabInfos;
};

static long GetIdealDy(TabsCtrl* ctrl) {
    auto priv = ctrl->priv;
    int padTop = PADDING_TOP;
    int padBottom = PADDING_BOTTOM;
    DpiScale(priv->hwnd, padTop, padBottom);
    return priv->fontDy + padTop + padBottom;
}

static HWND CreateTooltipForRect(HWND parent, const WCHAR* s, RECT& r) {
    HMODULE h = GetModuleHandleW(nullptr);
    DWORD dwStyleEx = WS_EX_TOPMOST;
    DWORD dwStyle = WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP;
    HWND hwnd = CreateWindowExW(dwStyleEx, TOOLTIPS_CLASSW, NULL, dwStyle, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                CW_USEDEFAULT, parent, NULL, h, NULL);

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    TOOLINFOW ti = {0};
    ti.cbSize = sizeof(TOOLINFO);
    ti.uFlags = TTF_SUBCLASS;
    ti.hwnd = parent;
    ti.hinst = h;
    ti.lpszText = (WCHAR*)s;
    ti.rect = r;
    SendMessageW(hwnd, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    SendMessageW(hwnd, TTM_ACTIVATE, TRUE, 0);
    return hwnd;
}

void LayoutTabs(TabsCtrl* ctrl) {
    auto priv = ctrl->priv;
    long x = 0;
    long dy = priv->size.cy;
    auto idealDy = GetIdealDy(ctrl);

    int padLeft = PADDING_LEFT;
    int padRight = PADDING_RIGHT;
    DpiScale(priv->hwnd, padLeft, padRight);

    long closeButtonDy = (priv->fontMetrics.tmAscent / 2) + DpiScale(priv->hwnd, 1);
    long closeButtonY = (dy - closeButtonDy) / 2;
    if (closeButtonY < 0) {
        closeButtonDy = dy - 2;
        closeButtonY = 2;
    }

    for (auto& ti : priv->tabInfos) {
        long xStart = x;
        x += padLeft;

        auto sz = ti->titleSize;
        // position y of title text and 'x' circle
        long titleY = 0;
        if (dy > sz.cy) {
            titleY = (dy - sz.cy) / 2;
        }
        ti->titlePos = MakePoint(x, titleY);

        // TODO: implement max dx of the tab
        x += sz.cx;
        x += padRight;
        ti->closeRect = MakeRect(x, closeButtonY, closeButtonDy, closeButtonDy);
        x += closeButtonDy;
        x += padRight;
        long dx = (x - xStart);
        ti->tabRect = MakeRect(xStart, 0, dx, dy);
        if (!ti->toolTip.IsEmpty()) {
            if (ti->hwndTooltip) {
                DestroyWindow(ti->hwndTooltip);
            }
            ti->hwndTooltip = CreateTooltipForRect(priv->hwnd, ti->toolTip.Get(), ti->tabRect);
        }
    }
    priv->idealSize = MakeSize(x, idealDy);
    // TODO: if dx > size of the tab, we should shrink the tabs
    TriggerRepaint(priv->hwnd);
}

static LRESULT CALLBACK TabsParentProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR uIdSubclass,
                                       DWORD_PTR dwRefData) {
    UNUSED(uIdSubclass);
    UNUSED(dwRefData);

    // TabsCtrl *w = (TabsCtrl *)dwRefData;
    // CrashIf(GetParent(ctrl->hwnd) != (HWND)lp);

    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void PaintClose(HWND hwnd, HDC hdc, RECT& r, bool isHighlighted) {
    auto x = r.left;
    auto y = r.top;
    auto dx = RectDx(r);
    auto dy = RectDy(r);

    COLORREF lineCol = COL_BLACK;
    if (isHighlighted) {
        int p = 3;
        DpiScale(hwnd, p);
        AutoDeleteBrush brush(CreateSolidBrush(COL_RED));
        RECT r2 = r;
        r2.left -= p;
        r2.right += p;
        r2.top -= p;
        r2.bottom += p;
        FillRect(hdc, &r2, brush);
        lineCol = COL_WHITE;
    }

    AutoDeletePen pen(CreatePen(PS_SOLID, 2, lineCol));
    ScopedSelectPen p(hdc, pen);
    MoveToEx(hdc, x, y, nullptr);
    LineTo(hdc, x + dx, y + dy);

    MoveToEx(hdc, x + dx, y, nullptr);
    LineTo(hdc, x, y + dy);
}

static void Paint(TabsCtrl* ctrl) {
    auto priv = ctrl->priv;
    HWND hwnd = priv->hwnd;

    PAINTSTRUCT ps;
    RECT rc = GetClientRect(hwnd);
    HDC hdc = BeginPaint(hwnd, &ps);

    AutoDeleteBrush brush(CreateSolidBrush(COL_LIGHTER_GRAY));
    FillRect(hdc, &rc, brush);

    ScopedSelectFont f(hdc, priv->font);
    uint opts = ETO_OPAQUE;

    int padLeft = PADDING_LEFT;
    DpiScale(priv->hwnd, padLeft);

    int tabIdx = 0;
    for (const auto& ti : priv->tabInfos) {
        if (ti->title.IsEmpty()) {
            continue;
        }
        auto tabType = Tab::Background;
        if (tabIdx == priv->state->selectedItem) {
            tabType = Tab::Selected;
        } else if (tabIdx == priv->tabIdxUnderCursor) {
            tabType = Tab::Highlighted;
        }
        COLORREF bgCol = COL_LIGHTER_GRAY;
        COLORREF txtCol = COL_DARK_GRAY;

        bool paintClose = false;
        switch (tabType) {
            case Tab::Background:
                bgCol = COL_LIGHTER_GRAY;
                txtCol = COL_DARK_GRAY;
                break;
            case Tab::Selected:
                bgCol = COL_WHITE;
                txtCol = COL_DARK_GRAY;
                paintClose = true;
                break;
            case Tab::Highlighted:
                bgCol = COL_LIGHT_GRAY;
                txtCol = COL_BLACK;
                paintClose = true;
                break;
        }

        SetTextColor(hdc, txtCol);
        SetBkColor(hdc, bgCol);

        auto tabRect = ti->tabRect;
        AutoDeleteBrush brush2(CreateSolidBrush(bgCol));
        FillRect(hdc, &tabRect, brush2);

        auto pos = ti->titlePos;
        int x = pos.x;
        int y = pos.y;
        const WCHAR* s = ti->title.Get();
        uint sLen = (uint)ti->title.size();
        ExtTextOutW(hdc, x, y, opts, nullptr, s, sLen, nullptr);

        if (paintClose) {
            bool isCursorOverClose = priv->isCursorOverClose && (tabIdx == priv->tabIdxUnderCursor);
            PaintClose(hwnd, hdc, ti->closeRect, isCursorOverClose);
        }

        tabIdx++;
    }

    EndPaint(hwnd, &ps);
}

static void SetTabUnderCursor(TabsCtrl* ctrl, int tabUnderCursor, bool isMouseOverClose) {
    auto priv = ctrl->priv;
    if (priv->tabIdxUnderCursor == tabUnderCursor && priv->isCursorOverClose == isMouseOverClose) {
        return;
    }
    priv->tabIdxUnderCursor = tabUnderCursor;
    priv->isCursorOverClose = isMouseOverClose;
    TriggerRepaint(priv->hwnd);
}

static int TabFromMousePos(TabsCtrl* ctrl, int x, int y, bool& isMouseOverClose) {
    POINT mousePos = {x, y};
    for (size_t i = 0; i < ctrl->priv->tabInfos.size(); i++) {
        auto& ti = ctrl->priv->tabInfos[i];
        if (PtInRect(&ti->tabRect, mousePos)) {
            isMouseOverClose = PtInRect(&ti->closeRect, mousePos);
            return (int)i;
        }
    }
    return -1;
}

static void OnMouseMove(TabsCtrl* ctrl) {
    auto priv = ctrl->priv;
    auto mousePos = GetCursorPosInHwnd(priv->hwnd);
    bool isMouseOverClose = false;
    auto tabIdx = TabFromMousePos(ctrl, mousePos.x, mousePos.y, isMouseOverClose);

    SetTabUnderCursor(ctrl, tabIdx, isMouseOverClose);
    TrackMouseLeave(priv->hwnd);
}

static void OnLeftButtonUp(TabsCtrl* ctrl) {
    auto priv = ctrl->priv;
    auto mousePos = GetCursorPosInHwnd(priv->hwnd);
    bool isMouseOverClose;
    auto tabIdx = TabFromMousePos(ctrl, mousePos.x, mousePos.y, isMouseOverClose);
    if (tabIdx == -1) {
        return;
    }
    if (isMouseOverClose) {
        if (ctrl->onTabClosed) {
            ctrl->onTabClosed(ctrl, priv->state, tabIdx);
        }
        return;
    }
    if (tabIdx == priv->state->selectedItem) {
        return;
    }
    if (ctrl->onTabSelected) {
        ctrl->onTabSelected(ctrl, priv->state, tabIdx);
    }
}

static LRESULT CALLBACK TabsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    UNUSED(uIdSubclass);
    TabsCtrl* ctrl = (TabsCtrl*)dwRefData;
    TabsCtrlPrivate* priv = ctrl->priv;
    // CrashIf(ctrl->hwnd != (HWND)lp);

    // TraceMsg(msg);

    if (WM_ERASEBKGND == msg) {
        return TRUE; // tells Windows we handle background erasing so it doesn't do it
    }

    // This is needed in order to receive WM_MOUSEMOVE messages
    if (WM_NCHITTEST == msg) {
        // TODO: or just return HTCLIENT always?
        if (hwnd == GetCapture()) {
            return HTCLIENT;
        }
        auto mousePos = GetCursorPosInHwnd(ctrl->priv->hwnd);
        bool isMouseOverClose;
        auto tabIdx = TabFromMousePos(ctrl, mousePos.x, mousePos.y, isMouseOverClose);
        if (-1 == tabIdx) {
            return HTTRANSPARENT;
        }
        return HTCLIENT;
    }

    if (WM_NCDESTROY == msg) {
        RemoveWindowSubclass(GetParent(priv->hwnd), TabsParentProc, 0);
        RemoveWindowSubclass(priv->hwnd, TabsProc, 0);
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

    if (WM_PAINT == msg) {
        CrashIf(priv->hwnd != hwnd);
        Paint(ctrl);
        return 0;
    }

    if (WM_LBUTTONDOWN == msg) {
        return 0;
    }

    if (WM_LBUTTONUP == msg) {
        OnLeftButtonUp(ctrl);
        return 0;
    }

    if (WM_MOUSELEAVE == msg) {
        SetTabUnderCursor(ctrl, -1, false);
        return 0;
    }

    if (WM_MOUSEMOVE == msg) {
        OnMouseMove(ctrl);
        return 0;
    }

    if (WM_SIZE == msg) {
        long dx = LOWORD(lp);
        long dy = HIWORD(lp);
        priv->size = MakeSize(dx, dy);
        LayoutTabs(ctrl);
        return 0;
    }

    return DefSubclassProc(hwnd, msg, wp, lp);
}

TabsCtrl* AllocTabsCtrl(HWND parent, RECT initialPosition) {
    auto w = new TabsCtrl;
    w->parent = parent;
    w->initialPos = initialPosition;
    return w;
}

void SetFont(TabsCtrl* ctrl, HFONT font) {
    auto priv = ctrl->priv;
    priv->font = font;
    GetObject(font, sizeof(LOGFONTW), &priv->logFont);

    ScopedGetDC hdc(priv->hwnd);
    ScopedSelectFont prevFont(hdc, priv->font);
    GetTextMetrics(hdc, &priv->fontMetrics);
    priv->fontDy = priv->fontMetrics.tmHeight;
}

bool CreateTabsCtrl(TabsCtrl* ctrl) {
    auto r = ctrl->initialPos;
    auto x = r.left;
    auto y = r.top;
    auto dx = RectDx(r);
    auto dy = RectDy(r);
    DWORD exStyle = 0;
    DWORD style = WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE | TCS_FOCUSNEVER | TCS_FIXEDWIDTH | TCS_FORCELABELLEFT;
    HINSTANCE h = GetModuleHandleW(nullptr);
    auto hwnd = CreateWindowExW(exStyle, WC_TABCONTROL, L"", style, x, y, dx, dy, ctrl->parent, nullptr, h, ctrl);

    if (hwnd == nullptr) {
        return false;
    }

    auto priv = new TabsCtrlPrivate(hwnd);
    r = ctrl->initialPos;
    priv->size = MakeSize(RectDx(r), RectDy(r));
    priv->hwnd = hwnd;
    ctrl->priv = priv;
    SetFont(ctrl, GetDefaultGuiFont());

    SetWindowSubclass(hwnd, TabsProc, 0, (DWORD_PTR)ctrl);
    // SetWindowSubclass(GetParent(hwnd), TabsParentProc, 0, (DWORD_PTR)ctrl);
    return true;
}

void DeleteTabsCtrl(TabsCtrl* ctrl) {
    if (ctrl) {
        DeleteObject(ctrl->priv->font);
        delete ctrl->priv;
    }
    delete ctrl;
}

void SetState(TabsCtrl* ctrl, TabsCtrlState* state) {
    auto priv = ctrl->priv;
    priv->state = state;
    priv->tabInfos.Reset();

    // measure size of tab's title
    auto& tabInfos = priv->tabInfos;
    for (auto& tab : state->tabs) {
        auto ti = new TabItemInfo();
        tabInfos.Append(ti);
        ti->titleSize = MakeSize(0, 0);
        if (!tab->title.IsEmpty()) {
            ti->title = wstrFromUtf8(tab->title);
            const WCHAR* s = ti->title.Get();
            ti->titleSize = TextSizeInHwnd2(priv->hwnd, s, priv->font);
        }
        if (!tab->toolTip.IsEmpty()) {
            ti->toolTip = wstrFromUtf8(tab->toolTip);
        }
    }
    LayoutTabs(ctrl);
    // TODO: should use mouse position to determine this
    // TODO: calculate isHighlighted
    priv->isCursorOverClose = false;
}

SIZE GetIdealSize(TabsCtrl* ctrl) {
    return ctrl->priv->idealSize;
}

void SetPos(TabsCtrl* ctrl, RECT& r) {
    MoveWindow(ctrl->priv->hwnd, &r);
}

/* ----- */

using Gdiplus::Color;
using Gdiplus::CompositingQualityHighQuality;
using Gdiplus::Font;
using Gdiplus::Graphics;
using Gdiplus::GraphicsPath;
using Gdiplus::PathData;
using Gdiplus::Pen;
using Gdiplus::Region;
using Gdiplus::SolidBrush;
using Gdiplus::StringAlignmentCenter;
using Gdiplus::StringFormat;
using Gdiplus::TextRenderingHintClearTypeGridFit;
using Gdiplus::UnitPixel;

TabPainter::TabPainter(TabsCtrl2* ctrl, Size tabSize) {
    tabsCtrl = ctrl;
    hwnd = tabsCtrl->hwnd;
    Reshape(tabSize.dx, tabSize.dy);
}

TabPainter::~TabPainter() {
    delete data;
}

// Calculates tab's elements, based on its width and height.
// Generates a GraphicsPath, which is used for painting the tab, etc.
bool TabPainter::Reshape(int dx, int dy) {
    dx--;
    if (width == dx && height == dy) {
        return false;
    }
    width = dx;
    height = dy;

    GraphicsPath shape;
    // define tab's body
    shape.AddRectangle(Gdiplus::Rect(0, 0, width, height));
    shape.SetMarker();

    // define "x"'s circle
    int c = int((float)height * 0.78f + 0.5f); // size of bounding square for the circle
    int maxC = DpiScale(hwnd, 17);
    if (height > maxC) {
        c = DpiScale(hwnd, 17);
    }
    Gdiplus::Point p(width - c - DpiScale(hwnd, 3), (height - c) / 2); // circle's position
    shape.AddEllipse(p.X, p.Y, c, c);
    shape.SetMarker();
    // define "x"
    int o = int((float)c * 0.286f + 0.5f); // "x"'s offset
    shape.AddLine(p.X + o, p.Y + o, p.X + c - o, p.Y + c - o);
    shape.StartFigure();
    shape.AddLine(p.X + c - o, p.Y + o, p.X + o, p.Y + c - o);
    shape.SetMarker();

    delete data;
    data = new PathData();
    shape.GetPathData(data);
    return true;
}

// Finds the index of the tab, which contains the given point.
int TabPainter::IndexFromPoint(int x, int y, bool* inXbutton) {
    Gdiplus::Point point(x, y);
    Graphics gfx(hwnd);
    GraphicsPath shapes(data->Points, data->Types, data->Count);
    GraphicsPath shape;
    Gdiplus::GraphicsPathIterator iterator(&shapes);
    iterator.NextMarker(&shape);

    Rect rClient = ClientRect(hwnd);
    float yPosTab = inTitlebar ? 0.0f : float(rClient.dy - height - 1);
    gfx.TranslateTransform(1.0f, yPosTab);
    for (int i = 0; i < Count(); i++) {
        Gdiplus::Point pt(point);
        gfx.TransformPoints(Gdiplus::CoordinateSpaceWorld, Gdiplus::CoordinateSpaceDevice, &pt, 1);
        if (shape.IsVisible(pt, &gfx)) {
            iterator.NextMarker(&shape);
            if (inXbutton) {
                *inXbutton = shape.IsVisible(pt, &gfx) ? true : false;
            }
            return i;
        }
        gfx.TranslateTransform(float(width + 1), 0.0f);
    }
    if (inXbutton) {
        *inXbutton = false;
    }
    return -1;
}

// Invalidates the tab's region in the client area.
void TabPainter::Invalidate(int index) {
    InvalidateRect(hwnd, nullptr, FALSE);
#if 0
    if (index < 0) {
        return;
    }

    Graphics gfx(hwnd);
    GraphicsPath shapes(data->Points, data->Types, data->Count);
    GraphicsPath shape;
    Gdiplus::GraphicsPathIterator iterator(&shapes);
    iterator.NextMarker(&shape);
    Region region(&shape);

    Rect rClient = ClientRect(hwnd);
    float yPosTab = inTitlebar ? 0.0f : float(rClient.dy - height - 1);
    gfx.TranslateTransform(float((width + 1) * index) + 1.0f, yPosTab);
    HRGN hRgn = region.GetHRGN(&gfx);
    InvalidateRgn(hwnd, hRgn, FALSE);
    DeleteObject(hRgn);
#endif
}

// Paints the tabs that intersect the window's update rectangle.
void TabPainter::Paint(HDC hdc, RECT& rc) {
    IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
#if 0
        // paint the background
        bool isTranslucentMode = inTitlebar && dwm::IsCompositionEnabled();
        if (isTranslucentMode) {
            PaintParentBackground(hwnd, hdc);
        } else {
            // note: not sure what color should be used here and painting
            // background works fine
            /*HBRUSH brush = CreateSolidBrush(colors.bar);
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);*/
        }
#else
    PaintParentBackground(hwnd, hdc);
#endif
    // TODO: GDI+ doesn't seem to cope well with SetWorldTransform
    XFORM ctm = {1.0, 0, 0, 1.0, 0, 0};
    SetWorldTransform(hdc, &ctm);

    COLORREF bgCol, textCol, xCol, circleCol;

    Graphics gfx(hdc);
    bgCol = GetAppColor(AppColor::TabBackgroundBg);
    gfx.Clear(GdiRgbFromCOLORREF(bgCol));

    gfx.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    gfx.SetCompositingQuality(CompositingQualityHighQuality);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    gfx.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    gfx.SetPageUnit(UnitPixel);
    GraphicsPath shapes(data->Points, data->Types, data->Count);
    GraphicsPath shape;
    Gdiplus::GraphicsPathIterator iterator(&shapes);

    SolidBrush br(Color(0, 0, 0));
    Pen pen(&br, 2.0f);

    Font f(hdc, tabsCtrl->hfont);
    // TODO: adjust these constant values for DPI?
    Gdiplus::RectF layout((float)DpiScale(hwnd, 3), 1.0f, float(width - DpiScale(hwnd, 20)), (float)height);
    StringFormat sf(StringFormat::GenericDefault());
    sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

    float yPosTab = inTitlebar ? 0.0f : float(ClientRect(hwnd).dy - height - 1);
    int nTabs = Count();
    logf("TabPainter::Paint: nTabs=%d\n", nTabs);
    for (int i = 0; i < nTabs; i++) {
        gfx.ResetTransform();
        gfx.TranslateTransform(1.f + (float)(width + 1) * i - (float)rc.left, yPosTab - (float)rc.top);

        if (!gfx.IsVisible(0, 0, width + 1, height + 1)) {
            continue;
        }

        // Get the correct colors based on the state and the current theme
        bgCol = GetAppColor(AppColor::TabBackgroundBg);
        textCol = GetAppColor(AppColor::TabBackgroundText);
        xCol = GetAppColor(AppColor::TabBackgroundCloseX);
        circleCol = GetAppColor(AppColor::TabBackgroundCloseCircle);

        if (selectedTabIdx == i) {
            bgCol = GetAppColor(AppColor::TabSelectedBg);
            textCol = GetAppColor(AppColor::TabSelectedText);
            xCol = GetAppColor(AppColor::TabSelectedCloseX);
            circleCol = GetAppColor(AppColor::TabSelectedCloseCircle);
        } else if (highlighted == i) {
            bgCol = GetAppColor(AppColor::TabHighlightedBg);
            textCol = GetAppColor(AppColor::TabHighlightedText);
            xCol = GetAppColor(AppColor::TabHighlightedCloseX);
            circleCol = GetAppColor(AppColor::TabHighlightedCloseCircle);
        }
        if (xHighlighted == i) {
            xCol = GetAppColor(AppColor::TabHoveredCloseX);
            circleCol = GetAppColor(AppColor::TabHoveredCloseCircle);
        }
        if (xClicked == i) {
            xCol = GetAppColor(AppColor::TabClickedCloseX);
            circleCol = GetAppColor(AppColor::TabClickedCloseCircle);
        }

        // paint tab's body
        gfx.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        iterator.NextMarker(&shape);
        br.SetColor(GdiRgbFromCOLORREF(bgCol));
        Gdiplus::Point points[4];
        shape.GetPathPoints(points, 4);
        Gdiplus::Rect body(points[0].X, points[0].Y, points[2].X - points[0].X, points[2].Y - points[0].Y);
        body.Inflate(0, 0);
        gfx.SetClip(body);
        body.Inflate(5, 5);
        gfx.FillRectangle(&br, body);
        gfx.ResetClip();

        // draw tab's text
        gfx.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        br.SetColor(GdiRgbFromCOLORREF(textCol));
        WCHAR* text = tabsCtrl->GetTabText(i);
        gfx.DrawString(text, -1, &f, layout, &sf, &br);

        // paint "x"'s circle
        iterator.NextMarker(&shape);
        // bool closeCircleEnabled = true;
        if ((xClicked == i || xHighlighted == i) /*&& closeCircleEnabled*/) {
            br.SetColor(GdiRgbFromCOLORREF(circleCol));
            gfx.FillPath(&br, &shape);
        }

        // paint "x"
        iterator.NextMarker(&shape);
        pen.SetColor(GdiRgbFromCOLORREF(xCol));
        gfx.DrawPath(&pen, &shape);
        iterator.Rewind();
    }
}

int TabPainter::Count() {
    int n = tabsCtrl->GetTabCount();
    return n;
}

Kind kindTabs = "tabs";

static void SendNotification(TabsCtrl2* tabsCtrl, uint code, int tab1, int tab2) {
    if (!tabsCtrl->onNotify) {
        return;
    }
    TabNotifyInfo info;
    info.nmhdr.hwndFrom = tabsCtrl->hwnd;
    info.nmhdr.idFrom = tabsCtrl->ctrlID;
    info.nmhdr.code = code;
    info.tabIdx1 = tab1;
    info.tabIdx2 = tab2;
    WmNotifyEvent ev{};
    ev.w = tabsCtrl;
    ev.hwnd = GetParent(tabsCtrl->hwnd);
    ev.msg = WM_NOTIFY;
    ev.code = code;
    ev.nmhdr = &info.nmhdr;
    ev.lp = (LPARAM)&info;
    tabsCtrl->onNotify(&ev);
}

TabsCtrl2::TabsCtrl2(HWND p) : WindowBase(p) {
    dwStyle = WS_CHILD | WS_CLIPSIBLINGS | TCS_FOCUSNEVER | TCS_FIXEDWIDTH | TCS_FORCELABELLEFT | WS_VISIBLE;
    winClass = WC_TABCONTROLW;
    kind = kindTabs;
}

TabsCtrl2::~TabsCtrl2() {
    delete tabPainter;
}

const char* CodeToString(uint code) {
    if (code == TCN_SELCHANGE) {
        return "TCN_SELCHANGE";
    }
    if (code == TCN_SELCHANGING) {
        return "TCN_SELCHANGING";
    }
    if (code == TCN_GETOBJECT) {
        return "TCN_GETOBJECT";
    }
    if (code == TCN_FOCUSCHANGE) {
        return "TCN_FOCUSCHANGE";
    }
    if (code == NM_CLICK) {
        return "NM_CLICK";
    }
    if (code == NM_DBLCLK) {
        return "NM_DBLCLK";
    }
    if (code == NM_RELEASEDCAPTURE) {
        return "NM_RELEASEDCAPTURE";
    }
    return "";
}

static void Handle_WM_NOTIFY(void* user, WndEvent* ev) {
    uint msg = ev->msg;

    CrashIf(msg != WM_NOTIFY);

    TabsCtrl2* w = (TabsCtrl2*)user;
    ev->w = w;
    LPARAM lp = ev->lp;
    NMHDR* hdr = (NMHDR*)lp;
    UINT code = hdr->code;

    logf("TabsCtrl2:Handle_WM_NOTIFY: code=%d (%s)\n", (int)code, CodeToString);
    if (TCN_SELCHANGING == code) {
        TabPainter* tab = w->tabPainter;
        // if we have permission to select the tab
        // TODO: Should we allow the switch of the tab if we are in process of printing?
        tab->Invalidate(tab->selectedTabIdx);
        tab->Invalidate(tab->nextTab);
        tab->selectedTabIdx = tab->nextTab;
    }

    if (w->onNotify) {
        WmNotifyEvent a{};
        CopyWndEvent cp(&a, ev);
        a.nmhdr = (NMHDR*)lp;
        a.code = code;

        w->onNotify(&a);
        if (a.didHandle) {
            return;
        }
    }

    if (TCN_SELCHANGING == code) {
        // send notification that the tab is selected
        SendNotification(w, TCN_SELCHANGE, -1, -1);
    }
    CrashIf(GetParent(w->hwnd) != (HWND)ev->hwnd);
}

bool TabsCtrl2::Create() {
    if (createToolTipsHwnd) {
        dwStyle |= TCS_TOOLTIPS;
    }
    tabPainter = new TabPainter(this, tabSize);

    bool ok = WindowBase::Create();
    if (!ok) {
        return false;
    }

    void* user = this;
    RegisterHandlerForMessage(hwnd, WM_NOTIFY, Handle_WM_NOTIFY, user);
    Subclass();
    return true;
}

void TabsCtrl2::WndProc(WndEvent* ev) {
    HWND hwnd = ev->hwnd;
    UINT msg = ev->msg;
    WPARAM wp = ev->wp;
    LPARAM lp = ev->lp;

    // DbgLogMsg("tree:", hwnd, msg, wp, ev->lp);

    TabsCtrl2* w = this;
    CrashIf(w->hwnd != (HWND)hwnd);

    PAINTSTRUCT ps;
    HDC hdc;
    int index;
    LPTCITEM tcs;

    TabPainter* tab = w->tabPainter;

    switch (msg) {
        case TCM_INSERTITEM:
            index = (int)wp;
            if (index <= tab->selectedTabIdx) {
                tab->selectedTabIdx++;
            }
            tab->xClicked = -1;
            InvalidateRgn(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);
            break;

        case TCM_SETITEM:
            // TODO: this should not be necessary
            index = (int)wp;
            tcs = (LPTCITEM)lp;
            if (TCIF_TEXT & tcs->mask) {
                tab->Invalidate(index);
            }
            break;

        case TCM_DELETEITEM:
            // TODO: this should not be necessary
            index = (int)wp;
            if (index < tab->selectedTabIdx) {
                tab->selectedTabIdx--;
            } else if (index == tab->selectedTabIdx) {
                tab->selectedTabIdx = -1;
            }
            tab->xClicked = -1;
            if (tab->Count()) {
                InvalidateRgn(hwnd, nullptr, FALSE);
                UpdateWindow(hwnd);
            }
            break;

        case TCM_DELETEALLITEMS:
            tab->selectedTabIdx = -1;
            tab->highlighted = -1;
            tab->xClicked = -1;
            tab->xHighlighted = -1;
            InvalidateRgn(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);
            break;

        case TCM_SETITEMSIZE:
            if (tab->Reshape(LOWORD(lp), HIWORD(lp))) {
                tab->xClicked = -1;
                if (tab->Count()) {
                    InvalidateRgn(hwnd, nullptr, FALSE);
                    UpdateWindow(hwnd);
                }
            }
            break;

        case TCM_SETCURSEL: {
            index = (int)wp;
            CrashIf(index >= tab->Count());
            if (index >= tab->Count()) {
                return;
            }
            int previous = tab->selectedTabIdx;
            if (index != tab->selectedTabIdx) {
                tab->Invalidate(tab->selectedTabIdx);
                tab->Invalidate(index);
                tab->selectedTabIdx = index;
                UpdateWindow(hwnd);
            }
            return;
        }

        case WM_NCHITTEST: {
            if (!tab->inTitlebar || hwnd == GetCapture()) {
                ev->result = HTCLIENT;
                ev->didHandle = true;
                return;
            }
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            if (-1 != tab->IndexFromPoint(pt.x, pt.y)) {
                ev->result = HTCLIENT;
                ev->didHandle = true;
                return;
            }
            ev->result = HTTRANSPARENT;
            ev->didHandle = true;
        }
            return;

        case WM_MOUSELEAVE:
            PostMessageW(hwnd, WM_MOUSEMOVE, 0xFF, 0);
            ev->result = 0;
            ev->didHandle = true;
            return;

        case WM_MOUSEMOVE: {
            tab->mouseCoordinates = lp;

            if (0xff != wp) {
                TrackMouseLeave(hwnd);
            }

            bool inX = false;
            int hl = wp == 0xFF ? -1 : tab->IndexFromPoint(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &inX);
            if (tab->isDragging && hl == -1) {
                // preserve the highlighted tab if it's dragged outside the tabs' area
                hl = tab->highlighted;
            }
            if (tab->highlighted != hl) {
                if (tab->isDragging) {
                    // send notification if the highlighted tab is dragged over another
                    int tabNo = tab->highlighted;
                    SendNotification(w, T_DRAG, tabNo, hl);
                }

                tab->Invalidate(hl);
                tab->Invalidate(tab->highlighted);
                tab->highlighted = hl;
            }
            int xHl = inX && !tab->isDragging ? hl : -1;
            if (tab->xHighlighted != xHl) {
                tab->Invalidate(xHl);
                tab->Invalidate(tab->xHighlighted);
                tab->xHighlighted = xHl;
            }
            if (!inX) {
                tab->xClicked = -1;
            }
        }
            ev->didHandle = true;
            return;

        case WM_LBUTTONDOWN:
            bool inX;
            tab->nextTab = tab->IndexFromPoint(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &inX);
            if (inX) {
                // send request to close the tab
                int next = tab->nextTab;
                // if we have permission to close the tab
                SendNotification(w, T_CLOSING, next, -1);
                tab->Invalidate(tab->nextTab);
                tab->xClicked = tab->nextTab;
            } else if (tab->nextTab != -1) {
                if (tab->nextTab != tab->selectedTabIdx) {
                    // send request to select tab
                    SendNotification(w, TCN_SELCHANGING, -1, -1);
                }
                tab->isDragging = true;
                SetCapture(hwnd);
            }
            ev->didHandle = true;
            return;

        case WM_LBUTTONUP:
            if (tab->xClicked != -1) {
                // send notification that the tab is closed
                int clicked = tab->xClicked;
                SendNotification(w, T_CLOSE, clicked, -1);
                tab->Invalidate(clicked);
                tab->xClicked = -1;
            }
            if (tab->isDragging) {
                tab->isDragging = false;
                ReleaseCapture();
            }
            ev->didHandle = true;
            return;

        case WM_MBUTTONDOWN:
            // middle-clicking unconditionally closes the tab
            {
                tab->nextTab = tab->IndexFromPoint(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                // send request to close the tab
                int next = tab->nextTab;
                SendNotification(w, T_CLOSING, next, -1);
            }
            ev->didHandle = true;
            return;

        case WM_MBUTTONUP:
            if (tab->xClicked != -1) {
                // send notification that the tab is closed
                int clicked = tab->xClicked;
                SendNotification(w, T_CLOSE, clicked, -1);
                tab->Invalidate(clicked);
                tab->xClicked = -1;
            }
            ev->didHandle = true;
            return;

        case WM_ERASEBKGND:
            ev->result = TRUE;
            ev->didHandle = true;
            return;

        case WM_PAINT: {
            RECT rc;
            GetUpdateRect(hwnd, &rc, FALSE);
            // TODO: when is wp != nullptr?
            hdc = wp ? (HDC)wp : BeginPaint(hwnd, &ps);

            DoubleBuffer buffer(hwnd, Rect::FromRECT(rc));
            tab->Paint(buffer.GetDC(), rc);
            buffer.Flush(hdc);

            ValidateRect(hwnd, nullptr);
            if (!wp) {
                EndPaint(hwnd, &ps);
            }
            ev->didHandle = true;
            return;
        }

#if 0
        case WM_SIZE: {
            WindowInfo* win = FindWindowInfoByHwnd(hwnd);
            if (win) {
                UpdateTabWidth(win);
            }
        }
            break;
#endif
    }
}

Size TabsCtrl2::GetIdealSize() {
    Size sz{32, 128};
    return sz;
}

int TabsCtrl2::GetTabCount() {
    int n = TabCtrl_GetItemCount(hwnd);
    return n;
}

// TODO: remove in favor of std::string_view version
int TabsCtrl2::InsertTab(int idx, const WCHAR* ws) {
    CrashIf(idx < 0);

    TCITEMW item{0};
    item.mask = TCIF_TEXT;
    item.pszText = (WCHAR*)ws;
    int insertedIdx = TabCtrl_InsertItem(hwnd, idx, &item);
    return insertedIdx;
}

int TabsCtrl2::InsertTab(int idx, std::string_view sv) {
    CrashIf(idx < 0);

    TCITEMW item{0};
    item.mask = TCIF_TEXT;
    AutoFreeWstr s = strconv::Utf8ToWstr(sv);
    item.pszText = s.Get();
    int insertedIdx = TabCtrl_InsertItem(hwnd, idx, &item);
    return insertedIdx;
}

void TabsCtrl2::RemoveTab(int idx) {
    CrashIf(idx < 0);
    CrashIf(idx >= GetTabCount());
    BOOL ok = TabCtrl_DeleteItem(hwnd, idx);
    CrashIf(!ok);
}

void TabsCtrl2::RemoveAllTabs() {
    TabCtrl_DeleteAllItems(hwnd);
}

// TODO: remove in favor of std::string_view version
void TabsCtrl2::SetTabText(int idx, const WCHAR* ws) {
    CrashIf(idx < 0);
    CrashIf(idx >= GetTabCount());

    TCITEMW item{0};
    item.mask = TCIF_TEXT;
    item.pszText = (WCHAR*)ws;
    TabCtrl_SetItem(hwnd, idx, &item);
}

void TabsCtrl2::SetTabText(int idx, std::string_view sv) {
    CrashIf(idx < 0);
    CrashIf(idx >= GetTabCount());

    TCITEMW item{0};
    item.mask = TCIF_TEXT;
    AutoFreeWstr s = strconv::Utf8ToWstr(sv);
    item.pszText = s.Get();
    TabCtrl_SetItem(hwnd, idx, &item);
}

// result is valid until next call to GetTabText()
// TODO:
WCHAR* TabsCtrl2::GetTabText(int idx) {
    CrashIf(idx < 0);
    CrashIf(idx >= GetTabCount());

    WCHAR buf[512]{0};
    TCITEMW item{0};
    item.mask = TCIF_TEXT;
    item.pszText = buf;
    item.cchTextMax = dimof(buf) - 1; // -1 just in case
    TabCtrl_GetItem(hwnd, idx, &item);
    lastTabText.Set(buf);
    return lastTabText.Get();
}

int TabsCtrl2::GetSelectedTabIndex() {
    int idx = TabCtrl_GetCurSel(hwnd);
    logf("TabsCtrl2::GetSelectedTabIndex: idx=%d\n", idx);
    return idx;
}

int TabsCtrl2::SetSelectedTabByIndex(int idx) {
    int prevSelectedIdx = TabCtrl_SetCurSel(hwnd, idx);
    logf("TabsCtrl2::SetSelectedTabByIndex: idx=%d, prev=%d\n", idx, prevSelectedIdx);
    return prevSelectedIdx;
}

void TabsCtrl2::SetTabSize(Size sz) {
    tabSize = sz;
    if (hwnd) {
        TabCtrl_SetItemSize(hwnd, sz.dx, sz.dy);
    }
}

void TabsCtrl2::SetToolTipsHwnd(HWND hwndTooltip) {
    TabCtrl_SetToolTips(hwnd, hwndTooltip);
}

HWND TabsCtrl2::GetToolTipsHwnd() {
    HWND res = TabCtrl_GetToolTips(hwnd);
    return res;
}
