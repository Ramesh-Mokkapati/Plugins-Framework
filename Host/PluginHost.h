#pragma once

#include "..\Shared\IPlugin.h"

class CMainFrame;

// IPluginHost implementation handed to plugins via IPlugin::Initialize.
// Reference counted by hand (no ATL module in an MFC exe). The frame calls
// Detach() when it is destroyed so a plugin that leaks a reference cannot
// reach a dead window.
class CPluginHostServices : public IPluginHost
{
public:
    explicit CPluginHostServices(CMainFrame* pFrame) : m_pFrame(pFrame) {}

    void Detach() { m_pFrame = nullptr; }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IPluginHost
    STDMETHODIMP GetMainWindow(HWND* phWnd) override;
    STDMETHODIMP LogMessage(LPCWSTR pszMessage) override;

protected:
    virtual ~CPluginHostServices() = default;   // destroyed only via Release()

private:
    LONG       m_cRef = 1;
    CMainFrame* m_pFrame;
};
