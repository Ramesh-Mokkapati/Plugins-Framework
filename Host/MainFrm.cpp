#include "stdafx.h"
#include "PluginsFramework.h"
#include "MainFrm.h"

namespace
{
    // "Refresh Plugins", "Detect New Plugins Automatically" and a separator
    // precede the dynamic items in the Plugins menu.
    constexpr UINT kStaticPluginMenuItems = 3;

    constexpr UINT WM_PLUGIN_REGISTRY_CHANGED = WM_APP + 1;

    // regsvr32 writes several keys per (un)registration; wait for it to settle.
    constexpr UINT_PTR kSyncTimerId = 1;
    constexpr UINT     kSyncDelayMs = 750;

    constexpr wchar_t kSettingsSection[] = L"Settings";
    constexpr wchar_t kAutoDetectEntry[] = L"AutoDetectPlugins";

    const UINT kIndicators[] =
    {
        ID_SEPARATOR,
        ID_INDICATOR_PLUGINS,
        ID_INDICATOR_CAPS,
        ID_INDICATOR_NUM,
    };

    // Plugin-supplied names must not create accidental menu mnemonics.
    CString EscapeMenuText(const CString& text)
    {
        CString escaped(text);
        escaped.Replace(L"&", L"&&");
        return escaped;
    }
}

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_TIMER()
    ON_COMMAND(ID_PLUGINS_REFRESH, &CMainFrame::OnPluginsRefresh)
    ON_COMMAND(ID_PLUGINS_AUTODETECT, &CMainFrame::OnPluginsAutoDetect)
    ON_UPDATE_COMMAND_UI(ID_PLUGINS_AUTODETECT, &CMainFrame::OnUpdatePluginsAutoDetect)
    ON_COMMAND_RANGE(ID_PLUGIN_CMD_FIRST, ID_PLUGIN_CMD_LAST, &CMainFrame::OnPluginCommand)
    ON_UPDATE_COMMAND_UI(ID_INDICATOR_PLUGINS, &CMainFrame::OnUpdatePluginsIndicator)
    ON_MESSAGE(WM_PLUGIN_REGISTRY_CHANGED, &CMainFrame::OnPluginRegistryChanged)
END_MESSAGE_MAP()

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{
    if (!CFrameWnd::PreCreateWindow(cs))
        return FALSE;
    cs.dwExStyle &= ~WS_EX_CLIENTEDGE;
    cs.cx = 900;
    cs.cy = 600;
    return TRUE;
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
        return -1;

    if (!m_wndStatusBar.Create(this) ||
        !m_wndStatusBar.SetIndicators(kIndicators, _countof(kIndicators)))
        return -1;

    if (!m_wndPluginList.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT |
                                LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                CRect(), this, IDC_PLUGIN_LIST))
        return -1;
    m_wndPluginList.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
                                     LVS_EX_DOUBLEBUFFER);
    m_wndPluginList.InsertColumn(0, L"Plugin", LVCFMT_LEFT, 180);
    m_wndPluginList.InsertColumn(1, L"Version", LVCFMT_LEFT, 70);
    m_wndPluginList.InsertColumn(2, L"Status", LVCFMT_LEFT, 220);
    m_wndPluginList.InsertColumn(3, L"Description", LVCFMT_LEFT, 220);
    m_wndPluginList.InsertColumn(4, L"CLSID", LVCFMT_LEFT, 280);

    if (!m_wndLog.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE |
                         ES_AUTOVSCROLL | ES_READONLY,
                         CRect(), this, IDC_LOG))
        return -1;
    m_wndLog.SetFont(CFont::FromHandle(static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT))));
    m_wndPluginList.SetFont(m_wndLog.GetFont());

    m_spHost.Attach(new CPluginHostServices(this));
    LoadPlugins();
    SetAutoDetect(AfxGetApp()->GetProfileInt(kSettingsSection, kAutoDetectEntry, 1) != 0);
    return 0;
}

void CMainFrame::OnDestroy()
{
    m_registryWatcher.Stop();
    KillTimer(kSyncTimerId);

    // Plugins must be released while the window and COM are still alive.
    UnloadPlugins();
    if (m_spHost)
    {
        m_spHost->Detach();
        m_spHost.Release();
    }
    ::CoFreeUnusedLibraries();
    CFrameWnd::OnDestroy();
}

void CMainFrame::RecalcLayout(BOOL bNotify)
{
    CFrameWnd::RecalcLayout(bNotify);
    if (!m_wndPluginList.GetSafeHwnd() || !m_wndLog.GetSafeHwnd())
        return;

    // Client area left over after the status bar: plugin list on top, log below.
    CRect rc;
    RepositionBars(0, 0xFFFF, AFX_IDW_PANE_FIRST, reposQuery, &rc);
    const int listHeight = rc.Height() * 55 / 100;
    m_wndPluginList.MoveWindow(rc.left, rc.top, rc.Width(), listHeight);
    m_wndLog.MoveWindow(rc.left, rc.top + listHeight, rc.Width(), rc.Height() - listHeight);
}

void CMainFrame::LoadPlugins()
{
    PluginSyncResult result;
    HRESULT hr = m_pluginManager.DiscoverAndLoadPlugins(m_spHost, &result);
    if (FAILED(hr))
        AppendLog(L"Plugin discovery failed: " + HResultToString(hr));
    OnPluginsChanged(result);
}

// Incremental update used by automatic detection: plugins that are still
// registered keep running.
void CMainFrame::SyncPlugins()
{
    PluginSyncResult result;
    HRESULT hr = m_pluginManager.SyncPlugins(m_spHost, &result);
    if (FAILED(hr))
    {
        AppendLog(L"Plugin discovery failed: " + HResultToString(hr));
        return;
    }
    if (!result.changed)
        return;

    OnPluginsChanged(result);
    if (!result.removed.empty())
        ::CoFreeUnusedLibrariesEx(0, 0);   // let uninstallers delete the DLLs
}

void CMainFrame::OnPluginsChanged(const PluginSyncResult& result)
{
    for (const auto& info : result.removed)
        AppendLog(L"Unloaded plugin (unregistered): " + info.name);
    for (const auto& info : result.added)
        AppendLog(L"Loaded plugin: " + info.name + L" " + info.version);
    for (const auto& info : result.failed)
        AppendLog(L"Disabled plugin " + GuidToString(info.clsid) + L": " +
                  HResultToString(info.status));

    BuildPluginMenu();
    PopulatePluginList();

    m_strPluginStatus.Format(L"%zu plugin(s) enabled, %zu disabled",
                             m_pluginManager.GetActivePlugins().size(),
                             m_pluginManager.GetDisabledPlugins().size());
}

void CMainFrame::OnUpdatePluginsIndicator(CCmdUI* pCmdUI)
{
    pCmdUI->Enable();
    pCmdUI->SetText(m_strPluginStatus);
}

void CMainFrame::UnloadPlugins()
{
    ClearPluginMenu();
    HRESULT hr = m_pluginManager.UnloadAllPlugins();
    if (FAILED(hr))
        AppendLog(L"A plugin failed to shut down cleanly: " + HResultToString(hr));
}

CMenu* CMainFrame::GetPluginsMenu()
{
    CMenu* pMainMenu = GetMenu();
    if (!pMainMenu)
        return nullptr;

    // Locate the popup by its static Refresh command rather than by position.
    for (int i = 0; i < pMainMenu->GetMenuItemCount(); ++i)
    {
        CMenu* pSub = pMainMenu->GetSubMenu(i);
        if (pSub && pSub->GetMenuState(ID_PLUGINS_REFRESH, MF_BYCOMMAND) != static_cast<UINT>(-1))
            return pSub;
    }
    return nullptr;
}

void CMainFrame::ClearPluginMenu()
{
    m_commandBindings.clear();

    CMenu* pMenu = GetPluginsMenu();
    if (!pMenu)
        return;
    // DeleteMenu also destroys any per-plugin popup submenus.
    while (pMenu->GetMenuItemCount() > static_cast<int>(kStaticPluginMenuItems))
        pMenu->DeleteMenu(kStaticPluginMenuItems, MF_BYPOSITION);
    DrawMenuBar();
}

void CMainFrame::BuildPluginMenu()
{
    ClearPluginMenu();

    CMenu* pMenu = GetPluginsMenu();
    if (!pMenu)
        return;

    const auto plugins = m_pluginManager.GetActivePlugins();
    if (plugins.empty())
    {
        pMenu->AppendMenu(MF_STRING | MF_GRAYED, 0, L"(No plugins registered)");
        DrawMenuBar();
        return;
    }

    const size_t capacity = ID_PLUGIN_CMD_LAST - ID_PLUGIN_CMD_FIRST + 1;
    auto bindCommand = [&](size_t pluginIndex, ULONG commandID) -> UINT
    {
        if (m_commandBindings.size() >= capacity)
            return 0;
        m_commandBindings.push_back({ pluginIndex, commandID });
        return ID_PLUGIN_CMD_FIRST + static_cast<UINT>(m_commandBindings.size() - 1);
    };

    bool outOfIds = false;
    for (size_t i = 0; i < plugins.size() && !outOfIds; ++i)
    {
        const PluginInfo& plugin = plugins[i];

        if (plugin.commands.size() == 1)
        {
            UINT nID = bindCommand(i, plugin.commands.front().commandID);
            if (nID == 0) { outOfIds = true; break; }
            pMenu->AppendMenu(MF_STRING, nID, EscapeMenuText(plugin.name));
            continue;
        }

        CMenu subMenu;
        subMenu.CreatePopupMenu();
        for (const PluginCommand& cmd : plugin.commands)
        {
            UINT nID = bindCommand(i, cmd.commandID);
            if (nID == 0) { outOfIds = true; break; }
            subMenu.AppendMenu(MF_STRING, nID, EscapeMenuText(cmd.caption));
        }
        if (subMenu.GetMenuItemCount() > 0)
            pMenu->AppendMenu(MF_POPUP, reinterpret_cast<UINT_PTR>(subMenu.Detach()),
                              EscapeMenuText(plugin.name));
    }

    if (outOfIds)
        AppendLog(L"Plugin command ID range exhausted; some plugin commands were not added.");
    DrawMenuBar();
}

void CMainFrame::PopulatePluginList()
{
    m_wndPluginList.DeleteAllItems();

    auto addRow = [this](const PluginInfo& info, const CString& status)
    {
        int row = m_wndPluginList.InsertItem(m_wndPluginList.GetItemCount(), info.name);
        m_wndPluginList.SetItemText(row, 1, info.version);
        m_wndPluginList.SetItemText(row, 2, status);
        m_wndPluginList.SetItemText(row, 3, info.description);
        m_wndPluginList.SetItemText(row, 4, GuidToString(info.clsid));
    };

    for (const auto& info : m_pluginManager.GetActivePlugins())
        addRow(info, L"Enabled");
    for (const auto& info : m_pluginManager.GetDisabledPlugins())
        addRow(info, L"Disabled: " + HResultToString(info.status));
}

void CMainFrame::OnPluginsRefresh()
{
    AppendLog(L"Refreshing plugins...");
    UnloadPlugins();
    // Let COM unload DLLs that were unregistered since the last scan.
    ::CoFreeUnusedLibrariesEx(0, 0);
    LoadPlugins();
}

void CMainFrame::OnPluginsAutoDetect()
{
    SetAutoDetect(!m_bAutoDetect);
    AfxGetApp()->WriteProfileInt(kSettingsSection, kAutoDetectEntry, m_bAutoDetect ? 1 : 0);
}

void CMainFrame::OnUpdatePluginsAutoDetect(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(m_bAutoDetect ? 1 : 0);
}

void CMainFrame::SetAutoDetect(bool enable)
{
    if (enable == m_registryWatcher.IsRunning())
    {
        m_bAutoDetect = enable;
        return;
    }

    if (enable)
    {
        if (!m_registryWatcher.Start(GetSafeHwnd(), WM_PLUGIN_REGISTRY_CHANGED))
        {
            AppendLog(L"Could not start watching the registry; use Refresh Plugins instead.");
            m_bAutoDetect = false;
            return;
        }
        AppendLog(L"Automatic plugin detection is on.");
        // Catch anything registered while detection was off.
        SetTimer(kSyncTimerId, kSyncDelayMs, nullptr);
    }
    else
    {
        m_registryWatcher.Stop();
        KillTimer(kSyncTimerId);
        AppendLog(L"Automatic plugin detection is off.");
    }
    m_bAutoDetect = enable;
}

LRESULT CMainFrame::OnPluginRegistryChanged(WPARAM, LPARAM)
{
    // Restart the debounce timer on every change notification.
    if (m_bAutoDetect)
        SetTimer(kSyncTimerId, kSyncDelayMs, nullptr);
    return 0;
}

void CMainFrame::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent != kSyncTimerId)
    {
        CFrameWnd::OnTimer(nIDEvent);
        return;
    }

    // A plugin command may be pumping messages (e.g. showing a message box);
    // never release plugins underneath it. The timer stays armed and retries.
    if (m_nPluginCallDepth > 0)
        return;

    KillTimer(kSyncTimerId);
    SyncPlugins();
}

void CMainFrame::OnPluginCommand(UINT nID)
{
    const size_t slot = nID - ID_PLUGIN_CMD_FIRST;
    if (slot >= m_commandBindings.size())
        return;

    const CommandBinding binding = m_commandBindings[slot];
    ++m_nPluginCallDepth;
    HRESULT hr = m_pluginManager.ExecuteCommand(binding.pluginIndex, binding.commandID);
    --m_nPluginCallDepth;
    if (FAILED(hr))
    {
        CString msg;
        msg.Format(L"Plugin command %lu failed: ", binding.commandID);
        AppendLog(msg + HResultToString(hr));
    }
}

void CMainFrame::AppendLog(LPCWSTR pszMessage)
{
    if (!m_wndLog.GetSafeHwnd())
        return;

    SYSTEMTIME st;
    ::GetLocalTime(&st);
    CString line;
    line.Format(L"[%02u:%02u:%02u] %s\r\n", st.wHour, st.wMinute, st.wSecond, pszMessage);

    m_wndLog.SetSel(-1, -1);
    m_wndLog.ReplaceSel(line);
}
