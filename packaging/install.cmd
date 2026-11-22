@echo off
rem Installs neura from an unpacked release archive on Windows: runs
rem install.ps1 beside this file in Windows PowerShell, with the same
rem options (-Force, -Add <dir>, -NoPathUpdate, -Help). It exists for
rem Command Prompt and for a script policy that would refuse install.ps1:
rem the policy is bypassed for this one script only. Every check is
rem bin\neura-update.exe's (docs/design/Windows030.md, W24).
setlocal
set "powershell=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%powershell%" (
    echo install.cmd: %powershell% is missing; run install.ps1 with PowerShell 1>&2
    exit /b 1
)
"%powershell%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
exit /b %ERRORLEVEL%
