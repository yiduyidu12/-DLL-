@echo off
setlocal
cd /d "%~dp0.."

echo ============================================
echo  Step 0: Release old processes
echo ============================================
taskkill /F /IM test_full.exe       >nul 2>nul
taskkill /F /IM test_exception.exe  >nul 2>nul
taskkill /F /IM test_crash.exe      >nul 2>nul

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

echo.
echo ============================================
echo  Compiling test_full.cpp...
echo ============================================
cl.exe /EHsc /utf-8 /Iinclude test/test_full.cpp /link bin\Release\FileScanner.lib gdi32.lib ole32.lib /out:bin\Release\test_full.exe

if %errorlevel% neq 0 (
    echo Build failed!
    exit /b 1
)

echo.
echo Running test on __test_scan_data...
echo.
bin\Release\test_full.exe __test_scan_data

endlocal
