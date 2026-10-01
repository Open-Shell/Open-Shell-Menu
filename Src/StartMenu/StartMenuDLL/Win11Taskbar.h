// Windows 11 XAML taskbar appearance.
// The Win32 taskbar paint path does not draw the bar the user sees.

#pragma once

// Re-read the existing taskbar settings and apply them to the Windows 11 XAML bar.
// No-op on older Windows, and until the diagnostics tap is connected inside Explorer.
void ApplyWin11Taskbar( void );

// Connect to the XAML visual tree from inside Explorer. No-op in any other process.
void StartWin11TaskbarConnect( void );
