@echo off
setlocal
cd /d "%~dp0.."

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

mkdir bin\Release 2>NUL
mkdir obj\TestApp\Release 2>NUL

echo Compiling TestApp.cpp...
cl.exe /c /EHsc /O2 /utf-8 /Iinclude src\TestApp\TestApp.cpp /Fo:obj\TestApp\Release\TestApp.obj

echo Linking TestApp.exe...
link.exe obj\TestApp\Release\TestApp.obj bin\Release\FileScanner.lib kernel32.lib user32.lib comctl32.lib shell32.lib gdi32.lib ole32.lib /OUT:bin\Release\TestApp.exe

echo Build completed!
dir /b bin\Release\TestApp.exe

endlocal
