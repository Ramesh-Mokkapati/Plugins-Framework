#include "stdafx.h"
#include "PluginManager.h"

CPluginManager::~CPluginManager()
{
    // Normally already empty: the main frame unloads while COM is still alive.
    UnloadAllPlugins();
}

HRESULT CPluginManager::DiscoverAndLoadPlugins(IUnknown* pHostApp, PluginSyncResult* pResult)
{
    UnloadAllPlugins();
    return SyncPlugins(pHostApp, pResult);
}

HRESULT CPluginManager::SyncPlugins(IUnknown* pHostApp, PluginSyncResult* pResult)
{
    std::vector<CLSID> registered;
    HRESULT hr = EnumerateRegisteredPlugins(registered);
    if (FAILED(hr))
        return hr;

    auto isRegistered = [&registered](REFCLSID clsid)
    {
        return std::find(registered.begin(), registered.end(), clsid) != registered.end();
    };

    PluginSyncResult result;

    // 1. Shut down plugins that have left the category.
    for (auto it = m_active.begin(); it != m_active.end();)
    {
        if (isRegistered(it->info.clsid))
        {
            ++it;
            continue;
        }
        it->spPlugin->Shutdown();
        it->spPlugin.Release();
        result.removed.push_back(it->info);
        it = m_active.erase(it);
    }

    // 2. Load newcomers and retry everything that was disabled last time.
    std::vector<PluginInfo> previouslyDisabled;
    previouslyDisabled.swap(m_disabled);

    for (const CLSID& clsid : registered)
    {
        const bool alreadyActive = std::any_of(m_active.begin(), m_active.end(),
            [&clsid](const LoadedPlugin& p) { return p.info.clsid == clsid; });
        if (alreadyActive)
            continue;

        PluginInfo info;
        info.clsid = clsid;
        CComPtr<IPlugin> spPlugin;
        info.status = LoadPlugin(clsid, pHostApp, info, spPlugin);
        if (SUCCEEDED(info.status))
        {
            info.enabled = true;
            result.added.push_back(info);
            m_active.push_back({ std::move(info), spPlugin });
            continue;
        }

        const bool wasDisabled = std::any_of(previouslyDisabled.begin(), previouslyDisabled.end(),
            [&clsid](const PluginInfo& p) { return p.clsid == clsid; });
        if (!wasDisabled)
            result.failed.push_back(info);
        m_disabled.push_back(std::move(info));
    }

    result.changed = !result.added.empty() || !result.removed.empty() ||
                     !result.failed.empty() || m_disabled.size() != previouslyDisabled.size();
    if (pResult)
        *pResult = std::move(result);
    return S_OK;
}

HRESULT CPluginManager::EnumerateRegisteredPlugins(std::vector<CLSID>& clsids)
{
    clsids.clear();

    CComPtr<ICatInformation> spCatInfo;
    HRESULT hr = spCatInfo.CoCreateInstance(CLSID_StdComponentCategoriesMgr, nullptr,
                                            CLSCTX_INPROC_SERVER);
    if (FAILED(hr))
        return hr;

    CATID catids[] = { CATID_PluginsFramework };
    CComPtr<IEnumCLSID> spEnum;
    // cRequired = -1: ignore "Required Categories", match on implemented only.
    hr = spCatInfo->EnumClassesOfCategories(_countof(catids), catids,
                                            static_cast<ULONG>(-1), nullptr, &spEnum);
    if (FAILED(hr))
        return hr;

    CLSID clsid;
    ULONG fetched = 0;
    while (spEnum->Next(1, &clsid, &fetched) == S_OK && fetched == 1)
        clsids.push_back(clsid);
    return S_OK;
}

HRESULT CPluginManager::LoadPlugin(REFCLSID clsid, IUnknown* pHostApp,
                                   PluginInfo& info, CComPtr<IPlugin>& spPlugin)
{
    info.name = GuidToString(clsid);   // fallback until the plugin names itself

    HRESULT hr = ::CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IPlugin,
                                    reinterpret_cast<void**>(&spPlugin));
    if (FAILED(hr))
        return hr;

    CComBSTR bstrName, bstrDesc, bstrVersion;
    hr = spPlugin->GetPluginInfo(&bstrName, &bstrDesc, &bstrVersion);
    if (FAILED(hr))
        return hr;
    if (bstrName.Length() > 0)
        info.name = bstrName;
    info.description = bstrDesc;
    info.version = bstrVersion;

    hr = spPlugin->Initialize(pHostApp);
    if (FAILED(hr))
    {
        spPlugin.Release();
        return hr;
    }

    QueryCommands(spPlugin, info);
    return S_OK;
}

void CPluginManager::QueryCommands(IPlugin* pPlugin, PluginInfo& info)
{
    CComQIPtr<IPluginCommands, &IID_IPluginCommands> spCommands(pPlugin);
    ULONG count = 0;
    if (spCommands && SUCCEEDED(spCommands->GetCommandCount(&count)))
    {
        for (ULONG i = 0; i < count; ++i)
        {
            PluginCommand cmd;
            CComBSTR bstrCaption;
            if (SUCCEEDED(spCommands->GetCommandInfo(i, &cmd.commandID, &bstrCaption)))
            {
                cmd.caption = bstrCaption;
                info.commands.push_back(std::move(cmd));
            }
        }
    }

    if (info.commands.empty())
        info.commands.push_back({ PLUGIN_DEFAULT_COMMAND, info.name });
}

HRESULT CPluginManager::UnloadAllPlugins()
{
    HRESULT hrResult = S_OK;
    // Shut down in reverse load order.
    for (auto it = m_active.rbegin(); it != m_active.rend(); ++it)
    {
        if (it->spPlugin)
        {
            HRESULT hr = it->spPlugin->Shutdown();
            if (FAILED(hr) && SUCCEEDED(hrResult))
                hrResult = hr;
            it->spPlugin.Release();
        }
    }
    m_active.clear();
    m_disabled.clear();
    return hrResult;
}

std::vector<PluginInfo> CPluginManager::GetActivePlugins() const
{
    std::vector<PluginInfo> result;
    result.reserve(m_active.size());
    for (const auto& loaded : m_active)
        result.push_back(loaded.info);
    return result;
}

HRESULT CPluginManager::ExecuteCommand(size_t pluginIndex, ULONG commandID)
{
    if (pluginIndex >= m_active.size() || !m_active[pluginIndex].spPlugin)
        return E_INVALIDARG;
    return m_active[pluginIndex].spPlugin->ExecuteCommand(commandID);
}

CString GuidToString(REFGUID guid)
{
    wchar_t buf[40] = {};
    ::StringFromGUID2(guid, buf, _countof(buf));
    return buf;
}

CString HResultToString(HRESULT hr)
{
    CString str;
    str.Format(L"0x%08X", static_cast<unsigned>(hr));

    LPWSTR pszMsg = nullptr;
    DWORD len = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                                 nullptr, static_cast<DWORD>(hr), 0,
                                 reinterpret_cast<LPWSTR>(&pszMsg), 0, nullptr);
    if (len > 0 && pszMsg)
    {
        CString msg(pszMsg);
        msg.TrimRight();
        str += L" (" + msg + L")";
    }
    ::LocalFree(pszMsg);
    return str;
}
