#pragma once
// -----------------------------------------------------------------------------
// display.h - HUB75 panel UI. The only display code in the project.
//
// Consumption-only: reads the plain structs in types.h owned by fantasy.cpp
// and renders a single, fixed scoreboard view:
//
//   TAK              OPP         <- WHITE team names on TOP (half the score size)
//   [###]      [###]            <- 3-cell square ORANGE 7-seg scores right-aligned
//                                  (unused cells show dim outlines), so hundreds fit
//   ooooooooooooooooooooooooooo  <- dotted win-probability bar: green share = dad's
//                                  ESPN win %, remainder red
//   one 1px red frame around the full board edge
//
// No view rotation: call displayRender() from loop() and it repaints every
// DISPLAY_BLINK_MS.
// -----------------------------------------------------------------------------
#include <Arduino.h>

// Must be called once from setup() after Serial is up. Safe to fail (the device
// stays a headless data layer if the panel is absent).
void displayInit();

// Called every loop(); internally paces drawing.
void displayRender();

// True once a frame has been drawn at least once (handy for boot reporting).
bool displayReady();