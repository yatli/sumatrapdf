/* Copyright 2020 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

struct TabsCtrl;
class TabsCtrlState;

typedef std::function<void(TabsCtrl*, TabsCtrlState*, int)> TabSelectedCb;
typedef std::function<void(TabsCtrl*, TabsCtrlState*, int)> TabClosedCb;

class TabItem {
  public:
    TabItem(const std::string_view title, const std::string_view toolTip);

    str::Str title;
    str::Str toolTip;

    str::Str iconSvgPath;
};

class TabsCtrlState {
  public:
    Vec<TabItem*> tabs;
    int selectedItem = 0;
};

class TabsCtrlPrivate;

struct TabsCtrl {
    // creation parameters. must be set before CreateTabsCtrl
    HWND parent = nullptr;
    RECT initialPos = {};

    TabSelectedCb onTabSelected = nullptr;
    TabClosedCb onTabClosed = nullptr;

    TabsCtrlPrivate* priv;
};

/* Creation sequence:
- AllocTabsCtrl()
- set creation parameters
- CreateTabsCtrl()
*/

TabsCtrl* AllocTabsCtrl(HWND parent, RECT initialPosition);
bool CreateTabsCtrl(TabsCtrl*);
void DeleteTabsCtrl(TabsCtrl*);
void SetState(TabsCtrl*, TabsCtrlState*);
SIZE GetIdealSize(TabsCtrl*);
void SetPos(TabsCtrl*, RECT&);
void SetFont(TabsCtrl*, HFONT);

/* TabsCtrl2 */

#define T_CLOSING (TCN_LAST + 1)
#define T_CLOSE (TCN_LAST + 2)
#define T_DRAG (TCN_LAST + 3)

struct TabsCtrl2;

constexpr COLORREF kDefaultTabBgCol = (COLORREF)-1;

struct TabPainter {
    TabsCtrl2* tabsCtrl{nullptr};
    Gdiplus::PathData* data{nullptr};
    int width{-1};
    int height{-1};
    HWND hwnd{nullptr};

    int selectedTabIdx{-1};
    int highlighted{-1};
    int xClicked{-1};
    int xHighlighted{-1};
    int nextTab{-1};
    bool isDragging{false};
    bool inTitlebar{false};
    LPARAM mouseCoordinates{0};
    COLORREF currBgCol{kDefaultTabBgCol};

    TabPainter(TabsCtrl2* ctrl, Size tabSize);
    ~TabPainter();
    bool Reshape(int dx, int dy);
    int IndexFromPoint(int x, int y, bool* inXbutton = nullptr);
    void Invalidate(int index);
    void Paint(HDC hdc, RECT& rc);
    int Count();
};

struct TabsCtrl2 : WindowBase {
    str::WStr lastTabText;
    bool createToolTipsHwnd{false};
    Size tabSize{32, 64};

    TabPainter* tabPainter{nullptr};

    // for all WM_NOTIFY messages
    WmNotifyHandler onNotify{nullptr};

    TabsCtrl2(HWND parent);
    ~TabsCtrl2() override;
    bool Create() override;

    void WndProc(WndEvent*) override;

    Size GetIdealSize() override;

    int InsertTab(int idx, std::string_view sv);
    int InsertTab(int idx, const WCHAR* ws);

    void RemoveTab(int idx);
    void RemoveAllTabs();

    void SetTabText(int idx, std::string_view sv);
    void SetTabText(int idx, const WCHAR* ws);

    WCHAR* GetTabText(int idx);

    int GetSelectedTabIndex();
    int SetSelectedTabByIndex(int idx);

    void SetTabSize(Size sz);
    int GetTabCount();

    void SetToolTipsHwnd(HWND);
    HWND GetToolTipsHwnd();
};
