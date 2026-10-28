// PluginTemplate.cpp - starting point for a new PluginsFramework plugin.
//
// Plain C++/COM with no ATL dependency, so every call the host relies on is
// visible. Create your own copy with tools\New-Plugin.ps1 (it renames everything
// and generates a fresh CLSID), then fill in the sections marked "TODO(plugin)".
// See docs\PLUGIN_DEVELOPMENT.md for the full contract.
//
//   regsvr32 PluginTemplate.dll                 register machine-wide (elevated)
//   regsvr32 /n /i:user PluginTemplate.dll      register for the current user
//   regsvr32 /u [/n /i:user] PluginTemplate.dll unregister
//
// A running host with automatic detection on picks the plugin up within a
// second of registration, and drops it again on unregistration.

#include <windows.h>
#include <olectl.h>
#include <comcat.h>
#include <strsafe.h>
#include <new>

#include "IPlugin.h"

// ---------------------------------------------------------------------------
// TODO(plugin): identity. Every plugin needs its own CLSID; never reuse one.
// ---------------------------------------------------------------------------

class __declspec(uuid("27C5686B-13CD-4FB7-BAF8-0837F074A81E")) CTemplatePlugin;

namespace
{
    constexpr wchar_t kPluginName[]        = L"Template Plugin";
    constexpr wchar_t kPluginDescription[] = L"Starting point for new plugins";
    constexpr wchar_t kPluginVersion[]     = L"1.0.0";

    HMODULE g_hModule = nullptr;
    LONG    g_cModuleRefs = 0;     // live objects + IClassFactory::LockServer locks
}

// ---------------------------------------------------------------------------
// The plugin object
// ---------------------------------------------------------------------------

class CTemplatePlugin : public IPlugin
{
public:
    CTemplatePlugin() { ::InterlockedIncrement(&g_cModuleRefs); }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        // TODO(plugin): to offer several menu commands, also implement
        // IPluginCommands and return it here (see SamplePlugin).
        if (riid == IID_IUnknown || riid == IID_IPlugin)
        {
            *ppv = static_cast<IPlugin*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return ::InterlockedIncrement(&m_cRef);
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG cRef = ::InterlockedDecrement(&m_cRef);
        if (cRef == 0)
            delete this;
        return cRef;
    }

    // IPlugin. The host calls, in order: GetPluginInfo, Initialize,
    // ExecuteCommand (any number of times), Shutdown, then releases the object.

    STDMETHODIMP GetPluginInfo(BSTR* pName, BSTR* pDescription, BSTR* pVersion) override
    {
        if (!pName || !pDescription || !pVersion)
            return E_POINTER;

        *pName = ::SysAllocString(kPluginName);
        *pDescription = ::SysAllocString(kPluginDescription);
        *pVersion = ::SysAllocString(kPluginVersion);
        if (!*pName || !*pDescription || !*pVersion)
        {
            ::SysFreeString(*pName);
            ::SysFreeString(*pDescription);
            ::SysFreeString(*pVersion);
            *pName = *pDescription = *pVersion = nullptr;
            return E_OUTOFMEMORY;
        }
        return S_OK;   // the host frees the strings
    }

    STDMETHODIMP Initialize(IUnknown* pHostApp) override
    {
        if (!pHostApp)
            return E_INVALIDARG;
        HRESULT hr = pHostApp->QueryInterface(IID_IPluginHost,
                                              reinterpret_cast<void**>(&m_pHost));
        if (FAILED(hr))
            return hr;   // a failure here makes the host list the plugin as disabled

        // TODO(plugin): one-time setup (load settings, allocate resources).
        return S_OK;
    }

    STDMETHODIMP ExecuteCommand(ULONG commandID) override
    {
        if (!m_pHost)
            return E_UNEXPECTED;
        // Without IPluginCommands the host shows one menu item named after the
        // plugin and always passes PLUGIN_DEFAULT_COMMAND.
        if (commandID != PLUGIN_DEFAULT_COMMAND)
            return E_INVALIDARG;

        // TODO(plugin): the command's work. Use the host window as the owner
        // of any UI so it stays modal to the application.
        HWND hwndOwner = nullptr;
        m_pHost->GetMainWindow(&hwndOwner);

        wchar_t message[256];
        StringCchPrintfW(message, _countof(message), L"Hello from %s.", kPluginName);
        return m_pHost->LogMessage(message);
    }

    STDMETHODIMP Shutdown() override
    {
        // TODO(plugin): release resources acquired in Initialize.
        if (m_pHost)
        {
            m_pHost->Release();
            m_pHost = nullptr;
        }
        return S_OK;
    }

private:
    ~CTemplatePlugin()
    {
        if (m_pHost)
            m_pHost->Release();
        ::InterlockedDecrement(&g_cModuleRefs);
    }

    LONG         m_cRef = 1;
    IPluginHost* m_pHost = nullptr;
};

// ---------------------------------------------------------------------------
// Class factory (COM calls this to create the plugin object)
// ---------------------------------------------------------------------------

class CTemplatePluginFactory : public IClassFactory
{
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory)
        {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    // The factory is a static object; its lifetime is the module's.
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        *ppv = nullptr;
        if (pUnkOuter)
            return CLASS_E_NOAGGREGATION;

        auto* pPlugin = new (std::nothrow) CTemplatePlugin;
        if (!pPlugin)
            return E_OUTOFMEMORY;
        HRESULT hr = pPlugin->QueryInterface(riid, ppv);
        pPlugin->Release();
        return hr;
    }

    STDMETHODIMP LockServer(BOOL fLock) override
    {
        if (fLock)
            ::InterlockedIncrement(&g_cModuleRefs);
        else
            ::InterlockedDecrement(&g_cModuleRefs);
        return S_OK;
    }
};

namespace
{
    CTemplatePluginFactory g_factory;

    // ----------------------------------------------------------------------
    // Registration helpers
    // ----------------------------------------------------------------------

    HRESULT ClsidKeyPath(wchar_t (&path)[64])
    {
        wchar_t clsid[40];
        if (!::StringFromGUID2(__uuidof(CTemplatePlugin), clsid, _countof(clsid)))
            return E_UNEXPECTED;
        return StringCchPrintfW(path, _countof(path), L"CLSID\\%s", clsid);
    }

    HRESULT SetStringValue(HKEY hKey, LPCWSTR name, LPCWSTR value)
    {
        const DWORD cb = static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));
        return HRESULT_FROM_WIN32(::RegSetValueExW(hKey, name, 0, REG_SZ,
                                                   reinterpret_cast<const BYTE*>(value), cb));
    }

    // HKCR\CLSID\{clsid}                  = <plugin name>
    // HKCR\CLSID\{clsid}\InprocServer32   = <full path of this DLL>
    //                     ThreadingModel  = Apartment   (required: host is an STA)
    HRESULT RegisterClassKeys()
    {
        wchar_t modulePath[MAX_PATH];
        DWORD len = ::GetModuleFileNameW(g_hModule, modulePath, _countof(modulePath));
        if (len == 0 || len == _countof(modulePath))
            return HRESULT_FROM_WIN32(::GetLastError());

        wchar_t keyPath[64];
        HRESULT hr = ClsidKeyPath(keyPath);
        if (FAILED(hr))
            return hr;

        HKEY hClsid = nullptr;
        hr = HRESULT_FROM_WIN32(::RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, 0,
                                                  KEY_WRITE, nullptr, &hClsid, nullptr));
        if (FAILED(hr))
            return hr;
        hr = SetStringValue(hClsid, nullptr, kPluginName);

        HKEY hInproc = nullptr;
        if (SUCCEEDED(hr))
            hr = HRESULT_FROM_WIN32(::RegCreateKeyExW(hClsid, L"InprocServer32", 0, nullptr, 0,
                                                      KEY_WRITE, nullptr, &hInproc, nullptr));
        if (SUCCEEDED(hr))
            hr = SetStringValue(hInproc, nullptr, modulePath);
        if (SUCCEEDED(hr))
            hr = SetStringValue(hInproc, L"ThreadingModel", L"Apartment");

        if (hInproc)
            ::RegCloseKey(hInproc);
        ::RegCloseKey(hClsid);
        return hr;
    }

    HRESULT UnregisterClassKeys()
    {
        wchar_t keyPath[64];
        HRESULT hr = ClsidKeyPath(keyPath);
        if (FAILED(hr))
            return hr;
        LSTATUS err = ::RegDeleteTreeW(HKEY_CLASSES_ROOT, keyPath);
        return (err == ERROR_SUCCESS || err == ERROR_FILE_NOT_FOUND) ? S_OK
                                                                    : HRESULT_FROM_WIN32(err);
    }

    // Joining CATID_PluginsFramework is what makes the host discover the plugin.
    HRESULT RegisterCategory()
    {
        ICatRegister* pCatRegister = nullptr;
        HRESULT hr = ::CoCreateInstance(CLSID_StdComponentCategoriesMgr, nullptr,
                                        CLSCTX_INPROC_SERVER, IID_ICatRegister,
                                        reinterpret_cast<void**>(&pCatRegister));
        if (FAILED(hr))
            return hr;

        CATEGORYINFO catInfo = {};
        catInfo.catid = CATID_PluginsFramework;
        catInfo.lcid = 0x0409;
        StringCchCopyW(catInfo.szDescription, _countof(catInfo.szDescription),
                       PLUGINS_FRAMEWORK_CATEGORY_DESC);
        hr = pCatRegister->RegisterCategories(1, &catInfo);   // idempotent

        if (SUCCEEDED(hr))
        {
            CATID catid = CATID_PluginsFramework;
            hr = pCatRegister->RegisterClassImplCategories(__uuidof(CTemplatePlugin), 1, &catid);
        }
        pCatRegister->Release();
        return hr;
    }

    HRESULT UnregisterCategory()
    {
        ICatRegister* pCatRegister = nullptr;
        HRESULT hr = ::CoCreateInstance(CLSID_StdComponentCategoriesMgr, nullptr,
                                        CLSCTX_INPROC_SERVER, IID_ICatRegister,
                                        reinterpret_cast<void**>(&pCatRegister));
        if (FAILED(hr))
            return hr;

        // Leave the category definition alone: other plugins still use it.
        CATID catid = CATID_PluginsFramework;
        hr = pCatRegister->UnRegisterClassImplCategories(__uuidof(CTemplatePlugin), 1, &catid);
        pCatRegister->Release();
        return hr;
    }

    // Redirects HKEY_CLASSES_ROOT to HKCU\Software\Classes for per-user
    // registration, so the same code (including ICatRegister) writes there.
    class CPerUserClassesRedirect
    {
    public:
        HRESULT Enable()
        {
            LSTATUS err = ::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes", 0,
                                          KEY_ALL_ACCESS, &m_hKey);
            if (err == ERROR_SUCCESS)
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
// DLL exports (listed in PluginTemplate.def)
// ---------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        g_hModule = hInstance;
        ::DisableThreadLibraryCalls(hInstance);
    }
    return TRUE;
}

_Use_decl_annotations_
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = nullptr;
    if (rclsid != __uuidof(CTemplatePlugin))
        return CLASS_E_CLASSNOTAVAILABLE;
    return g_factory.QueryInterface(riid, ppv);
}

_Use_decl_annotations_
STDAPI DllCanUnloadNow()
{
    return g_cModuleRefs == 0 ? S_OK : S_FALSE;
}

_Use_decl_annotations_
STDAPI DllRegisterServer()
{
    HRESULT hr = RegisterClassKeys();
    if (SUCCEEDED(hr))
        hr = RegisterCategory();   // last, so the host never sees a half-registered class
    if (FAILED(hr))
        DllUnregisterServer();
    return hr;
}

_Use_decl_annotations_
STDAPI DllUnregisterServer()
{
    // Leave the category first so a running host stops using the plugin
    // before its CLSID key disappears.
    HRESULT hrCat = UnregisterCategory();
    HRESULT hr = UnregisterClassKeys();
    return FAILED(hr) ? hr : hrCat;
}

// regsvr32 /n /i:user -> per-user registration without elevation.
_Use_decl_annotations_
STDAPI DllInstall(BOOL bInstall, LPCWSTR pszCmdLine)
{
    CPerUserClassesRedirect redirect;
    if (pszCmdLine && _wcsicmp(pszCmdLine, L"user") == 0)
    {
        HRESULT hr = redirect.Enable();
        if (FAILED(hr))
            return hr;
    }
    return bInstall ? DllRegisterServer() : DllUnregisterServer();
}
