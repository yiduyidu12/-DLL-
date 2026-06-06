@echo off
setlocal
cd /d "%~dp0.."

echo ============================================
echo  Step 0: Kill old processes
echo ============================================
taskkill /F /IM test_full.exe       >nul 2>nul
taskkill /F /IM test_exception.exe  >nul 2>nul
taskkill /F /IM test_crash.exe      >nul 2>nul

echo.
echo ============================================
echo  Step 1: Generate test data
echo ============================================
powershell -ExecutionPolicy Bypass -File scripts/gen_test_data.ps1

echo.
echo ============================================
echo  Step 2: Compile test program
echo ============================================
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cl /EHsc /utf-8 /Iinclude test/test_full.cpp /link bin\Release\FileScanner.lib gdi32.lib ole32.lib /out:bin\Release\test_full.exe
if %errorlevel% neq 0 (
    echo Compile failed!
    pause
    exit /b 1
)

echo.
echo ============================================
echo  Step 3: Run tests
echo ============================================
bin\Release\test_full.exe __test_scan_data

echo.
echo ============================================
echo  Step 4: Cleanup
echo ============================================
taskkill /F /IM test_full.exe >nul 2>nul

echo Done.
endlocal
