@echo off
setlocal

:: Configure vcpkg paths for hunspell
if "%VCPKG_ROOT%"=="" set VCPKG_ROOT=c:\src\vcpkg
if "%VCPKG_TRIPLET%"=="" set VCPKG_TRIPLET=x64-windows
set VCPKG_INC=%VCPKG_ROOT%\installed\%VCPKG_TRIPLET%\include
set VCPKG_LIB=%VCPKG_ROOT%\installed\%VCPKG_TRIPLET%\lib

if not exist build mkdir build
cl /EHsc /DUNICODE /D_UNICODE /Iinclude /I"%VCPKG_INC%" src\*.cpp lib\sqlite3.c /Febuild\NoteSoFast.exe ^
	/link /LIBPATH:"%VCPKG_LIB%" hunspell-1.7.lib libintl.lib libiconv.lib user32.lib gdi32.lib comctl32.lib shell32.lib comdlg32.lib

if exist dict\en_US.aff copy /Y dict\en_US.aff build >nul
if exist dict\en_US.dic copy /Y dict\en_US.dic build >nul

endlocal
