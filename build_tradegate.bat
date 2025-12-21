@echo off
setlocal
set LOG=D:\dev\SierraChart\Studies\BuildFiles\TradeGate.build.log

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul

cl /Zc:wchar_t /GS /GL /W3 /O2 /Zc:inline /D NDEBUG /D _WINDOWS /D _USRDLL /D _WINDLL /Oy /Gd /Gy /Oi /GR- /GF /Ot /fp:precise /MT /std:c++17 /LD /EHa /WX- /nologo D:\dev\SierraChart\Studies\TradeGate.cpp /ID:\dev\SierraChart\Studies /IC:\SierraChart\ACS_Source /FoD:\dev\SierraChart\Studies\BuildFiles\ /link Gdi32.lib User32.lib /DLL /DYNAMICBASE /INCREMENTAL:NO /OPT:REF /OPT:ICF /MACHINE:X64 /OUT:C:\SierraChart\Data\TradeGate.dll /IMPLIB:D:\dev\SierraChart\Studies\BuildFiles\TradeGate.lib 1> "%LOG%" 2>&1

if not "%errorlevel%"=="0" (
  echo Build failed. Log:
  type "%LOG%"
)

exit /b %errorlevel%
