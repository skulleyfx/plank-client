#pragma once

#include <algorithm>
#include <cmath>

// The pen protocol carries a tilt magnitude in degrees from vertical and a
// rotation in degrees clockwise from north, while every tablet stack reports
// two independent tilt angles. Both the libinput capture on Linux and the SDL
// capture on Windows convert through here, so a pen leaning the same way
// produces the same numbers whichever client an artist sits at.
namespace PlankPen {

inline void encodeTilt(double tiltX, double tiltY,
                       unsigned short& rotation, unsigned char& tilt)
{
    const double pi = std::acos(-1.0);
    const double x = std::tan(tiltX * pi / 180.0);
    const double y = std::tan(tiltY * pi / 180.0);
    const double magnitude = std::atan(std::hypot(x, y)) * 180.0 / pi;
    double direction = -std::atan2(x, y) * 180.0 / pi;
    if (direction < 0.0) {
        direction += 360.0;
    }

    tilt = static_cast<unsigned char>(std::lround(
        std::max(0.0, std::min(90.0, magnitude))));
    rotation = static_cast<unsigned short>(std::lround(direction)) % 360;
}

}  // namespace PlankPen
