@echo off
if not exist build mkdir build
cl /EHsc /DUNICODE /D_UNICODE /Iinclude src\*.cpp lib\sqlite3.c /Febuild\NoteSoFast.exe /link user32.lib gdi32.lib comctl32.lib shell32.lib comdlg32.lib
