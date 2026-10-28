#include "stdafx.h"
#include "RegistryWatcher.h"

namespace
{
    struct WatchedKey
    {
        HKEY    root;
        LPCWSTR path;
        HKEY    hKey = nullptr;
        HANDLE  hEvent = nullptr;
    };

    constexpr DWORD kNotifyFilter = REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET;

    bool Arm(const WatchedKey& key)
    {
        return ::RegNotifyChangeKeyValue(key.hKey, TRUE, kNotifyFilter, key.hEvent, TRUE) ==
               ERROR_SUCCESS;
    }
}

bool CRegistryWatcher::Start(HWND hwndNotify, UINT nMessage)
{
    if (IsRunning())
        return true;

    m_hStopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_hStopEvent)
        return false;

    m_thread = std::thread(&CRegistryWatcher::Run, this, hwndNotify, nMessage);
    return true;
}

void CRegistryWatcher::Stop()
{
    if (m_thread.joinable())
    {
        ::SetEvent(m_hStopEvent);
        m_thread.join();
    }
    if (m_hStopEvent)
    {
        ::CloseHandle(m_hStopEvent);
        m_hStopEvent = nullptr;
    }
}

// Asynchronous registry notifications are tied to the registering thread, so
// the keys are opened, armed and re-armed here, on the watcher thread.
void CRegistryWatcher::Run(HWND hwndNotify, UINT nMessage)
{
    WatchedKey keys[] =
    {
        { HKEY_CURRENT_USER,  L"Software\\Classes" },
        { HKEY_LOCAL_MACHINE, L"Software\\Classes\\CLSID" },
    };

    HANDLE waitHandles[1 + _countof(keys)] = { m_hStopEvent };
    WatchedKey* waitKeys[1 + _countof(keys)] = { nullptr };
    DWORD waitCount = 1;

    for (WatchedKey& key : keys)
    {
        if (::RegOpenKeyExW(key.root, key.path, 0, KEY_NOTIFY, &key.hKey) != ERROR_SUCCESS)
            continue;
        key.hEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!key.hEvent || !Arm(key))
            continue;
        waitHandles[waitCount] = key.hEvent;
        waitKeys[waitCount] = &key;
        ++waitCount;
    }

    while (waitCount > 1)
    {
        DWORD rc = ::WaitForMultipleObjects(waitCount, waitHandles, FALSE, INFINITE);
        if (rc < WAIT_OBJECT_0 + 1 || rc >= WAIT_OBJECT_0 + waitCount)
            break;  // stop requested, or the wait failed

        WatchedKey* key = waitKeys[rc - WAIT_OBJECT_0];
        if (!Arm(*key))
            break;
        ::PostMessageW(hwndNotify, nMessage, 0, 0);
    }

    for (WatchedKey& key : keys)
    {
        if (key.hKey)
            ::RegCloseKey(key.hKey);
        if (key.hEvent)
            ::CloseHandle(key.hEvent);
    }
}
