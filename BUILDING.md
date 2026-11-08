# Building the Start Menu with Visual Studio

Open `OpenShell-VS2026.sln` in Visual Studio 2026 and choose **Release / x64**.
Install the Desktop development with C++ workload, C++ ATL support for the
selected toolset, and a Windows 11 SDK. Allow NuGet package restore.

The root solution builds the Start Menu, its helper DLL, and the bundled skins.
It does not build an installer or the Classic Explorer/Classic IE components.
Build output is under `build/bin/Release64`; skins are copied into its `Skins`
subfolder. Local binaries are unsigned. Language DLLs are distributed separately.

From a Visual Studio developer command prompt:

```bat
MSBuild OpenShell-VS2026.sln /t:Restore /p:RestorePackagesConfig=true
MSBuild OpenShell-VS2026.sln /m /p:Configuration=Release /p:Platform=x64
```

Projects select v145 with Visual Studio 2026 and retain v143 with Visual Studio
2022. `OpenShellPlatformToolset` can override this selection. The existing
`Src/OpenShell.sln` and release packaging scripts remain available; see
`Src/BUILDME.txt` for the additional installer tools.

Release scripts can continue to supply `CS_VERSION` and `CS_VERSION_ORIG`.
Direct IDE builds default to version 4.4.1000.
