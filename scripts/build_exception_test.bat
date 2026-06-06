@echo off
setlocal
cd /d "%~dp0.."

echo ============================================
echo  Step 0: Kill old processes and free DLL
echo ============================================
taskkill /F /IM test_exception.exe >nul 2>nul

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

echo Compiling test_exception.cpp...
cl.exe /EHsc /utf-8 /Iinclude test/test_exception.cpp /link bin\Release\FileScanner.lib gdi32.lib ole32.lib /out:bin\Release\test_exception.exe

if %errorlevel% neq 0 (
    echo Build failed!
    exit /b 1
)

echo Build completed!
echo.
bin\Release\test_exception.exe

echo.
echo ============================================
echo  Cleanup
echo ============================================
taskkill /F /IM test_exception.exe >nul 2>nul
echo Done.

endlocal
