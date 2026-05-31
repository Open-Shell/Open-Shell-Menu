@echo off

rem Convert . to _
set CS_VERSION_STR=%CS_VERSION:.=_%

set CS_VERSION_ORIG=%CS_VERSION%

rem Strip optional "-xyz" suffix from version
for /f "delims=- tokens=1,1" %%i in ("%CS_VERSION%") do set CS_VERSION=%%i
