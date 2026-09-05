@echo off
setlocal
fltmc.exe >nul 2>&1
if errorlevel 1 (
    echo This script must be run from an Administrator command prompt.
    exit /b 1
)
sc.exe stop CpuFreqMonitor >nul 2>&1
sc.exe delete CpuFreqMonitor
exit /b %errorlevel%
