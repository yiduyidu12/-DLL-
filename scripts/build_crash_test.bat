@echo off
setlocal
cd /d "%~dp0.."

echo ============================================
echo  Step 0: Kill old processes
echo ============================================
taskkill /F /IM test_crash.exe >nul 2>nul

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

echo Compiling test_crash.cpp...
cl.exe /EHsc /utf-8 /Iinclude test/test_crash.cpp /link bin\Release\FileScanner.lib gdi32.lib ole32.lib /out:bin\Release\test_crash.exe

if %errorlevel% neq 0 (
    echo Build failed!
    exit /b 1
)

echo Build completed!
echo.
bin\Release\test_crash.exe

echo.
echo ============================================
echo  Cleanup
echo ============================================
taskkill /F /IM test_crash.exe >nul 2>nul
echo Done.

endlocal
