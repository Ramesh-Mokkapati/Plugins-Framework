// IPlugin.h - COM contract shared by the PluginsFramework host and its plugins.
//
// Plugins are in-process COM servers (ThreadingModel=Apartment) that implement
// IPlugin and register themselves under the CATID_PluginsFramework component
// category. The host discovers them through ICatInformation at startup / refresh.
//
// The interfaces are declared directly in C++ (no MIDL step) because host and
// plugins always share the host's STA, so no proxy/stub marshaling is needed.

#pragma once

#include <unknwn.h>
#include <oleauto.h>

// ---------------------------------------------------------------------------
// GUIDs
// ---------------------------------------------------------------------------

// {BE2C480A-079B-4F9C-854A-CCB54C20EAE7}
inline constexpr CATID CATID_PluginsFramework =
    { 0xBE2C480A, 0x079B, 0x4F9C, { 0x85, 0x4A, 0xCC, 0xB5, 0x4C, 0x20, 0xEA, 0xE7 } };

// {07ED12C6-C9A0-458A-B1DC-AC0592EE3918}
inline constexpr IID IID_IPlugin =
    { 0x07ED12C6, 0xC9A0, 0x458A, { 0xB1, 0xDC, 0xAC, 0x05, 0x92, 0xEE, 0x39, 0x18 } };

// {F5CE21F8-32F3-49FC-8FEF-E1A5DCB7CAD1}
inline constexpr IID IID_IPluginCommands =
    { 0xF5CE21F8, 0x32F3, 0x49FC, { 0x8F, 0xEF, 0xE1, 0xA5, 0xDC, 0xB7, 0xCA, 0xD1 } };

// {123FD03D-B886-4B4A-98DF-67DEAC8E3B42}
inline constexpr IID IID_IPluginHost =
    { 0x123FD03D, 0xB886, 0x4B4A, { 0x98, 0xDF, 0x67, 0xDE, 0xAC, 0x8E, 0x3B, 0x42 } };

inline constexpr wchar_t PLUGINS_FRAMEWORK_CATEGORY_DESC[] = L"PluginsFramework Plugins";

// Command ID passed to IPlugin::ExecuteCommand for plugins that do not
// implement IPluginCommands (they get a single menu item).
inline constexpr ULONG PLUGIN_DEFAULT_COMMAND = 0;

// ---------------------------------------------------------------------------
// IPlugin - mandatory interface every plugin implements.
// ---------------------------------------------------------------------------
MIDL_INTERFACE("07ED12C6-C9A0-458A-B1DC-AC0592EE3918")
IPlugin : public IUnknown
{
public:
    // Caller frees the returned strings with SysFreeString.
    virtual HRESULT STDMETHODCALLTYPE GetPluginInfo(
        /* [out] */ BSTR* pName,
        /* [out] */ BSTR* pDescription,
        /* [out] */ BSTR* pVersion) = 0;

    // pHostApp exposes IPluginHost; plugins may QueryInterface for it and keep
    // a reference until Shutdown.
    virtual HRESULT STDMETHODCALLTYPE Initialize(/* [in] */ IUnknown* pHostApp) = 0;

    // commandID is a plugin-defined ID (see IPluginCommands), or
    // PLUGIN_DEFAULT_COMMAND for plugins exposing a single command.
    virtual HRESULT STDMETHODCALLTYPE ExecuteCommand(/* [in] */ ULONG commandID) = 0;

    // Called before the host releases the plugin. Release host references here.
    virtual HRESULT STDMETHODCALLTYPE Shutdown() = 0;
};

// ---------------------------------------------------------------------------
// IPluginCommands - optional; lets a plugin contribute several menu commands.
// ---------------------------------------------------------------------------
MIDL_INTERFACE("F5CE21F8-32F3-49FC-8FEF-E1A5DCB7CAD1")
IPluginCommands : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetCommandCount(/* [out] */ ULONG* pCount) = 0;

    // Returns the plugin-defined command ID and its menu caption for index
    // [0, count). Caller frees *pCaption with SysFreeString.
    virtual HRESULT STDMETHODCALLTYPE GetCommandInfo(
        /* [in]  */ ULONG index,
        /* [out] */ ULONG* pCommandID,
        /* [out] */ BSTR* pCaption) = 0;
};

// ---------------------------------------------------------------------------
// IPluginHost - implemented by the host, passed to IPlugin::Initialize.
// ---------------------------------------------------------------------------
MIDL_INTERFACE("123FD03D-B886-4B4A-98DF-67DEAC8E3B42")
IPluginHost : public IUnknown
{
public:
    // Main frame window, for use as an owner of plugin dialogs / message boxes.
    virtual HRESULT STDMETHODCALLTYPE GetMainWindow(/* [out] */ HWND* phWnd) = 0;

    // Appends a line to the host's log pane.
    virtual HRESULT STDMETHODCALLTYPE LogMessage(/* [in] */ LPCWSTR pszMessage) = 0;
};
