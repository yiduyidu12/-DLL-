@echo off
setlocal
cd /d "%~dp0.."

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

mkdir bin\Release 2>NUL
mkdir obj\FileScanner\Release 2>NUL

echo Compiling Filter.cpp...
cl.exe /c /EHsc /O2 /utf-8 /DNDEBUG /Iinclude src\FileScanner\Filter.cpp /Fo:obj\FileScanner\Release\Filter.obj

echo Compiling FileScanner.cpp...
cl.exe /c /EHsc /O2 /utf-8 /DNDEBUG /DFILESCANNER_EXPORTS /Iinclude src\FileScanner\FileScanner.cpp /Fo:obj\FileScanner\Release\FileScanner.obj

echo Linking FileScanner.dll...
link.exe /DLL /OUT:bin\Release\FileScanner.dll obj\FileScanner\Release\Filter.obj obj\FileScanner\Release\FileScanner.obj kernel32.lib

echo Build completed!
copy /Y bin\Release\FileScanner.dll . >NUL
copy /Y bin\Release\FileScanner.lib . >NUL
dir /b bin\Release\FileScanner.dll

endlocal
