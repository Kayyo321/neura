# Installs neura from an unpacked release archive on Windows.
#
# Run this from the directory the archive unpacked to, the one that holds
# bin\, share\, lib\, install.ps1, install.cmd and VERSION:
#
#   powershell -ExecutionPolicy Bypass -File .\install.ps1
#
# or run install.cmd beside it, which does the same. It checks the archive
# and hands the installation to the archive's own bin\neura-update.exe,
# which verifies every file against the component manifests, takes the
# installation lock, and makes the change with a journal (docs/Install.md).
# All the checks are neura-update's; this script is a front for it
# (docs/design/Windows030.md, W24). It works in Windows PowerShell 5.1 and
# PowerShell 7 and never asks for administrator rights.
#
# The installation's tools are reached through one directory,
# <root>\current\bin. Unless -NoPathUpdate is given, the script adds it to
# the Path value of HKCU\Environment, where Windows keeps a user's own PATH,
# and tells running programs that the environment changed; new terminals
# then find neura. "neura uninstall" removes the entry again.

[CmdletBinding()]
param(
    # Install again a generation that is already installed: a whole one is
    # kept, a damaged one replaced.
    [switch]$Force,
    # Also install the components of a second unpacked archive of the same
    # build, such as the runtime archive beside the SDK.
    [string]$Add = '',
    # Leave the user's Path as it is.
    [switch]$NoPathUpdate,
    [switch]$Help
)

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

function Write-Usage {
    @'
usage: install.ps1 [-Force] [-Add <dir>] [-NoPathUpdate] [-Help]

Installs the neura components in this unpacked release archive for the
current user.

  <root>\versions\<generation>\  the components of this archive (and of -Add)
  <root>\current                 a junction to the active generation
  <root>\neura-install.txt       a marker that "neura update" and "neura uninstall" read
  <root>\current\bin             the one directory of every tool

Options:
  -Force          install again a generation that is already installed: a whole
                  one is kept, a damaged one replaced
  -Add <dir>      also install the components of a second unpacked archive of
                  the same build, such as the runtime archive beside the SDK
  -NoPathUpdate   do not add <root>\current\bin to your user Path
  -Help           print this text

Environment variables:
  NEURA_INSTALL_ROOT  the install root (default: %LOCALAPPDATA%\Neura)

install.cmd runs this script with the same options. The installer refuses to
touch an install that a package manager owns.
'@
}

# The system's reason for a program that did not start, such as an
# application control policy that blocks it.
function Get-Reason($Record) {
    if ($Record.Exception.InnerException) { return $Record.Exception.InnerException.Message }
    return $Record.Exception.Message
}

function Stop-Install([string]$Message) {
    [Console]::Error.WriteLine("install.ps1: $Message")
    exit 1
}

# Whether a Path entry names `Directory`, ignoring case, quotes, '/' or '\'
# and a final separator, after expanding %VARIABLE% references.
function Test-SameDirectory([string]$Entry, [string]$Directory) {
    $normalize = {
        param([string]$Text)
        $Text = [Environment]::ExpandEnvironmentVariables($Text.Trim().Trim('"'))
        $Text.Replace('/', '\').TrimEnd('\')
    }
    $a = & $normalize $Entry
    $b = & $normalize $Directory
    return ($a.Length -gt 0) -and [string]::Equals($a, $b, [StringComparison]::OrdinalIgnoreCase)
}

function Test-PathLists([string]$Value, [string]$Directory) {
    foreach ($entry in $Value.Split(';')) {
        if (Test-SameDirectory $entry $Directory) { return $true }
    }
    return $false
}

# Tells running programs, Explorer among them, that the environment changed.
function Send-EnvironmentChange {
    try {
        if (-not ('Neura.Environment' -as [type])) {
            Add-Type -Namespace Neura -Name Environment -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern System.IntPtr SendMessageTimeout(System.IntPtr window, uint message, System.UIntPtr wParam, string lParam,
    uint flags, uint timeout, out System.UIntPtr result);
'@
        }
        $result = [UIntPtr]::Zero
        # HWND_BROADCAST, WM_SETTINGCHANGE, SMTO_ABORTIFHUNG, 5 seconds.
        [void][Neura.Environment]::SendMessageTimeout([IntPtr]0xffff, 0x1a, [UIntPtr]::Zero, 'Environment', 2, 5000, [ref]$result)
        return $true
    } catch {
        return $false
    }
}

# Puts `Directory` first in the user's Path, keeping the value's type, so
# that entries such as %USERPROFILE%\bin keep expanding.
function Add-UserPath([string]$Directory) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Environment')
    try {
        $current = ''
        $kind = [Microsoft.Win32.RegistryValueKind]::ExpandString
        if ($key.GetValueNames() -contains 'Path') {
            $current = [string]$key.GetValue('Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            $kind = $key.GetValueKind('Path')
        }
        if (Test-PathLists $current $Directory) { return $false }
        $value = $Directory
        if ($current.Trim(';').Length -gt 0) { $value = $Directory + ';' + $current.TrimStart(';') }
        $key.SetValue('Path', $value, $kind)
    } finally {
        $key.Close()
    }
    if (-not (Send-EnvironmentChange)) {
        Write-Output "install.ps1: programs that are running were not told of the new Path; sign out and in again if a new terminal does not find neura"
    }
    return $true
}

if ($Help) {
    Write-Usage
    exit 0
}

$source = $PSScriptRoot
if (-not $source) { Stop-Install 'cannot find the directory of this script' }
$versionFile = Join-Path $source 'VERSION'
if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) {
    Stop-Install "$versionFile is missing; run install.ps1 from an unpacked release archive"
}
$version = [IO.File]::ReadAllText($versionFile)
if ($version.EndsWith("`n")) { $version = $version.Substring(0, $version.Length - 1) }
if ($version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?\z') {
    Stop-Install "VERSION does not hold a semantic version: $version"
}
$updater = Join-Path $source 'bin\neura-update.exe'
if (-not (Test-Path -LiteralPath $updater -PathType Leaf)) { Stop-Install "$updater is missing" }
# A native command's standard error is not an error of this script (in
# Windows PowerShell 5.1 a redirected line of it would stop it).
$ErrorActionPreference = 'Continue'
try {
    $line = @(& $updater --version 2>$null)[0]
} catch {
    Stop-Install "$updater does not run on this machine: $(Get-Reason $_)"
}
if ($LASTEXITCODE -ne 0) { Stop-Install "$updater does not run on this machine" }
$ErrorActionPreference = 'Stop'
if (-not ([string]$line).StartsWith("neura $version ")) { Stop-Install "$updater reports `"$line`"; expected neura $version" }

$arguments = @('update', '--install-from', $source)
if ($Add) {
    $item = Get-Item -LiteralPath $Add -ErrorAction SilentlyContinue
    if (-not $item -or -not $item.PSIsContainer) { Stop-Install "-Add: $Add is not a directory" }
    $arguments += @('--add', $item.FullName)
}
if ($Force) { $arguments += '--force' }

# The PATH entry: <root>\current\bin, the root as neura-update decides it.
$root = $env:NEURA_INSTALL_ROOT
if (-not $root) { $root = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Neura' }
$entry = $null
if ([IO.Path]::IsPathRooted($root)) { $entry = Join-Path $root 'current\bin' }
$userPath = [string][Environment]::GetEnvironmentVariable('Path', 'User')
$updatePath = (-not $NoPathUpdate) -and $entry -and -not (Test-PathLists $userPath $entry)

# When this script adds the entry, neura-update is told so through its own
# PATH, and so does not print the instruction to add it.
$savedPath = $env:Path
if ($updatePath) { $env:Path = $entry + ';' + $env:Path }
$status = 1
$ErrorActionPreference = 'Continue'
try {
    & $updater @arguments
    $status = $LASTEXITCODE
} catch {
    Stop-Install "$updater did not run: $(Get-Reason $_)"
} finally {
    $env:Path = $savedPath
    $ErrorActionPreference = 'Stop'
}
if ($status -ne 0) { exit $status }

if ($updatePath) {
    # The marker names the entry as neura-update resolved the root, which
    # is the spelling "neura uninstall" removes.
    $marker = Join-Path $root 'neura-install.txt'
    $bin = $null
    foreach ($text in [IO.File]::ReadAllLines($marker)) {
        if ($text.StartsWith('bin ')) { $bin = $text.Substring(4).Replace('/', '\') }
    }
    if (-not $bin) { Stop-Install "$marker names no bin directory" }
    if (Add-UserPath $bin) {
        Write-Output "install.ps1: added $bin to your user Path; open a new terminal to use neura"
    }
}
exit 0
