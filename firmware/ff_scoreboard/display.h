#pragma once
// -----------------------------------------------------------------------------
// display.h - HUB75 panel UI. The only display code in the project.
//
// Consumption-only: reads the plain structs in types.h owned by fantasy.cpp
// and renders a single, fixed scoreboard view:
//
//   YOU              OPP         <- WHITE team names on TOP, on the dark-green felt
//   [###]      [###]            <- BLACK LED panels with 3-cell square RED
//                                  7-seg scores (only lit bars, no outlines)
//   ooooooooooooooooooooooooooo  <- dotted win-probability bar: GOLD share
//                                  = your ESPN win %, remainder purple/blue
//   solid dark-green background, no border
//
// No view rotation: call displayRender() from loop(). It repaints when the
// fetched data changes, or animates the loader while waiting for the first poll.
// -----------------------------------------------------------------------------
#include <Arduino.h>

// Must be called once from setup() after Serial is up. Safe to fail (the device
// stays a headless data layer if the panel is absent).
void displayInit();

// Called every loop(); internally paces drawing.
void displayRender();

// True once a frame has been drawn at least once (handy for boot reporting).
bool displayReady();