<#
.SYNOPSIS
    Creates a new PluginsFramework plugin project from PluginTemplate.

.DESCRIPTION
    Copies PluginTemplate\ to <Name>\, renames the files, project and C++ classes,
    sets the display name and description, and generates a fresh CLSID and
    project GUID. Optionally adds the project to PluginsFramework.sln.

.EXAMPLE
    .\tools\New-Plugin.ps1 -Name ReportsPlugin -DisplayName "Reports" -AddToSolution

.EXAMPLE
    .\tools\New-Plugin.ps1 -Name ExportPlugin -DisplayName "Export to CSV" -Description "Exports the current view"
#>
[CmdletBinding()]
param(
    # Folder, project and DLL name; also used for the C++ class (C<Name>).
    [Parameter(Mandatory)]
    [ValidatePattern('^[A-Za-z][A-Za-z0-9_]*$')]
    [string]$Name,

    # Name shown in the host's Plugins menu and plugin list.
    [ValidatePattern('^[^"\\]+$')]
    [string]$DisplayName = $Name,

    [ValidatePattern('^[^"\\]+$')]
    [string]$Description = "$DisplayName plugin for PluginsFramework",

    [switch]$AddToSolution
)

$ErrorActionPreference = 'Stop'

$root         = Split-Path -Parent $PSScriptRoot
$templateDir  = Join-Path $root 'PluginTemplate'
$targetDir    = Join-Path $root $Name
$solutionPath = Join-Path $root 'PluginsFramework.sln'

# Values baked into PluginTemplate that every new plugin must replace.
$templateClsid       = '27C5686B-13CD-4FB7-BAF8-0837F074A81E'
$templateProjectGuid = '246FC5B7-72A8-41C9-BE3E-3802A3391607'

if (-not (Test-Path $templateDir)) { throw "Template not found: $templateDir" }
if (Test-Path $targetDir)          { throw "'$targetDir' already exists. Choose another -Name." }

$clsid       = [guid]::NewGuid().ToString().ToUpper()
$projectGuid = [guid]::NewGuid().ToString().ToUpper()
$utf8NoBom   = New-Object System.Text.UTF8Encoding $false

New-Item -ItemType Directory -Path $targetDir | Out-Null
foreach ($file in Get-ChildItem -Path $templateDir -File) {
    $text = [System.IO.File]::ReadAllText($file.FullName)
    $text = $text.Replace($templateClsid, $clsid).
                  Replace($templateProjectGuid, $projectGuid).
                  Replace('CTemplatePluginFactory', "C${Name}Factory").
                  Replace('CTemplatePlugin', "C$Name").
                  Replace('L"Template Plugin"', "L`"$DisplayName`"").
                  Replace('L"Starting point for new plugins"', "L`"$Description`"").
                  Replace('PluginTemplate', $Name)
    $newPath = Join-Path $targetDir ($file.Name.Replace('PluginTemplate', $Name))
    [System.IO.File]::WriteAllText($newPath, $text, $utf8NoBom)
}

if ($AddToSolution) {
    if (-not (Test-Path $solutionPath)) {
        Write-Warning "No solution at $solutionPath; build $Name\$Name.vcxproj directly."
    }
    else {
        $sln = [System.IO.File]::ReadAllText($solutionPath)
        $nl  = if ($sln.Contains("`r`n")) { "`r`n" } else { "`n" }
        $g   = "{$projectGuid}"

        $projectEntry = "Project(`"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}`") = `"$Name`", `"$Name\$Name.vcxproj`", `"$g`"${nl}EndProject${nl}"
        $configLines  = foreach ($cfg in 'Debug', 'Release') {
            foreach ($plat in @(@('x64', 'x64'), @('x86', 'Win32'))) {
                foreach ($kind in 'ActiveCfg', 'Build.0') {
                    "`t`t$g.$cfg|$($plat[0]).$kind = $cfg|$($plat[1])$nl"
                }
            }
        }

        $globalIndex = $sln.IndexOf("Global$nl")
        $configEnd   = $sln.IndexOf("`tEndGlobalSection$nl`tGlobalSection(SolutionProperties)")
        if ($globalIndex -lt 0 -or $configEnd -lt 0) {
            Write-Warning "Unrecognized solution layout; add $Name\$Name.vcxproj in Visual Studio."
        }
        else {
            $sln = $sln.Insert($configEnd, ($configLines -join ''))
            $sln = $sln.Insert($globalIndex, $projectEntry)
            [System.IO.File]::WriteAllText($solutionPath, $sln, $utf8NoBom)
            Write-Host "Added $Name to PluginsFramework.sln"
        }
    }
}

Write-Host ""
Write-Host "Created $targetDir"
Write-Host "  CLSID        {$clsid}"
Write-Host "  Display name $DisplayName"
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Implement the TODO(plugin) sections in $Name\$Name.cpp"
Write-Host "  2. Build:     msbuild $Name\$Name.vcxproj /p:Configuration=Debug /p:Platform=x64"
Write-Host "  3. Register:  regsvr32 /n /i:user bin\x64\Debug\$Name.dll"
Write-Host "  A running PluginsFramework host with auto-detect on loads it immediately."
