#include "stdafx.h"
#include "PluginsFramework.h"
#include "MainFrm.h"

BEGIN_MESSAGE_MAP(CPluginsFrameworkApp, CWinApp)
    ON_COMMAND(ID_APP_ABOUT, &CPluginsFrameworkApp::OnAppAbout)
END_MESSAGE_MAP()

CPluginsFrameworkApp theApp;

BOOL CPluginsFrameworkApp::InitInstance()
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    CWinApp::InitInstance();

    // Settings (e.g. auto-detect) live under HKCU\Software\PluginsFramework.
    SetRegistryKey(L"PluginsFramework");

    // Single-threaded apartment; plugins register ThreadingModel=Apartment and
    // are therefore created directly on this thread without marshaling.
    if (!AfxOleInit())
    {
        AfxMessageBox(L"COM/OLE initialization failed.", MB_ICONERROR);
        return FALSE;
    }

    auto* pFrame = new CMainFrame;
    m_pMainWnd = pFrame;
    if (!pFrame->LoadFrame(IDR_MAINFRAME))
        return FALSE;   // the frame deletes itself in PostNcDestroy

    pFrame->ShowWindow(m_nCmdShow);
    pFrame->UpdateWindow();
    return TRUE;
}

void CPluginsFrameworkApp::OnAppAbout()
{
    AfxMessageBox(L"PluginsFramework\n\nCOM component-category based plugin host.",
                  MB_ICONINFORMATION);
}
