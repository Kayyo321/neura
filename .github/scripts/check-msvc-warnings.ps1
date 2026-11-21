param([Parameter(Mandatory)][string]$Log)

$ErrorActionPreference = 'Stop'
$warnings = Get-Content -LiteralPath $Log | Where-Object {
    $_ -match '\bwarning LNK4098\b' -or (
        $_ -match '\bwarning C[0-9]+\b' -and $_ -notmatch '[\\/](third_party|_deps|vcpkg|vcpkg_installed)[\\/]'
    )
}
if ($warnings) {
    $warnings
    throw 'The build produced Microsoft compiler warnings in Neura sources or mixed CRT warnings.'
}
