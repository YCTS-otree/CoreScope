@echo off
setlocal
fltmc.exe >nul 2>&1
if errorlevel 1 (
    echo This script must be run from an Administrator command prompt.
    exit /b 1
)
set "DRIVER=%~dp0CpuFreqDrv_v1.0.0.sys"
if not exist "%DRIVER%" (
    echo Driver not found: "%DRIVER%"
    exit /b 1
)
sc.exe query CpuFreqMonitor >nul 2>&1
if not errorlevel 1 (
    sc.exe stop CpuFreqMonitor >nul 2>&1
    sc.exe delete CpuFreqMonitor >nul 2>&1
)
sc.exe create CpuFreqMonitor type= kernel start= demand binPath= "%DRIVER%" DisplayName= "CPU Frequency Monitor Driver"
if errorlevel 1 exit /b 1
sc.exe start CpuFreqMonitor
if errorlevel 1 exit /b 1
echo CpuFreqMonitor driver installed and started.
exit /b 0
