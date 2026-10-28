# Writing PluginsFramework plugins

A plugin is an in-process COM server (a DLL) that implements `IPlugin` and
registers itself in the `CATID_PluginsFramework` component category. The host
never links against a plugin: everything goes through the registry and the
interfaces in [`Shared/IPlugin.h`](../Shared/IPlugin.h), which is the only file
you need from this repository.

## Quick start

1. Scaffold a project from the template. The script copies
   [`PluginTemplate/`](../PluginTemplate), renames everything, and generates a
   fresh CLSID:

   ```bash
   powershell -ExecutionPolicy Bypass -File tools/New-Plugin.ps1 -Name ReportsPlugin -DisplayName "Reports" -AddToSolution
   ```

2. Implement the `TODO(plugin)` sections in `ReportsPlugin/ReportsPlugin.cpp`.
3. Build it:

   ```bash
   msbuild ReportsPlugin/ReportsPlugin.vcxproj /p:Configuration=Debug /p:Platform=x64
   ```

4. Register it for the current user. This doesn't need an elevated prompt:

   ```bash
   regsvr32 /n /i:user bin\x64\Debug\ReportsPlugin.dll
   ```

If PluginsFramework is already running with automatic detection on, **Reports**
shows up in the **Plugins** menu within about a second. Otherwise, start the host
or press F5 (**Refresh Plugins**).

## Two starting points

| Project | Style | Use it when |
|---|---|---|
| [`PluginTemplate`](../PluginTemplate/PluginTemplate.cpp) | Plain C++ and Win32, no ATL | You want every COM call visible, minimal dependencies, or a base for your own framework. It offers one menu command. |
| [`SamplePlugin`](../SamplePlugin/SamplePlugin.cpp) | ATL (`CComObjectRootEx`, `.rgs` registry script) | You already use ATL, or want several commands through `IPluginCommands`. |

Both follow the same contract. They only differ in how much code is boilerplate.

## The contract

### What the host does

```
startup / Refresh / registry change detected
  │
  ├─ ICatInformation::EnumClassesOfCategories(CATID_PluginsFramework)
  │
  └─ for each new CLSID:
       CoCreateInstance(clsid, CLSCTX_INPROC_SERVER, IID_IPlugin)   ── fails → listed as Disabled
       IPlugin::GetPluginInfo(&name, &description, &version)       ── fails → Disabled
       IPlugin::Initialize(pHost)                                   ── fails → Disabled
       QueryInterface(IID_IPluginCommands)                          ── optional
         └─ GetCommandCount / GetCommandInfo(i)  → menu items
       … user clicks a menu item → IPlugin::ExecuteCommand(commandID)
  │
  └─ CLSID no longer in the category, Refresh, or host exit:
       IPlugin::Shutdown()
       Release()
       CoFreeUnusedLibrariesEx()   → DllCanUnloadNow → DLL unloaded
```

All calls come from the host's UI thread, which is a single-threaded apartment.

### Required DLL exports

List these in a `.def` file as `PRIVATE`:

| Export | Purpose |
|---|---|
| `DllGetClassObject` | Returns the class factory for your CLSID |
| `DllCanUnloadNow` | `S_OK` when no objects or server locks remain |
| `DllRegisterServer` | Writes the registry keys below and joins the category |
| `DllUnregisterServer` | Leaves the category and removes the keys |
| `DllInstall` | Optional. Handles `regsvr32 /n /i:user` per-user registration |

### Required registration

```
HKCR\CLSID\{your-clsid}                     (default) = Your Plugin Name
HKCR\CLSID\{your-clsid}\InprocServer32      (default) = C:\full\path\YourPlugin.dll
                                            ThreadingModel = Apartment
HKCR\CLSID\{your-clsid}\Implemented Categories\{BE2C480A-079B-4F9C-854A-CCB54C20EAE7}
```

- Create the last key with `ICatRegister::RegisterClassImplCategories`. Also call
  `ICatRegister::RegisterCategories` so the category has a description. It's
  idempotent and safe to call on every registration.
- Join the category **last** when registering and leave it **first** when
  unregistering. A running host acts on the category key, so this order means it
  never sees a half-registered plugin.
- On unregistration, never call `UnRegisterCategories`: other plugins still use
  the category.
- `ThreadingModel` must be `Apartment` (or `Both`). That way the host's STA
  creates the plugin directly, with no marshaling. The interfaces have no
  proxy/stub.

An installer (MSI, WiX, Inno Setup) can write these keys itself instead of
calling `DllRegisterServer`. The result is the same.

### `IPlugin`

| Method | Host expectations |
|---|---|
| `GetPluginInfo(BSTR* name, BSTR* description, BSTR* version)` | Allocate all three with `SysAllocString`; the host frees them. An empty name makes the host show the CLSID instead. |
| `Initialize(IUnknown* pHost)` | `QueryInterface` the argument for `IPluginHost` and keep it. Return a failure HRESULT to refuse loading; the host then lists the plugin as Disabled and retries on the next sync. |
| `ExecuteCommand(ULONG commandID)` | Runs a menu command. Without `IPluginCommands`, the ID is always `PLUGIN_DEFAULT_COMMAND` (0). Return `E_INVALIDARG` for unknown IDs. Failures are written to the host log. |
| `Shutdown()` | Release `IPluginHost` and anything else acquired in `Initialize`. After this, the host only calls `Release`. |

### `IPluginCommands` (optional)

Implement this to contribute more than one menu item. The plugin then gets its own
submenu under **Plugins**.

| Method | Notes |
|---|---|
| `GetCommandCount(ULONG* count)` | Number of commands. Queried once, right after `Initialize`. |
| `GetCommandInfo(ULONG index, ULONG* commandID, BSTR* caption)` | `commandID` is your own ID and is passed back to `ExecuteCommand`. `&` in captions is shown literally. |

The host has 101 menu command slots shared across all plugins.

### `IPluginHost` (provided by the host)

| Method | Use |
|---|---|
| `GetMainWindow(HWND*)` | Owner window for dialogs and message boxes, so they stay modal to the app. |
| `LogMessage(LPCWSTR)` | Appends a timestamped line to the host's log pane. |

## Rules and pitfalls

- **One CLSID per plugin, forever.** Generate a new one for every plugin
  (`New-Plugin.ps1` does this). Copying a CLSID from another plugin makes the two
  overwrite each other's registration.
- **Match the host's bitness.** An x64 host only sees and loads x64 plugins. A
  32-bit plugin registered on 64-bit Windows lands in the WOW6432Node view, where
  only a Win32 host sees it.
- **Never let a C++ exception or SEH fault cross a COM method.** Catch inside each
  method and return an HRESULT.
- **Don't block the UI thread.** `ExecuteCommand` runs on it. Move long work to a
  worker thread and report back through `LogMessage`, called on the UI thread.
- **Modal UI is fine.** While a command is running (for example, showing a message
  box), the host postpones automatic syncs, so your plugin isn't released under you.
- **Updating a plugin in place.** If you re-register the same CLSID with a new DLL
  path while the host is running, the host keeps the instance it already loaded.
  Use **Refresh Plugins** or restart the host to pick up the new build.
- **Uninstalling.** Unregister before you delete the DLL. A host with automatic
  detection then calls `Shutdown` and unloads the DLL, which frees the file.

## Automatic detection

The host watches `HKCU\Software\Classes` and `HKLM\Software\Classes\CLSID` with
`RegNotifyChangeKeyValue`. Changes are debounced (750 ms), then the host
re-enumerates the category and syncs:

| Registry change | Host reaction |
|---|---|
| New CLSID joins the category | Plugin is loaded, initialized and added to the menu |
| CLSID leaves the category | `Shutdown`, `Release`, menu item removed, DLL unloaded |
| Disabled plugin is fixed (for example, its DLL reappears) | Retried on the next change notification, or press F5 |

Plugins that are still registered keep running; only the differences are applied.
Turn detection off under **Plugins → Detect New Plugins Automatically**. The
setting is stored in `HKCU\Software\PluginsFramework`.

## Troubleshooting

The plugin list shows the HRESULT for every disabled plugin.

| HRESULT | Meaning | Usual cause |
|---|---|---|
| `0x80040154` `REGDB_E_CLASSNOTREG` | Class not registered | `InprocServer32` key missing, or the plugin's bitness differs from the host's |
| `0x8007007E` | Module not found | The DLL path in `InprocServer32` doesn't exist, or a dependency DLL is missing |
| `0x800401F9` `CO_E_ERRORINDLL` | Error in the DLL | Exports missing from the `.def` file, or the DLL was built for the wrong platform |
| `0x80004002` `E_NOINTERFACE` | No such interface | The object's `QueryInterface` doesn't answer `IID_IPlugin` |
| `0x80040111` `CLASS_E_CLASSNOTAVAILABLE` | DLL doesn't serve this CLSID | The CLSID in the registry differs from the one `DllGetClassObject` checks |
