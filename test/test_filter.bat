@echo off
setlocal
cd /d "%~dp0.."

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
taskkill /F /IM test_filter.exe >nul 2>nul
cl.exe /EHsc /utf-8 /Iinclude test/test_filter.cpp /link bin\Release\FileScanner.lib gdi32.lib ole32.lib /out:bin\Release\test_filter.exe
if %errorlevel% neq 0 exit /b 1
bin\Release\test_filter.exe
taskkill /F /IM test_filter.exe >nul 2>nul
endlocal
