#pragma once
#include <memory>
#include "window.hpp"

extern std::shared_ptr<Window> clock_window;
void InitializeClock();
void DrawClock(PixelWriter &writer);