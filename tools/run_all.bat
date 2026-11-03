@echo off
rem Run every test exe in this folder (or build\) and write results\<area>.txt.
rem usage: run_all.bat [/destructive]   (dx_* areas run only when /destructive is given)
setlocal enabledelayedexpansion
set DIR=%~dp0..\build
if not exist "%DIR%" set DIR=%~dp0
if not exist "%DIR%\results" mkdir "%DIR%\results"
set DESTRUCTIVE=0
if /i "%1"=="/destructive" set DESTRUCTIVE=1
for %%F in ("%DIR%\*.exe") do (
  set N=%%~nF
  set SKIP=0
  if /i "!N:~0,3!"=="dx_" if "!DESTRUCTIVE!"=="0" set SKIP=1
  if "!SKIP!"=="0" (
    echo === !N!
    rem 600 s hard limit per exe
    start /wait /b "" cmd /c "%%F > "%DIR%\results\!N!.txt" 2>&1"
  ) else echo --- skipping !N! ^(destructive^)
)
python "%~dp0summarize.py" "%DIR%\results" 2>nul
