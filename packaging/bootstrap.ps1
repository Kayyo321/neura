# Downloads, verifies, and installs a Neura release on Windows. Each release
# publishes this file as install.ps1, so one PowerShell command installs
# Neura:
#
#   irm https://github.com/Kayyo321/neura/releases/latest/download/install.ps1 | iex
#
# Options go through a script block:
#
#   & ([scriptblock]::Create((irm https://github.com/Kayyo321/neura/releases/latest/download/install.ps1))) -Components runtime
#
#   -Components SET   sdk (the default), runtime, or sdk,service
#   -Version VERSION  install this version, such as 0.3.0 or 0.3.0-rc.1, in
#                     place of the latest release
#   -Source WHERE     read the release from an https:// URL or a local
#                     directory that holds the release metadata and archives
#   -Force            install again a generation that is already installed
#   -NoPathUpdate     do not add the installation to your user Path
#   -Help             print this text
#
# What it does, as the release's install.sh does on Linux and macOS
# (docs/design/Windows030.md, W24): finds this machine's target, reads the
# release's neura-components.txt, downloads the one or two archives the set
# of components needs with System32's curl.exe, checks each archive's size
# and SHA-256 against that file, checks its build provenance when the GitHub
# CLI (gh) is installed and signed in, checks that every member stays inside
# the archive's one directory, unpacks it with System32's tar.exe into a
# temporary directory, and runs the install.ps1 inside the first archive,
# with -Add for the second. That installer copies Neura under
# %LOCALAPPDATA%\Neura (NEURA_INSTALL_ROOT changes the place) and, unless
# -NoPathUpdate is given, adds <root>\current\bin to the Path value of
# HKCU\Environment. Nothing else is changed, and administrator rights are
# never asked for.
#
# It needs Windows 10 version 2004 or later, Windows PowerShell 5.1 or
# PowerShell 7, and the curl.exe and tar.exe that Windows provides.
#
# The whole script is one function that the last line calls, so a download
# that stops partway runs nothing. It never calls exit, which would close a
# PowerShell window that ran it through iex: a failure is an error that
# stops the function.

function Install-Neura {
    [CmdletBinding()]
    param(
        [string]$Components = 'sdk',
        [string]$Version = '',
        [string]$Source = '',
        [switch]$Force,
        [switch]$NoPathUpdate,
        [switch]$Help
    )

    Set-StrictMode -Version 2
    $ErrorActionPreference = 'Stop'
    $repository = 'Kayyo321/neura'

    if ($Help) {
        Write-Output @'
Installs Neura from a release.

  -Components SET   sdk (the default: the development tools, without the
                    service), runtime (the execution core and the service),
                    or sdk,service (both)
  -Version VERSION  install this version, such as 0.3.0 or 0.3.0-rc.1, in
                    place of the latest release
  -Source WHERE     read the release from an https:// URL or a local
                    directory that holds the release metadata and archives
  -Force            install again a generation that is already installed
  -NoPathUpdate     do not add <root>\current\bin to your user Path
  -Help             print this text

NEURA_INSTALL_ROOT sets where Neura is copied (default %LOCALAPPDATA%\Neura).
'@
        return
    }

    function Stop-Install([string]$Message) {
        throw "neura install: $Message"
    }

    # Windows PowerShell 5.1 may not offer TLS 1.2 to .NET's web requests by
    # default; curl.exe does the downloads, and this covers the rest.
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    } catch {
    }

    switch ($Components) {
        'sdk' { $products = @('sdk') }
        'runtime' { $products = @('runtime') }
        'sdk,service' { $products = @('sdk', 'runtime') }
        'service,sdk' { $products = @('sdk', 'runtime') }
        default { Stop-Install "-Components takes sdk, runtime or sdk,service, not $Components" }
    }
    $semver = '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?\z'
    if ($Version -and $Version -cnotmatch $semver) { Stop-Install "not a version: $Version" }
    if (-not $Source) {
        if ($Version) {
            $Source = "https://github.com/$repository/releases/download/v$Version"
        } else {
            $Source = "https://github.com/$repository/releases/latest/download"
        }
    }
    if ($Source.StartsWith('https://')) {
        $remote = $true
    } elseif ($Source -match '^[A-Za-z][A-Za-z0-9+.-]*://') {
        Stop-Install "-Source accepts an https:// URL or a local directory, not $Source"
    } else {
        $remote = $false
        if (-not (Test-Path -LiteralPath $Source -PathType Container)) { Stop-Install "$Source is not a directory" }
        $Source = (Get-Item -LiteralPath $Source).FullName
    }

    # The target triple of this machine. A 32-bit PowerShell on 64-bit
    # Windows sees the machine's processor in PROCESSOR_ARCHITEW6432.
    $architecture = $env:PROCESSOR_ARCHITEW6432
    if (-not $architecture) { $architecture = $env:PROCESSOR_ARCHITECTURE }
    switch ($architecture) {
        'AMD64' { $triple = 'x86_64-windows-msvc' }
        'ARM64' { Stop-Install 'Neura has no release for Windows on Arm64; see https://github.com/Kayyo321/neura/blob/main/docs/Install.md#supported-platforms' }
        default { Stop-Install "Neura has no release for the processor $architecture" }
    }
    $build = [Environment]::OSVersion.Version.Build
    if ($build -lt 19041) { Stop-Install "Neura needs Windows 10 version 2004 (build 19041) or later; this is build $build" }

    $system = [Environment]::GetFolderPath('System')
    $curl = Join-Path $system 'curl.exe'
    $tar = Join-Path $system 'tar.exe'
    if ($remote -and -not (Test-Path -LiteralPath $curl -PathType Leaf)) { Stop-Install "$curl is required and was not found" }
    if (-not (Test-Path -LiteralPath $tar -PathType Leaf)) { Stop-Install "$tar is required and was not found" }

    # curl.exe and tar.exe may read their command lines in the ANSI code
    # page, so they run in the work directory and get relative, ASCII names.
    $work = Join-Path ([IO.Path]::GetTempPath()) ('neura-install-' + [Guid]::NewGuid().ToString('N'))
    [void](New-Item -ItemType Directory -Path $work)
    Push-Location -LiteralPath $work
    try {
        # A native command's standard error is not an error of this script.
        $ErrorActionPreference = 'Continue'

        # Copies one file of the release into the work directory; returns
        # whether it is there.
        $fetch = {
            param([string]$Name, [bool]$Optional)
            if ($remote) {
                & $curl --proto '=https' --proto-redir '=https' --tlsv1.2 --ssl-revoke-best-effort --fail --location --silent `
                    --show-error --output $Name "$Source/$Name" 2>$null
                if ($LASTEXITCODE -eq 0) { return $true }
                Remove-Item -LiteralPath $Name -Force -ErrorAction SilentlyContinue
                if ($Optional) { return $false }
                Stop-Install "cannot download $Source/$Name"
            }
            $path = Join-Path $Source $Name
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                if ($Optional) { return $false }
                Stop-Install "$path does not exist"
            }
            Copy-Item -LiteralPath $path -Destination $Name
            return $true
        }

        if (-not (& $fetch 'neura-components.txt' $true)) {
            Stop-Install "the release has no component metadata (neura-components.txt); Neura releases before 0.3 have no Windows archive"
        }
        $lines = [IO.File]::ReadAllLines((Join-Path $work 'neura-components.txt'))
        if ($lines.Count -lt 3 -or $lines[0] -cne 'neura-components 1') {
            Stop-Install "$Source/neura-components.txt is not component metadata this script can read"
        }
        $released = ''
        if ($lines[1] -cmatch '^version ([0-9A-Za-z.-]+)\z') { $released = $Matches[1] }
        if (-not $released) { Stop-Install 'the component metadata names no version' }
        if ($Version -and $released -cne $Version) { Stop-Install "asked for version $Version, and the release metadata is for $released" }

        $tops = @()
        foreach ($product in $products) {
            $entries = @($lines | Where-Object {
                    $fields = $_.Split(' ')
                    $fields.Count -eq 6 -and $fields[0] -ceq 'archive' -and $fields[1] -ceq $product -and $fields[2] -ceq $triple
                })
            if ($entries.Count -eq 0) {
                Stop-Install "Neura $released has no $product archive for $triple. Supported platforms: https://github.com/$repository/blob/main/docs/Install.md#supported-platforms"
            }
            if ($entries.Count -gt 1) { Stop-Install "the component metadata lists the $product archive for $triple more than once" }
            $fields = $entries[0].Split(' ')
            $file = $fields[3]
            $top = "neura-$product-$released-$triple"
            if ($file -cne "$top.tar.gz") { Stop-Install "the component metadata names an unexpected file: $file" }
            Write-Output "Downloading Neura-$product $released for $triple"
            [void](& $fetch $file $false)

            # The size and digest the metadata gives.
            $size = (Get-Item -LiteralPath $file).Length
            if ([string]$size -cne $fields[5]) { Stop-Install "$file is $size bytes; the release metadata says $($fields[5])" }
            $actual = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($actual -cne $fields[4]) { Stop-Install "SHA-256 mismatch for ${file}: got $actual, the release metadata says $($fields[4])" }
            Write-Output "Verified: size and SHA-256 of $file against the release metadata"
            # gh can verify nothing until it is signed in. That is a missing
            # tool, not a failed verification: only a check that ran and
            # failed stops the install.
            $gh = Get-Command gh -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
            if (-not $remote) {
                Write-Output 'Not verified: build provenance (the source is a local directory)'
            } elseif (-not $gh) {
                Write-Output 'Not verified: build provenance (gh, the GitHub CLI, is not installed)'
            } else {
                & $gh.Source auth status *> $null
                if ($LASTEXITCODE -ne 0) {
                    Write-Output 'Not verified: build provenance (gh is installed but not signed in; run "gh auth login" to enable the check)'
                } else {
                    $log = & $gh.Source attestation verify $file --repo $repository 2>&1 | Out-String
                    if ($LASTEXITCODE -ne 0) {
                        [Console]::Error.WriteLine(($log -split "`n" | ForEach-Object { "  $_" }) -join "`n")
                        Stop-Install "gh attestation verify did not confirm the build provenance of $file. Nothing was installed"
                    }
                    Write-Output "Verified: build provenance with gh attestation verify --repo $repository"
                }
            }

            # Every member must be inside the one expected directory, a
            # regular file or a directory, with a name Windows keeps as it
            # is: no '..', '\', ':', reserved device name, or trailing dot or
            # space.
            $names = @(& $tar -tzf $file 2>$null)
            if ($LASTEXITCODE -ne 0 -or $names.Count -eq 0) { Stop-Install "$file cannot be listed. Nothing was installed" }
            $kinds = @(& $tar -tvzf $file 2>$null)
            if ($LASTEXITCODE -ne 0 -or $kinds.Count -ne $names.Count) { Stop-Install "$file cannot be listed. Nothing was installed" }
            for ($i = 0; $i -lt $names.Count; $i++) {
                $name = $names[$i].TrimEnd("`r")
                if ($name -cne $top -and -not $name.StartsWith("$top/")) { Stop-Install "$file holds a member outside $top/. Nothing was installed" }
                foreach ($part in $name.TrimEnd('/').Split('/')) {
                    if ($part -eq '' -or $part -eq '.' -or $part -eq '..' -or $part.Contains('\') -or $part.Contains(':') -or
                        $part.EndsWith('.') -or $part.EndsWith(' ') -or
                        $part.Split('.')[0].TrimEnd(' ') -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9]|CONIN\$|CONOUT\$)\z') {
                        Stop-Install "$file holds the member $name, which Windows cannot keep as named. Nothing was installed"
                    }
                }
                if (-not ($kinds[$i].StartsWith('-') -or $kinds[$i].StartsWith('d'))) {
                    Stop-Install "$file holds $name, which is not a regular file or a directory. Nothing was installed"
                }
            }
            [void](New-Item -ItemType Directory -Path 'unpack' -Force)
            & $tar -xzf $file -C unpack 2>$null
            if ($LASTEXITCODE -ne 0) { Stop-Install "$file cannot be unpacked. Nothing was installed" }
            if (-not (Test-Path -LiteralPath (Join-Path $work "unpack\$top\install.ps1") -PathType Leaf)) {
                Stop-Install "$file has no install.ps1"
            }
            $tops += $top
        }

        # The archive's own installer, in a PowerShell of the same edition
        # with the script policy bypassed for it alone: the policy of this
        # machine may refuse script files, which "irm | iex" is not.
        if ($PSVersionTable.PSEdition -eq 'Core') {
            $shell = Join-Path $PSHOME 'pwsh.exe'
        } else {
            $shell = Join-Path $PSHOME 'powershell.exe'
        }
        $installer = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $work "unpack\$($tops[0])\install.ps1"))
        if ($tops.Count -eq 2) { $installer += @('-Add', (Join-Path $work "unpack\$($tops[1])")) }
        if ($Force) { $installer += '-Force' }
        if ($NoPathUpdate) { $installer += '-NoPathUpdate' }
        & $shell @installer
        if ($LASTEXITCODE -ne 0) { Stop-Install "the installer failed (exit status $LASTEXITCODE)" }
    } finally {
        Pop-Location
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Install-Neura @args
