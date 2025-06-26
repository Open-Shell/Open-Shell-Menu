#pragma once

// The following macros define the minimum required platform.  The minimum required platform
// is the earliest version of Windows, Internet Explorer etc. that has the necessary features to run 
// your application.  The macros work by enabling all features available on platform versions up to and 
// including the version specified.

// Modify the following defines if you have to target a platform prior to the ones specified below.
// Refer to MSDN for the latest info on corresponding values for different platforms.

#ifndef _WIN32_WINNT                   // Specifies that the minimum required platform is Windows 7.
#define _WIN32_WINNT _WIN32_WINNT_WIN7 // Change this to the appropriate value to target other versions of Windows.
#endif

#ifndef _WIN32_IE                      // Specifies that the minimum required platform is Internet Explorer 11.0.
#define _WIN32_IE _WIN32_IE_IE110      // Change this to the appropriate value to target other versions of IE.
#endif

#ifndef NTDDI_VERSION                  // Specifies that the minimum required platform is NTDDI for Windows 8.
#define NTDDI_VERSION NTDDI_WIN8       // Change this to the appropriate value to target other versions of NTDDI.
#endif
