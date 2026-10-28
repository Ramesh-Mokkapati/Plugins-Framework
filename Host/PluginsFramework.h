#pragma once

#include "Resource.h"

class CPluginsFrameworkApp : public CWinApp
{
public:
    BOOL InitInstance() override;

    afx_msg void OnAppAbout();
    DECLARE_MESSAGE_MAP()
};

extern CPluginsFrameworkApp theApp;
