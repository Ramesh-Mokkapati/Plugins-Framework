#include "stdafx.h"
#include "PluginHost.h"
#include "MainFrm.h"

STDMETHODIMP CPluginHostServices::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv)
        return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IPluginHost)
    {
        *ppv = static_cast<IPluginHost*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CPluginHostServices::AddRef()
{
    return ::InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CPluginHostServices::Release()
{
    ULONG cRef = ::InterlockedDecrement(&m_cRef);
    if (cRef == 0)
        delete this;
    return cRef;
}

STDMETHODIMP CPluginHostServices::GetMainWindow(HWND* phWnd)
{
    if (!phWnd)
        return E_POINTER;
    *phWnd = m_pFrame ? m_pFrame->GetSafeHwnd() : nullptr;
    return m_pFrame ? S_OK : E_UNEXPECTED;
}

STDMETHODIMP CPluginHostServices::LogMessage(LPCWSTR pszMessage)
{
    if (!pszMessage)
        return E_POINTER;
    if (!m_pFrame)
        return E_UNEXPECTED;
    m_pFrame->AppendLog(pszMessage);
    return S_OK;
}
