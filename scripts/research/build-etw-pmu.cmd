@echo off
setlocal
cd /d "%~dp0\..\.."
if not exist build mkdir build
for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -find VC/Auxiliary/Build/vcvars64.bat`) do call "%%I" >nul
where cl >nul 2>nul
if errorlevel 1 exit /b 1
cl /nologo /O2 /EHsc /std:c++17 scripts\research\etw-pmu-check.cpp /Fo:build\etw-pmu-check.obj /Fe:build\etw-pmu-check.exe
exit /b %errorlevel%
