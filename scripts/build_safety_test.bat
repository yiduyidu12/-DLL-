@echo off
setlocal
cd /d "%~dp0.."

echo ============================================
echo  Step 0: Release old processes
echo ============================================
taskkill /F /IM test_safety.exe  >nul 2>nul
taskkill /F /IM test_full.exe    >nul 2>nul
taskkill /F /IM test_exception.exe >nul 2>nul

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

echo.
echo ============================================
echo  Compiling test_safety.cpp
echo ============================================
cl.exe /EHsc /utf-8 /Iinclude test/test_safety.cpp /link bin\Release\FileScanner.lib gdi32.lib ole32.lib /out:bin\Release\test_safety.exe

if %errorlevel% neq 0 (
    echo Build failed!
    exit /b 1
)

echo.
echo ============================================
echo  Running safety tests
echo ============================================
bin\Release\test_safety.exe

echo.
echo ============================================
echo  Cleanup
echo ============================================
taskkill /F /IM test_safety.exe >nul 2>nul
echo Done.
endlocal
