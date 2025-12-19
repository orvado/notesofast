@echo off
setlocal

:: Configure vcpkg paths for hunspell
:: Prioritize the user's local vcpkg if it exists
if exist "C:\Users\Ken\vcpkg\installed\x86-windows\include\hunspell\hunspell.hxx" (
    set VCPKG_ROOT=C:\Users\Ken\vcpkg
    set VCPKG_TRIPLET=x86-windows
) else if "%VCPKG_ROOT%"=="" (
    set VCPKG_ROOT=c:\src\vcpkg
    set VCPKG_TRIPLET=x64-windows
)

if "%VCPKG_TRIPLET%"=="" set VCPKG_TRIPLET=x86-windows
set VCPKG_INC=%VCPKG_ROOT%\installed\%VCPKG_TRIPLET%\include
set VCPKG_LIB=%VCPKG_ROOT%\installed\%VCPKG_TRIPLET%\lib

if not exist build mkdir build
if not exist build\dict mkdir build\dict

:: Compile resources
rc /i src /fo build\resource.res src\resource.rc

cl /EHsc /DUNICODE /D_UNICODE /Iinclude /Isrc /I"%VCPKG_INC%" src\*.cpp lib\sqlite3.c /Febuild\NoteSoFast.exe ^
	/link build\resource.res /LIBPATH:"%VCPKG_LIB%" hunspell-1.7.lib intl.lib iconv.lib user32.lib gdi32.lib comctl32.lib shell32.lib comdlg32.lib

if exist dict\en\en_US.aff copy /Y dict\en\en_US.aff build\dict >nul
if exist dict\en\en_US.dic copy /Y dict\en\en_US.dic build\dict >nul

copy /Y "%VCPKG_ROOT%\installed\%VCPKG_TRIPLET%\bin\hunspell-1.7-0.dll" build >nul
copy /Y "%VCPKG_ROOT%\installed\%VCPKG_TRIPLET%\bin\intl-8.dll" build >nul
copy /Y "%VCPKG_ROOT%\installed\%VCPKG_TRIPLET%\bin\iconv-2.dll" build >nul

endlocal
