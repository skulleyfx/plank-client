#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <memory>

// Draws the host's pointer for a Windows shared-screen viewer. In PLANK local
// cursor mode the pointer is not in the video, so a viewer needs its own
// copy: a click-through, never-activated layered popup owned by the stream
// window, kept above the video and moved so the hotspot sits on the host's
// position. Create, update and destroy it on the thread that owns the SDL
// windows (the main thread).
class PlankWin32Cursor
{
public:
    // Returns false when the host pointer must not be drawn. Otherwise sets
    // the presentation window and the hotspot position in its window points
    // (the units SDL_GetWindowSize() reports).
    using Locator = std::function<bool(SDL_Window*& window, int& x, int& y)>;

    static std::unique_ptr<PlankWin32Cursor> create(Locator locator);
    ~PlankWin32Cursor();

    PlankWin32Cursor(const PlankWin32Cursor&) = delete;
    PlankWin32Cursor& operator=(const PlankWin32Cursor&) = delete;

    // PLANK_CURSOR_PIXEL_FORMAT_ARGB8888 pixels, tightly packed.
    void setImage(const unsigned char* argb, int width, int height,
                  int hotspotX, int hotspotY);

    // Show, move or hide the overlay from the locator's current answer.
    void update();

private:
    class Impl;

    explicit PlankWin32Cursor(std::unique_ptr<Impl> impl);

    static bool SDLCALL handleSdlEvent(void* userdata, SDL_Event* event);

    std::unique_ptr<Impl> m_Impl;
};
