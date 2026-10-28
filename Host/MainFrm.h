#pragma once

#include "PluginManager.h"
#include "PluginHost.h"
#include "RegistryWatcher.h"

class CMainFrame : public CFrameWnd
{
public:
    CMainFrame() = default;

    void AppendLog(LPCWSTR pszMessage);

protected:
    BOOL PreCreateWindow(CREATESTRUCT& cs) override;
    void RecalcLayout(BOOL bNotify = TRUE) override;

    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnDestroy();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnPluginsRefresh();
    afx_msg void OnPluginsAutoDetect();
    afx_msg void OnUpdatePluginsAutoDetect(CCmdUI* pCmdUI);
    afx_msg void OnPluginCommand(UINT nID);
    afx_msg void OnUpdatePluginsIndicator(CCmdUI* pCmdUI);
    afx_msg LRESULT OnPluginRegistryChanged(WPARAM, LPARAM);
    DECLARE_MESSAGE_MAP()

private:
    // Maps (nID - ID_PLUGIN_CMD_FIRST) to the plugin and its own command ID.
    struct CommandBinding
    {
        size_t pluginIndex;
        ULONG  commandID;
    };

    void LoadPlugins();
    void SyncPlugins();
    void OnPluginsChanged(const PluginSyncResult& result);
    void UnloadPlugins();
    void SetAutoDetect(bool enable);
    void BuildPluginMenu();
    void ClearPluginMenu();
    void PopulatePluginList();
    CMenu* GetPluginsMenu();

    CPluginManager                  m_pluginManager;
    CComPtr<CPluginHostServices>    m_spHost;
    std::vector<CommandBinding>     m_commandBindings;
    CRegistryWatcher                m_registryWatcher;
    bool                            m_bAutoDetect = false;
    int                             m_nPluginCallDepth = 0;   // >0 while inside ExecuteCommand

    CString     m_strPluginStatus;
    CStatusBar  m_wndStatusBar;
    CListCtrl   m_wndPluginList;
    CEdit       m_wndLog;
};
