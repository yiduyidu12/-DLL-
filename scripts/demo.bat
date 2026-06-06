@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
taskkill /F /IM demo.exe >nul 2>nul
copy /Y bin\Release\FileScanner.dll bin\Release\ >nul 2>nul
cl.exe /EHsc /utf-8 /Iinclude scripts/demo.cpp /link bin\Release\FileScanner.lib gdi32.lib ole32.lib /out:bin\Release\demo.exe
if %errorlevel% neq 0 exit /b 1
bin\Release\demo.exe
taskkill /F /IM demo.exe >nul 2>nul
endlocal
