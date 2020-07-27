/* Copyright 2020 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "utils/BaseUtil.h"
#include "utils/ScopedWin.h"
#include "utils/WinDynCalls.h"
#include "utils/Dpi.h"
#include "utils/FileUtil.h"
#include "utils/GdiPlusUtil.h"
#include "utils/UITask.h"
#include "utils/WinUtil.h"
#include "utils/Log.h"

#include "wingui/WinGui.h"
#include "wingui/Layout.h"
#include "wingui/Window.h"
#include "wingui/TreeModel.h"
#include "wingui/TreeCtrl.h"
#include "wingui/TabsCtrl.h"

#include "Annotation.h"
#include "EngineBase.h"
#include "EngineCreate.h"
#include "DisplayMode.h"
#include "SettingsStructs.h"
#include "AppColors.h"
#include "Controller.h"
#include "DisplayModel.h"
#include "GlobalPrefs.h"
#include "ProgressUpdateUI.h"
#include "Notifications.h"
#include "SumatraPDF.h"
#include "WindowInfo.h"
#include "TabInfo.h"
#include "resource.h"
#include "Commands.h"
#include "Caption.h"
#include "Menu.h"
#include "TableOfContents.h"
#include "Tabs.h"

constexpr int kTabBarHeight = 24;
constexpr int kMinTabWidth = 200;

int GetTabbarHeight(HWND hwnd, float factor) {
    int dy = DpiScale(hwnd, kTabBarHeight);
    return (int)(dy * factor);
}

static inline Size GetTabSize(HWND hwnd) {
    int dx = DpiScale(hwnd, std::max(gGlobalPrefs->tabWidth, kMinTabWidth));
    int dy = DpiScale(hwnd, kTabBarHeight);
    return Size(dx, dy);
}

static void SetTabTitle(TabInfo* tab) {
    WindowInfo* win = tab->win;
    int idx = win->tabs.Find(tab);
    WCHAR* title = (WCHAR*)tab->GetTabTitle();
    win->tabsCtrl->SetTabText(idx, title);
}

static void SwapTabs(WindowInfo* win, int tab1, int tab2) {
    if (tab1 == tab2 || tab1 < 0 || tab2 < 0) {
        return;
    }

    auto&& tabs = win->tabs;
    std::swap(tabs.at(tab1), tabs.at(tab2));
    SetTabTitle(tabs.at(tab1));
    SetTabTitle(tabs.at(tab2));

    int current = win->tabsCtrl->GetSelectedTabIndex();
    int newSelected = tab1;
    if (tab1 == current) {
        newSelected = tab2;
    }
    win->tabsCtrl->SetSelectedTabByIndex(newSelected);
}

static void RemoveTab(WindowInfo* win, int idx) {
    TabInfo* tab = win->tabs.at(idx);
    UpdateTabFileDisplayStateForTab(tab);
    win->tabSelectionHistory->Remove(tab);
    win->tabs.Remove(tab);
    if (tab == win->currentTab) {
        win->ctrl = nullptr;
        win->currentTab = nullptr;
    }
    delete tab;
    win->tabsCtrl->RemoveTab(idx);
    UpdateTabWidth(win);
}

// On tab selection, we save the data for the tab which is losing selection and
// load the data of the selected tab into the WindowInfo.
static void TabsOnNotify(WindowInfo* win, WmNotifyEvent* ev) {
    NMHDR* data = ev->nmhdr;
    TabNotifyInfo* notifyInfo = (TabNotifyInfo*)ev->lp;
    int current;

    switch (data->code) {
        case TCN_SELCHANGING:
            // TODO: Should we allow the switch of the tab if we are in process of printing?
            SaveCurrentTabInfo(win);
            return;

        case TCN_SELCHANGE:
            current = win->tabsCtrl->GetSelectedTabIndex();
            LoadModelIntoTab(win->tabs.at(current));
            break;

#if 0
        case T_CLOSING:
            // allow the closure
            return;
#endif
        case T_CLOSE: {
            int tab1 = notifyInfo->tabIdx1;
            current = win->tabsCtrl->GetSelectedTabIndex();
            if (tab1 == current) {
                CloseTab(win);
            } else {
                RemoveTab(win, tab1);
            }
        } break;

        case T_DRAG: {
            int tab1 = notifyInfo->tabIdx1;
            int tab2 = notifyInfo->tabIdx2;
            SwapTabs(win, tab1, tab2);
        } break;
    }
    return;
}

void CreateTabbar(WindowInfo* win) {
    TabsCtrl2* tabsCtrl = new TabsCtrl2(win->hwndFrame);
    tabsCtrl->Create();

    HWND hwndTabBar = tabsCtrl->hwnd;

    tabsCtrl->onNotify = [win](WmNotifyEvent* ev) {
        if (!WindowInfoStillValid(win)) {
            return;
        }
        TabsOnNotify(win, ev);
    };

    Size tabSize = GetTabSize(win->hwndFrame);
    tabsCtrl->SetTabSize(tabSize);
    win->tabsCtrl = tabsCtrl;

    win->tabSelectionHistory = new Vec<TabInfo*>();
}

// verifies that TabInfo state is consistent with WindowInfo state
static NO_INLINE void VerifyTabInfo(WindowInfo* win, TabInfo* tdata) {
    CrashIf(!tdata || !win || tdata->ctrl != win->ctrl);
    AutoFreeWstr winTitle(win::GetText(win->hwndFrame));
    SubmitCrashIf(!str::Eq(winTitle.Get(), tdata->frameTitle));
    bool expectedTocVisibility = tdata->showToc; // if not in presentation mode
    if (PM_DISABLED != win->presentation) {
        expectedTocVisibility = false; // PM_BLACK_SCREEN, PM_WHITE_SCREEN
        if (PM_ENABLED == win->presentation) {
            expectedTocVisibility = tdata->showTocPresentation;
        }
    }
    SubmitCrashIf(win->tocVisible != expectedTocVisibility);
    SubmitCrashIf(tdata->canvasRc != win->canvasRc);
}

// Must be called when the active tab is losing selection.
// This happens when a new document is loaded or when another tab is selected.
void SaveCurrentTabInfo(WindowInfo* win) {
    if (!win) {
        return;
    }

    int current = win->tabsCtrl->GetSelectedTabIndex();
    if (-1 == current) {
        return;
    }
    CrashIf(win->currentTab != win->tabs.at(current));

    TabInfo* tab = win->currentTab;
    if (win->tocLoaded) {
        TocTree* tocTree = tab->ctrl->GetToc();
        UpdateTocExpansionState(tab->tocState, win->tocTreeCtrl, tocTree);
    }
    VerifyTabInfo(win, tab);

    // update the selection history
    win->tabSelectionHistory->Remove(tab);
    win->tabSelectionHistory->Append(tab);
}

void UpdateCurrentTabBgColor(WindowInfo* win) {
    TabPainter* tab = win->tabsCtrl->tabPainter;
    if (win->AsEbook()) {
        COLORREF txtCol;
        GetEbookUiColors(txtCol, tab->currBgCol);
    } else {
        // TODO: match either the toolbar (if shown) or background
        tab->currBgCol = kDefaultTabBgCol;
    }
    RepaintNow(win->tabsCtrl->hwnd);
}

// On load of a new document we insert a new tab item in the tab bar.
TabInfo* CreateNewTab(WindowInfo* win, const WCHAR* filePath) {
    CrashIf(!win);
    if (!win) {
        return nullptr;
    }

    TabInfo* tab = new TabInfo(win, filePath);
    win->tabs.Append(tab);
    tab->canvasRc = win->canvasRc;

    int idx = (int)win->tabs.size() - 1;
    int insertedIdx = win->tabsCtrl->InsertTab(idx, (WCHAR*)tab->GetTabTitle());
    CrashIf(insertedIdx == -1);
    win->tabsCtrl->SetSelectedTabByIndex(idx);
    UpdateTabWidth(win);
    return tab;
}

// Refresh the tab's title
void TabsOnChangedDoc(WindowInfo* win) {
    TabInfo* tab = win->currentTab;
    CrashIf(!tab != !win->tabs.size());
    if (!tab) {
        return;
    }

    int idx = win->tabs.Find(tab);
    int selectedIdx = win->tabsCtrl->GetSelectedTabIndex();
    CrashIf(idx != selectedIdx);
    VerifyTabInfo(win, tab);
    SetTabTitle(tab);
}

// Called when we're closing a document
void TabsOnCloseDoc(WindowInfo* win) {
    if (win->tabs.size() == 0) {
        return;
    }

    DisplayModel* dm = win->AsFixed();
    if (dm) {
        EngineBase* engine = dm->GetEngine();
        if (EngineHasUnsavedAnnotations(engine)) {
            // TODO: warn about unsaved changed
            logf("File has unsaved changed\n");
        }
    }

    int current = win->tabsCtrl->GetSelectedTabIndex();
    RemoveTab(win, current);

    if (win->tabs.size() > 0) {
        TabInfo* tab = win->tabSelectionHistory->Pop();
        int idx = win->tabs.Find(tab);
        win->tabsCtrl->SetSelectedTabByIndex(idx);
        LoadModelIntoTab(tab);
    }
}

// Called when we're closing an entire window (quitting)
void TabsOnCloseWindow(WindowInfo* win) {
    win->tabsCtrl->RemoveAllTabs();
    win->tabSelectionHistory->Reset();
    win->currentTab = nullptr;
    win->ctrl = nullptr;
    DeleteVecMembers(win->tabs);
}

static void ShowTabBar(WindowInfo* win, bool show) {
    if (show == win->tabsVisible) {
        return;
    }
    win->tabsVisible = show;
    win->tabsCtrl->SetIsVisible(show);
    RelayoutWindow(win);
}

void UpdateTabWidth(WindowInfo* win) {
    int count = (int)win->tabs.size();
    bool showSingleTab = gGlobalPrefs->useTabs || win->tabsInTitlebar;
    bool showTabs = (count > 1) || (showSingleTab && (count > 0));
    if (!showTabs) {
        ShowTabBar(win, false);
        return;
    }
    ShowTabBar(win, true);
    Rect rect = ClientRect(win->tabsCtrl->hwnd);
    Size tabSize = GetTabSize(win->hwndFrame);
    auto maxDx = (rect.dx - 3) / count;
    tabSize.dx = std::min(tabSize.dx, maxDx);
    win->tabsCtrl->SetTabSize(tabSize);
}

void SetTabsInTitlebar(WindowInfo* win, bool inTitlebar) {
    if (inTitlebar == win->tabsInTitlebar) {
        return;
    }
    win->tabsInTitlebar = inTitlebar;
    TabPainter* tab = win->tabsCtrl->tabPainter;
    tab->inTitlebar = inTitlebar;
    SetParent(win->tabsCtrl->hwnd, inTitlebar ? win->hwndCaption : win->hwndFrame);
    ShowWindow(win->hwndCaption, inTitlebar ? SW_SHOW : SW_HIDE);
    if (inTitlebar != win->isMenuHidden) {
        ShowHideMenuBar(win);
    }
    if (inTitlebar) {
        CaptionUpdateUI(win, win->caption);
        RelayoutCaption(win);
    } else if (dwm::IsCompositionEnabled()) {
        // remove the extended frame
        MARGINS margins = {0};
        dwm::ExtendFrameIntoClientArea(win->hwndFrame, &margins);
        win->extendedFrameHeight = 0;
    }
    uint flags = SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOSIZE | SWP_NOMOVE;
    SetWindowPos(win->hwndFrame, nullptr, 0, 0, 0, 0, flags);
}

// Selects the given tab (0-based index).
void TabsSelect(WindowInfo* win, int tabIndex) {
    int count = (int)win->tabs.size();
    if (count < 2 || tabIndex < 0 || tabIndex >= count) {
        return;
    }
#if 0
    NMHDR ntd = {nullptr, 0, TCN_SELCHANGING};
    if (TabsOnNotify(win, (LPARAM)&ntd)) {
        return;
    }
#endif
    win->currentTab = win->tabs.at(tabIndex);
    AutoFree path = strconv::WstrToUtf8(win->currentTab->filePath);
    logf("TabsSelect: tabIndex: %d, new win->currentTab: 0x%p, path: '%s'\n", tabIndex, win->currentTab, path.Get());
    int prevIdx = win->tabsCtrl->SetSelectedTabByIndex(tabIndex);
#if 0
    if (prevIdx != -1) {
        ntd.code = TCN_SELCHANGE;
        TabsOnNotify(win, (LPARAM)&ntd);
    }
#endif
}

// Selects the next (or previous) tab.
void TabsOnCtrlTab(WindowInfo* win, bool reverse) {
    int count = (int)win->tabs.size();
    if (count < 2) {
        return;
    }
    int idx = win->tabsCtrl->GetSelectedTabIndex() + 1;
    if (reverse) {
        idx -= 2;
    }
    idx += count; // ensure > 0
    idx = idx % count;
    TabsSelect(win, idx);
}
