#pragma once

#include <QPoint>
#include <QSize>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// Pure helpers for the Windows shared-screen viewer cursor overlay. Nothing
// here touches Win32 or SDL so it can be unit tested on any platform.
namespace PlankWin32CursorLogic
{

struct Image
{
    int width = 0;
    int height = 0;
    int hotspotX = 0;
    int hotspotY = 0;
    // Top-down rows, 4 bytes per pixel in memory order B, G, R, A with
    // premultiplied alpha: what UpdateLayeredWindow() expects for ULW_ALPHA.
    std::vector<std::uint8_t> bgra;
};

// PLANK_CURSOR_PIXEL_FORMAT_ARGB8888 is a little-endian 0xAARRGGBB word per
// pixel, so the bytes are B, G, R, A. The Windows host renders the cursor on
// black, so colour is already premultiplied (the Wayland surface consumes it
// as ARGB32_Premultiplied too). Only clamp colour to alpha so a malformed
// image can never produce an invalid premultiplied pixel.
inline bool imageFromWire(const unsigned char* argb, int width, int height,
                          int hotspotX, int hotspotY, Image& image)
{
    if (argb == nullptr || width <= 0 || height <= 0 ||
            hotspotX < 0 || hotspotY < 0 ||
            hotspotX >= width || hotspotY >= height) {
        return false;
    }

    const std::size_t size =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    image.width = width;
    image.height = height;
    image.hotspotX = hotspotX;
    image.hotspotY = hotspotY;
    image.bgra.assign(argb, argb + size);
    for (std::size_t i = 0; i < size; i += 4) {
        const std::uint8_t alpha = image.bgra[i + 3];
        for (std::size_t c = 0; c < 3; ++c) {
            if (image.bgra[i + c] > alpha) {
                image.bgra[i + c] = alpha;
            }
        }
    }
    return true;
}

// A plain black-outlined white arrow (hotspot at its tip) for a viewer that
// has a host position but no host shape yet.
inline Image fallbackArrow()
{
    static const char* const rows[] = {
        "B           ",
        "BB          ",
        "BWB         ",
        "BWWB        ",
        "BWWWB       ",
        "BWWWWB      ",
        "BWWWWWB     ",
        "BWWWWWWB    ",
        "BWWWWWWWB   ",
        "BWWWWWWWWB  ",
        "BWWWWWWWWWB ",
        "BWWWWWWBBBBB",
        "BWWWBWWB    ",
        "BWWBBWWB    ",
        "BWB  BWWB   ",
        "BB   BWWB   ",
        "B     BWWB  ",
        "      BWWB  ",
        "       BB   ",
    };
    constexpr int width = 12;
    constexpr int height = static_cast<int>(sizeof(rows) / sizeof(rows[0]));

    Image image;
    image.width = width;
    image.height = height;
    image.bgra.assign(static_cast<std::size_t>(width) * height * 4U, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const char cell = rows[y][x];
            if (cell == ' ') {
                continue;
            }
            const std::uint8_t value = cell == 'W' ? 255 : 0;
            std::uint8_t* pixel =
                    &image.bgra[(static_cast<std::size_t>(y) * width + x) * 4U];
            pixel[0] = value;
            pixel[1] = value;
            pixel[2] = value;
            pixel[3] = 255;
        }
    }
    return image;
}

// SDL reports window positions in window points; Win32 client coordinates
// can differ by the DPI scale. Scale through the two sizes of the same
// client area, which holds whatever DPI awareness the thread runs with.
inline bool clientPointFromWindowPoint(const QPoint& windowPoint,
                                       const QSize& windowSize,
                                       const QSize& clientSize,
                                       QPoint& clientPoint)
{
    if (windowSize.width() <= 0 || windowSize.height() <= 0 ||
            clientSize.width() <= 0 || clientSize.height() <= 0) {
        return false;
    }

    const double x = static_cast<double>(windowPoint.x()) *
            clientSize.width() / windowSize.width();
    const double y = static_cast<double>(windowPoint.y()) *
            clientSize.height() / windowSize.height();
    clientPoint = QPoint(static_cast<int>(std::lround(x)),
                         static_cast<int>(std::lround(y)));
    return true;
}

// The overlay window's top-left so the image's hotspot lands on the point.
inline QPoint overlayTopLeft(const QPoint& hotspotScreenPoint,
                             const Image& image)
{
    return QPoint(hotspotScreenPoint.x() - image.hotspotX,
                  hotspotScreenPoint.y() - image.hotspotY);
}

} // namespace PlankWin32CursorLogic
