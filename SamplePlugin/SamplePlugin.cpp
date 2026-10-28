// SamplePlugin.cpp - self-registering in-process ATL COM plugin for PluginsFramework.
//
//   regsvr32 SamplePlugin.dll          register (machine-wide, needs elevation)
//   regsvr32 /u SamplePlugin.dll       unregister
//   regsvr32 /n /i:user SamplePlugin.dll      register for the current user only
//   regsvr32 /u /n /i:user SamplePlugin.dll   unregister the per-user registration

#include "stdafx.h"
#include "resource.h"
#include "..\Shared\IPlugin.h"

// {C7B0252E-D9AD-4611-87C2-5D3B81AB8C3E}
inline constexpr CLSID CLSID_SamplePlugin =
    { 0xC7B0252E, 0xD9AD, 0x4611, { 0x87, 0xC2, 0x5D, 0x3B, 0x81, 0xAB, 0x8C, 0x3E } };

// ---------------------------------------------------------------------------
// Plugin object
// ---------------------------------------------------------------------------

class ATL_NO_VTABLE CSamplePlugin :
    public CComObjectRootEx<CComSingleThreadModel>,
    public CComCoClass<CSamplePlugin, &CLSID_SamplePlugin>,
    public IPlugin,
    public IPluginCommands
{
public:
    DECLARE_REGISTRY_RESOURCEID(IDR_SAMPLEPLUGIN)
    DECLARE_NOT_AGGREGATABLE(CSamplePlugin)

    BEGIN_COM_MAP(CSamplePlugin)
        COM_INTERFACE_ENTRY_IID(IID_IPlugin, IPlugin)
        COM_INTERFACE_ENTRY_IID(IID_IPluginCommands, IPluginCommands)
    END_COM_MAP()

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    // IPlugin
    STDMETHODIMP GetPluginInfo(BSTR* pName, BSTR* pDescription, BSTR* pVersion) override
    {
        if (!pName || !pDescription || !pVersion)
            return E_POINTER;
        *pName = *pDescription = *pVersion = nullptr;

        CComBSTR name(L"Sample Plugin");
        CComBSTR desc(L"Demonstrates host callbacks and multiple commands");
        CComBSTR version(L"1.0.0");
        if (!name || !desc || !version)
            return E_OUTOFMEMORY;

        *pName = name.Detach();
        *pDescription = desc.Detach();
        *pVersion = version.Detach();
        return S_OK;
    }

    STDMETHODIMP Initialize(IUnknown* pHostApp) override
    {
        if (!pHostApp)
            return E_INVALIDARG;
        HRESULT hr = pHostApp->QueryInterface(IID_IPluginHost,
                                              reinterpret_cast<void**>(&m_spHost));
        if (FAILED(hr))
            return hr;
        m_spHost->LogMessage(L"Sample Plugin initialized.");
        return S_OK;
    }

    STDMETHODIMP ExecuteCommand(ULONG commandID) override
    {
        if (!m_spHost)
            return E_UNEXPECTED;

        switch (commandID)
        {
        case CmdSayHello:
        {
            HWND hwndOwner = nullptr;
            m_spHost->GetMainWindow(&hwndOwner);
            ::MessageBoxW(hwndOwner, L"Hello from the Sample Plugin!", L"Sample Plugin",
                          MB_OK | MB_ICONINFORMATION);
            return S_OK;
        }
        case CmdLogTime:
        {
            SYSTEMTIME st;
            ::GetLocalTime(&st);
            wchar_t msg[128];
            swprintf_s(msg, L"Sample Plugin: the date is %04u-%02u-%02u.",
                       st.wYear, st.wMonth, st.wDay);
            return m_spHost->LogMessage(msg);
        }
        case CmdShowModule:
        {
            wchar_t path[MAX_PATH] = {};
            ::GetModuleFileNameW(_AtlBaseModule.GetModuleInstance(), path, _countof(path));
            CStringW msg(L"Sample Plugin loaded from: ");
            return m_spHost->LogMessage(msg + path);
        }
        default:
            return E_INVALIDARG;
        }
    }

    STDMETHODIMP Shutdown() override
    {
        if (m_spHost)
        {
            m_spHost->LogMessage(L"Sample Plugin shutting down.");
            m_spHost.Release();
        }
        return S_OK;
    }

    // IPluginCommands
    STDMETHODIMP GetCommandCount(ULONG* pCount) override
    {
        if (!pCount)
            return E_POINTER;
        *pCount = _countof(kCommands);
        return S_OK;
    }

    STDMETHODIMP GetCommandInfo(ULONG index, ULONG* pCommandID, BSTR* pCaption) override
    {
        if (!pCommandID || !pCaption)
            return E_POINTER;
        *pCaption = nullptr;
        if (index >= _countof(kCommands))
            return E_INVALIDARG;

        *pCommandID = kCommands[index].id;
        *pCaption = ::SysAllocString(kCommands[index].caption);
        return *pCaption ? S_OK : E_OUTOFMEMORY;
    }

private:
    enum : ULONG { CmdSayHello = 1, CmdLogTime = 2, CmdShowModule = 3 };

    struct CommandDef
    {
        ULONG   id;
        LPCWSTR caption;
    };
    static constexpr CommandDef kCommands[] =
    {
        { CmdSayHello,   L"Say Hello" },
        { CmdLogTime,    L"Log Today's Date" },
        { CmdShowModule, L"Log Plugin Location" },
    };

    CComPtr<IPluginHost> m_spHost;
};

OBJECT_ENTRY_AUTO(CLSID_SamplePlugin, CSamplePlugin)

// ---------------------------------------------------------------------------
// Module and component-category registration
// ---------------------------------------------------------------------------

class CSamplePluginModule : public ATL::CAtlDllModuleT<CSamplePluginModule>
{
};

CSamplePluginModule _AtlModule;

namespace
{
    HRESULT RegisterPluginCategory()
    {
        CComPtr<ICatRegister> spCatRegister;
        HRESULT hr = spCatRegister.CoCreateInstance(CLSID_StdComponentCategoriesMgr, nullptr,
                                                    CLSCTX_INPROC_SERVER);
        if (FAILED(hr))
            return hr;

        // Ensure the category itself exists (idempotent), then join it.
        CATEGORYINFO catInfo = {};
        catInfo.catid = CATID_PluginsFramework;
        catInfo.lcid = 0x0409;
        wcscpy_s(catInfo.szDescription, PLUGINS_FRAMEWORK_CATEGORY_DESC);
        hr = spCatRegister->RegisterCategories(1, &catInfo);
        if (FAILED(hr))
            return hr;

        CATID catid = CATID_PluginsFramework;
        return spCatRegister->RegisterClassImplCategories(CLSID_SamplePlugin, 1, &catid);
    }

    HRESULT UnregisterPluginCategory()
    {
        CComPtr<ICatRegister> spCatRegister;
        HRESULT hr = spCatRegister.CoCreateInstance(CLSID_StdComponentCategoriesMgr, nullptr,
                                                    CLSCTX_INPROC_SERVER);
        if (FAILED(hr))
            return hr;

        // Leave the category definition in place: other plugins may belong to it.
        CATID catid = CATID_PluginsFramework;
        hr = spCatRegister->UnRegisterClassImplCategories(CLSID_SamplePlugin, 1, &catid);
        if (FAILED(hr))
            return hr;

        // ICatRegister leaves an empty "Implemented Categories" key behind, which
        // would stop the ATL registrar from removing the CLSID key. RegDeleteKey
        // only succeeds on keys without subkeys, so other categories survive.
        wchar_t clsid[40] = {};
        ::StringFromGUID2(CLSID_SamplePlugin, clsid, _countof(clsid));
        CStringW keyPath;
        keyPath.Format(L"CLSID\\%s\\Implemented Categories", clsid);
        ::RegDeleteKeyW(HKEY_CLASSES_ROOT, keyPath);
        return S_OK;
    }

    // RAII guard that redirects HKEY_CLASSES_ROOT to HKCU\Software\Classes so
    // both the ATL registrar and ICatRegister write per-user keys.
    class CPerUserClassesRedirect
    {
    public:
        HRESULT Enable()
        {
            LONG err = ::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes", 0,
                                       KEY_ALL_ACCESS, &m_hKey);
            if (err != ERROR_SUCCESS)
                return HRESULT_FROM_WIN32(err);
            err = ::RegOverridePredefKey(HKEY_CLASSES_ROOT, m_hKey);
            return HRESULT_FROM_WIN32(err);
        }

        ~CPerUserClassesRedirect()
        {
            if (m_hKey)
            {
                ::RegOverridePredefKey(HKEY_CLASSES_ROOT, nullptr);
                ::RegCloseKey(m_hKey);
            }
        }

    private:
        HKEY m_hKey = nullptr;
    };
}

// ---------------------------------------------------------------------------
// DLL exports
// ---------------------------------------------------------------------------

extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(hInstance);
    return _AtlModule.DllMain(dwReason, lpReserved);
}

_Use_decl_annotations_
STDAPI DllCanUnloadNow()
{
    return _AtlModule.DllCanUnloadNow();
}

_Use_decl_annotations_
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    return _AtlModule.DllGetClassObject(rclsid, riid, ppv);
}

_Use_decl_annotations_
STDAPI DllRegisterServer()
{
    HRESULT hr = _AtlModule.DllRegisterServer(FALSE);   // no type library
    if (SUCCEEDED(hr))
        hr = RegisterPluginCategory();
    if (FAILED(hr))
        DllUnregisterServer();
    return hr;
}

_Use_decl_annotations_
STDAPI DllUnregisterServer()
{
    // Leave the category first so the host stops seeing the plugin even if
    // removing the CLSID key fails.
    HRESULT hrCat = UnregisterPluginCategory();
    HRESULT hr = _AtlModule.DllUnregisterServer(FALSE);
    return FAILED(hr) ? hr : hrCat;
}

// regsvr32 /n /i:user registers for the current user only (no elevation).
_Use_decl_annotations_
STDAPI DllInstall(BOOL bInstall, LPCWSTR pszCmdLine)
{
    CPerUserClassesRedirect redirect;
    if (pszCmdLine && _wcsicmp(pszCmdLine, L"user") == 0)
    {
        ATL::AtlSetPerUserRegistration(true);
        HRESULT hr = redirect.Enable();
        if (FAILED(hr))
            return hr;
    }

    return bInstall ? DllRegisterServer() : DllUnregisterServer();
}
