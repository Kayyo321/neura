$ErrorActionPreference = 'Stop'
# Use the runner's Visual Studio installation; no third-party setup action.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio 2022 C++ tools were not found' }
$vcvars = Join-Path $visualStudio 'VC\Auxiliary\Build\vcvars64.bat'
$developerEnvironment = & $env:ComSpec /d /c "call `"$vcvars`" >nul && set"
if ($LASTEXITCODE) { throw 'vcvars64.bat failed' }
foreach ($line in $developerEnvironment) {
    if ($line -match '^([^=]+)=(.*)$' -and $Matches[1] -notmatch '^(GITHUB_|RUNNER_|VCPKG_ROOT$)') {
        $name = $Matches[1]
        $value = $Matches[2]
        if ([Environment]::GetEnvironmentVariable($name, 'Process') -cne $value) {
            [Environment]::SetEnvironmentVariable($name, $value, 'Process')
            "$name=$value" | Out-File -Append -Encoding utf8 $env:GITHUB_ENV
        }
    }
}
$vcpkg = $env:VCPKG_INSTALLATION_ROOT
if (-not $vcpkg) { $vcpkg = 'C:\vcpkg' }
if (-not (Test-Path -LiteralPath "$vcpkg\scripts\buildsystems\vcpkg.cmake")) { throw "vcpkg was not found at $vcpkg" }
$llvm = Join-Path $visualStudio 'VC\Tools\Llvm\x64\bin'
$libclang = Join-Path $llvm 'libclang.dll'
if (-not (Test-Path -LiteralPath $libclang)) { throw 'The Visual Studio LLVM libclang component is required' }
$llvm | Out-File -Append -Encoding utf8 $env:GITHUB_PATH
"VCPKG_ROOT=$vcpkg" | Out-File -Append -Encoding utf8 $env:GITHUB_ENV
"NEURA_LIBCLANG=$libclang" | Out-File -Append -Encoding utf8 $env:GITHUB_ENV
"NEURA_LIBCLANG_REQUIRED=1" | Out-File -Append -Encoding utf8 $env:GITHUB_ENV
"JOBS=$env:NUMBER_OF_PROCESSORS" | Out-File -Append -Encoding utf8 $env:GITHUB_ENV
"VCPKG_DEFAULT_BINARY_CACHE=$env:RUNNER_TEMP\vcpkg-cache" | Out-File -Append -Encoding utf8 $env:GITHUB_ENV
New-Item -ItemType Directory -Force "$env:RUNNER_TEMP\vcpkg-cache" | Out-Null
$compiler = (Get-Item -LiteralPath (Get-Command cl.exe).Source).VersionInfo.FileVersion -replace '[^0-9A-Za-z_.-]', '-'
"compiler=$compiler" | Out-File -Append -Encoding utf8 $env:GITHUB_OUTPUT
Write-Output $compiler
