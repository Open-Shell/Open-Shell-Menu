@echo off
set PATH=C:\Program Files\7-Zip\;C:\Program Files (x86)\HTML Help Workshop;C:\Program Files (x86)\WiX Toolset v3.14\bin\;%PATH%

cd %~dp0

rem Clean repository and build fresh. Will erase current changes so disabled by default.
rem git clean -dfx

rem Default version
set CS_VERSION=4.4.1000

if defined APPVEYOR_BUILD_VERSION (
	set CS_VERSION=%APPVEYOR_BUILD_VERSION%
)

echo Version: %CS_VERSION%

rem Convert . to _
set CS_VERSION_STR=%CS_VERSION:.=_%

set CS_VERSION_ORIG=%CS_VERSION%
rem Strip optional "-xyz" suffix from version
for /f "delims=- tokens=1,1" %%i in ("%CS_VERSION%") do set CS_VERSION=%%i

call BuildBinaries.bat
if ERRORLEVEL 1 exit /b 1

call _BuildEnglish.bat
if ERRORLEVEL 1 exit /b 1

call BuildArchives.bat
if ERRORLEVEL 1 exit /b 1

exit /b 0
