#pragma once

#include "..\Shared\IPlugin.h"

struct PluginCommand
{
    ULONG   commandID = PLUGIN_DEFAULT_COMMAND;
    CString caption;
};

struct PluginInfo
{
    CLSID   clsid = CLSID_NULL;
    CString name;
    CString description;
    CString version;
    std::vector<PluginCommand> commands;    // never empty for an enabled plugin
    bool    enabled = false;
    HRESULT status = S_OK;                  // failure reason when !enabled
};

// What a SyncPlugins call changed.
struct PluginSyncResult
{
    std::vector<PluginInfo> added;      // newly enabled
    std::vector<PluginInfo> removed;    // were enabled, no longer in the category
    std::vector<PluginInfo> failed;     // newly disabled (first failure for that CLSID)
    bool changed = false;               // active or disabled set differs from before
};

// Discovers plugins registered under CATID_PluginsFramework, owns the live
// IPlugin pointers and routes commands to them. Must be used on the STA thread
// that called AfxOleInit, and emptied (UnloadAllPlugins) before COM shuts down.
class CPluginManager
{
public:
    CPluginManager() = default;
    ~CPluginManager();

    CPluginManager(const CPluginManager&) = delete;
    CPluginManager& operator=(const CPluginManager&) = delete;

    // Enumerates the component category and creates/initializes every plugin.
    // Previously loaded plugins are unloaded first, so this doubles as refresh.
    // pHostApp is handed to IPlugin::Initialize.
    HRESULT DiscoverAndLoadPlugins(IUnknown* pHostApp, PluginSyncResult* pResult = nullptr);

    // Brings the loaded set in line with the registry without disturbing
    // plugins that are still registered: newly registered CLSIDs are loaded,
    // unregistered ones are shut down and released, and previously disabled
    // ones are retried. Used for automatic detection of installs/uninstalls.
    // Plugin indices (see GetActivePlugins) may change.
    HRESULT SyncPlugins(IUnknown* pHostApp, PluginSyncResult* pResult = nullptr);

    // Calls IPlugin::Shutdown on every plugin and releases it.
    HRESULT UnloadAllPlugins();

    // Successfully loaded plugins; the vector index is the pluginIndex used by
    // ExecuteCommand.
    std::vector<PluginInfo> GetActivePlugins() const;

    // Plugins that are in the category but could not be created/initialized.
    const std::vector<PluginInfo>& GetDisabledPlugins() const { return m_disabled; }

    HRESULT ExecuteCommand(size_t pluginIndex, ULONG commandID);

private:
    struct LoadedPlugin
    {
        PluginInfo       info;
        CComPtr<IPlugin> spPlugin;
    };

    static HRESULT EnumerateRegisteredPlugins(std::vector<CLSID>& clsids);
    static HRESULT LoadPlugin(REFCLSID clsid, IUnknown* pHostApp,
                              PluginInfo& info, CComPtr<IPlugin>& spPlugin);
    static void QueryCommands(IPlugin* pPlugin, PluginInfo& info);

    std::vector<LoadedPlugin> m_active;
    std::vector<PluginInfo>   m_disabled;
};

CString GuidToString(REFGUID guid);
CString HResultToString(HRESULT hr);
