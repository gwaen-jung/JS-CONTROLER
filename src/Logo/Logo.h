#pragma once

#include <Arduino.h>

// Logo ReShape Lab (banh rang + chip) dang bitmap 1bpp.
constexpr int kLogoTftW = 181;
constexpr int kLogoTftH = 200;
constexpr int kLogoOledBigW = 36;
constexpr int kLogoOledBigH = 40;
constexpr int kLogoOledSmallW = 25;
constexpr int kLogoOledSmallH = 28;

extern const uint8_t kLogoTft[];
extern const uint8_t kLogoOledBig[];
extern const uint8_t kLogoOledSmall[];
