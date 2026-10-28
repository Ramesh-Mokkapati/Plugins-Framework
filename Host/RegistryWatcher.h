#pragma once

// Watches the registry locations where COM classes are registered and posts a
// message to a window whenever anything below them changes. regsvr32 writes
// several keys per registration, so the receiver should debounce.
//
// Watched (subtree):
//   HKCU\Software\Classes        per-user registrations (regsvr32 /n /i:user)
//   HKLM\Software\Classes\CLSID  machine-wide registrations (32-bit processes
//                                are redirected to WOW6432Node automatically)
class CRegistryWatcher
{
public:
    CRegistryWatcher() = default;
    ~CRegistryWatcher() { Stop(); }

    CRegistryWatcher(const CRegistryWatcher&) = delete;
    CRegistryWatcher& operator=(const CRegistryWatcher&) = delete;

    // Posts nMessage (wParam = lParam = 0) to hwndNotify on every change.
    bool Start(HWND hwndNotify, UINT nMessage);
    void Stop();
    bool IsRunning() const { return m_thread.joinable(); }

private:
    void Run(HWND hwndNotify, UINT nMessage);

    std::thread m_thread;
    HANDLE      m_hStopEvent = nullptr;
};
