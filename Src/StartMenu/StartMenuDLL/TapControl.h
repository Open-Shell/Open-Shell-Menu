#pragma once

// Windows 11 draws the native Taskbar and Start button in XAML.
// These helpers control our TAP mechanism that allows to do changes in the XAML tree.

// Send updated settings to the TAP.
void UpdateTap();
// Stop the TAP and clean up resources.
void StopTap();
