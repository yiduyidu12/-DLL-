@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
taskkill /F /IM test_timestamp.exe >nul 2>nul
for /L %%i in (1,1,5) do ( copy /Y "bin\Release\FileScanner.dll" ".\FileScanner.dll" >nul 2>nul && goto :ok )
:ok
cl.exe /EHsc /utf-8 /Iinclude test_timestamp.cpp /link bin\Release\FileScanner.lib gdi32.lib ole32.lib /out:test_timestamp.exe
if %errorlevel% neq 0 exit /b 1
test_timestamp.exe
taskkill /F /IM test_timestamp.exe >nul 2>nul
endlocal
