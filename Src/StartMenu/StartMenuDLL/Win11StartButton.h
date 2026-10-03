#pragma once

// Windows 11 draws the native Start button in XAML. These helpers hide only
// the native glyph/hit target while Open-Shell's replacement button is active.
// They never move or resize either button.
void StartWin11StartButtonMonitor( void );
void UpdateWin11StartButtonMonitor( void );
void StopWin11StartButtonMonitor( void );
