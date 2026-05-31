@echo off
cd %~dp0

call _Version.bat

if exist Output rd /Q /S Output
md Output
md Output\x64
md Output\ARM64

echo -- Compiling

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set MSBuildDir=%%i\MSBuild\Current\Bin\

REM Restore NuGet packages
"%MSBuildDir%MSBuild.exe" ..\OpenShell.sln /m /t:Restore -p:RestorePackagesConfig=true /verbosity:quiet /nologo

REM ********* Build x64 solution
echo --- x64
"%MSBuildDir%MSBuild.exe" ..\OpenShell.sln /m /t:Rebuild /p:Configuration="Setup" /p:Platform="x64" /verbosity:quiet /nologo
@if ERRORLEVEL 1 exit /b 1

REM ********* Build ARM64 solution
echo --- ARM64
"%MSBuildDir%MSBuild.exe" ..\OpenShell.sln /m /t:Rebuild /p:Configuration="Setup" /p:Platform="ARM64" /verbosity:quiet /nologo
if ERRORLEVEL 1 exit /b 1

REM ********* Build 32-bit solution (must be after 64-bit)
echo --- x86
"%MSBuildDir%MSBuild.exe" ..\OpenShell.sln /m /t:Rebuild /p:Configuration="Setup" /p:Platform="Win32" /verbosity:quiet /nologo
@if ERRORLEVEL 1 exit /b 1


REM ********* Make en-US.dll
cd ..
..\build\bin\Release\Utility.exe makeEN ..\build\bin\Setup\ClassicExplorer32.dll ..\build\bin\Setup\StartMenuDLL.dll ..\build\bin\Setup\ClassicIEDLL_32.dll ..\build\bin\Release\Update.exe
@if ERRORLEVEL 1 exit /b 1

..\build\bin\Release\Utility.exe extract en-US.dll en-US.csv
move en-US.dll Localization\English > nul
move en-US.csv Localization\English > nul

cd Setup


REM ********* Copy binaries

copy /B ..\..\build\bin\Setup\ClassicExplorer32.dll Output > nul
copy /B ..\..\build\bin\Setup\ClassicExplorerSettings.exe Output > nul
copy /B ..\..\build\bin\Setup\ClassicIEDLL_32.dll Output > nul
copy /B ..\..\build\bin\Setup\ClassicIE_32.exe Output > nul
copy /B ..\..\build\bin\Setup\StartMenu.exe Output > nul
copy /B ..\..\build\bin\Setup\StartMenuDLL.dll Output > nul
copy /B ..\..\build\bin\Setup\StartMenuHelper32.dll Output > nul
copy /B ..\..\build\bin\Release\Update.exe Output > nul
copy /B ..\..\build\bin\Release\DesktopToasts.dll Output > nul
copy /B ..\..\build\bin\Release\SetupHelper.exe Output > nul
copy /B ..\..\build\bin\Release\Utility.exe Output > nul

copy /B ..\..\build\bin\SetupARM64\ClassicExplorer64.dll Output\ARM64 > nul
copy /B ..\..\build\bin\SetupARM64\ClassicIEDLL_64.dll Output\ARM64 > nul
copy /B ..\..\build\bin\SetupARM64\ClassicIE_64.exe Output\ARM64 > nul
copy /B ..\..\build\bin\SetupARM64\StartMenu.exe Output\ARM64 > nul
copy /B ..\..\build\bin\SetupARM64\StartMenuDLL.dll Output\ARM64 > nul
copy /B ..\..\build\bin\SetupARM64\StartMenuHelper64.dll Output\ARM64 > nul

copy /B ..\..\build\bin\Setup64\ClassicExplorer64.dll Output\x64 > nul
copy /B ..\..\build\bin\Setup64\ClassicIEDLL_64.dll Output\x64 > nul
copy /B ..\..\build\bin\Setup64\ClassicIE_64.exe Output\x64 > nul
copy /B ..\..\build\bin\Setup64\StartMenu.exe Output\x64 > nul
copy /B ..\..\build\bin\Setup64\StartMenuDLL.dll Output\x64 > nul
copy /B ..\..\build\bin\Setup64\StartMenuHelper64.dll Output\x64 > nul

copy /B "..\..\build\bin\Skins\Classic Skin.skin" Output > nul
copy /B "..\..\build\bin\Skins\Full Glass.skin" Output > nul
copy /B "..\..\build\bin\Skins\Smoked Glass.skin" Output > nul
copy /B "..\..\build\bin\Skins\Windows Aero.skin" Output > nul
copy /B "..\..\build\bin\Skins\Windows Basic.skin" Output > nul
copy /B "..\..\build\bin\Skins\Windows XP Luna.skin" Output > nul
copy /B "..\..\build\bin\Skins\Windows 8.skin" Output > nul
copy /B "..\..\build\bin\Skins\Metro.skin" Output > nul
copy /B "..\..\build\bin\Skins\Classic Skin.skin7" Output > nul
copy /B "..\..\build\bin\Skins\Windows Aero.skin7" Output > nul
copy /B "..\..\build\bin\Skins\Windows 8.skin7" Output > nul
copy /B "..\..\build\bin\Skins\Midnight.skin7" Output > nul
copy /B "..\..\build\bin\Skins\Metro.skin7" Output > nul
copy /B "..\..\build\bin\Skins\Metallic.skin7" Output > nul
copy /B "..\..\build\bin\Skins\Immersive.skin" Output > nul
copy /B "..\..\build\bin\Skins\Immersive.skin7" Output > nul

REM ********* Build ADMX
echo --- ADMX
if exist Output\PolicyDefinitions.zip (
  del Output\PolicyDefinitions.zip
)
cd ..\Localization\English
..\..\..\build\bin\Setup\StartMenu.exe -saveadmx en-US
@if ERRORLEVEL 1 exit /b 1
..\..\..\build\bin\Setup\ClassicExplorerSettings.exe -saveadmx en-US
@if ERRORLEVEL 1 exit /b 1
..\..\..\build\bin\Setup\ClassicIE_32.exe -saveadmx en-US
@if ERRORLEVEL 1 exit /b 1
md en-US
copy /B *.adml en-US > nul
tar -a -c -f ..\..\Setup\Output\PolicyDefinitions.zip *.admx en-US\*.adml PolicyDefinitions.rtf
rd /Q /S en-US
cd ..\..\Setup

exit /b 0
