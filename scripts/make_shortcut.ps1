<#
    make_shortcut.ps1 — put "Void Mago" on the desktop.

        powershell -ExecutionPolicy Bypass -File scripts\make_shortcut.ps1

    Points at scripts\VoidMago.cmd rather than at the binary, so a missing
    build explains itself instead of producing a window that never appears.
    Pass -Remove to take it off again; nothing else on the machine is touched.
#>
[CmdletBinding()]
param(
    [switch]$Remove,
    [string]$Name = "Void Mago"
)

$ErrorActionPreference = "Stop"

$repo     = Split-Path -Parent $PSScriptRoot
$launcher = Join-Path $PSScriptRoot "VoidMago.cmd"
$icon     = Join-Path $PSScriptRoot "voidmago.ico"
$desktop  = [Environment]::GetFolderPath("Desktop")
$link     = Join-Path $desktop "$Name.lnk"

if ($Remove) {
    if (Test-Path $link) {
        Remove-Item $link
        Write-Host "removed $link"
    } else {
        Write-Host "nothing at $link"
    }
    exit 0
}

if (-not (Test-Path $launcher)) { throw "launcher not found: $launcher" }

# The icon is generated, not committed as a binary blob nobody can diff.
if (-not (Test-Path $icon)) {
    Write-Host "generating the icon..."
    & python (Join-Path $PSScriptRoot "make_icon.py")
}

$shell    = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($link)
$shortcut.TargetPath       = $launcher
$shortcut.WorkingDirectory = $repo
$shortcut.Description      = "Void Mago Desktop - the family, its pairs, and what a change would cost"
if (Test-Path $icon) { $shortcut.IconLocation = "$icon,0" }
$shortcut.Save()

Write-Host "created $link"
Write-Host "  -> $launcher"

$exe = Join-Path $repo "build-desktop\bin\voidmago-desktop.exe"
if (-not (Test-Path $exe)) {
    Write-Host ""
    Write-Host "Void Mago Desktop is not built yet. The shortcut will say so when clicked:" -ForegroundColor Yellow
    Write-Host "  cmake -S . -B build-desktop -G Ninja -DCMAKE_BUILD_TYPE=Release -DVOIDMAGO_BUILD_DESKTOP=ON"
    Write-Host "  cmake --build build-desktop"
}
