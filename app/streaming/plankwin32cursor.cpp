#include "plankwin32cursor.h"
#include "plankwin32cursorlogic.h"

#include <windows.h>

#include <cstring>
#include <utility>

namespace {

const wchar_t ViewerCursorWindowClass[] = L"PlankViewerCursor";

LRESULT CALLBACK viewerCursorWindowProc(HWND hwnd, UINT message,
                                        WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_NCHITTEST:
        // Never take the viewer's own clicks (toolbar, letterbox).
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

bool registerViewerCursorWindowClass()
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = viewerCursorWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = ViewerCursorWindowClass;
    return RegisterClassExW(&windowClass) != 0 ||
            GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND hwndForWindow(SDL_Window* window)
{
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    return properties != 0 ?
                reinterpret_cast<HWND>(SDL_GetPointerProperty(
                    properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)) :
                nullptr;
}

} // namespace

class PlankWin32Cursor::Impl
{
public:
    explicit Impl(Locator locator)
        : m_Locator(std::move(locator)),
          m_ThreadId(GetCurrentThreadId()),
          m_Image(PlankWin32CursorLogic::fallbackArrow())
    {
    }

    ~Impl()
    {
        destroyWindow();
    }

    bool onOwnerThread() const
    {
        return GetCurrentThreadId() == m_ThreadId;
    }

    void setImage(PlankWin32CursorLogic::Image image)
    {
        m_Image = std::move(image);
        m_ContentDirty = true;
    }

    void update()
    {
        if (m_Updating || !onOwnerThread()) {
            return;
        }
        m_Updating = true;
        if (!place()) {
            hide();
        }
        m_Updating = false;
    }

private:
    bool place()
    {
        SDL_Window* window = nullptr;
        int x = 0;
        int y = 0;
        if (!m_Locator || !m_Locator(window, x, y) || window == nullptr) {
            return false;
        }

        HWND owner = hwndForWindow(window);
        if (owner == nullptr || !IsWindowVisible(owner) || IsIconic(owner)) {
            return false;
        }
        if (!ensureWindow(owner)) {
            return false;
        }

        int windowWidth = 0;
        int windowHeight = 0;
        RECT client = {};
        if (!SDL_GetWindowSize(window, &windowWidth, &windowHeight) ||
                !GetClientRect(owner, &client)) {
            return false;
        }
        QPoint clientPoint;
        if (!PlankWin32CursorLogic::clientPointFromWindowPoint(
                    QPoint(x, y),
                    QSize(windowWidth, windowHeight),
                    QSize(client.right - client.left, client.bottom - client.top),
                    clientPoint)) {
            return false;
        }
        POINT screenPoint = { clientPoint.x(), clientPoint.y() };
        if (!ClientToScreen(owner, &screenPoint)) {
            return false;
        }
        const QPoint topLeft = PlankWin32CursorLogic::overlayTopLeft(
                    QPoint(screenPoint.x, screenPoint.y), m_Image);

        if (m_ContentDirty) {
            if (!paint(topLeft)) {
                return false;
            }
            m_ContentDirty = false;
        }
        else if (topLeft != m_TopLeft) {
            SetWindowPos(m_Hwnd, nullptr, topLeft.x(), topLeft.y(), 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                         SWP_NOACTIVATE);
        }
        m_TopLeft = topLeft;

        if (!m_Shown) {
            ShowWindow(m_Hwnd, SW_SHOWNOACTIVATE);
            m_Shown = true;
        }
        return true;
    }

    bool ensureWindow(HWND owner)
    {
        if (m_Hwnd != nullptr && owner == m_Owner && IsWindow(m_Hwnd)) {
            return true;
        }
        destroyWindow();

        if (!registerViewerCursorWindowClass()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Unable to register PLANK viewer cursor window class: %lu",
                        GetLastError());
            return false;
        }

        // Owned rather than WS_EX_TOPMOST: Windows keeps an owned window above
        // its owner (and makes it topmost whenever the owner is), so it covers
        // the video in windowed and fullscreen modes without floating over the
        // viewer's other applications.
        m_Hwnd = CreateWindowExW(
                    WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
                    WS_EX_NOACTIVATE,
                    ViewerCursorWindowClass, L"", WS_POPUP,
                    0, 0, 1, 1, owner, nullptr,
                    GetModuleHandleW(nullptr), nullptr);
        if (m_Hwnd == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Unable to create PLANK viewer cursor window: %lu",
                        GetLastError());
            return false;
        }
        m_Owner = owner;
        m_ContentDirty = true;
        m_Shown = false;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "PLANK viewer host-cursor overlay enabled");
        return true;
    }

    bool paint(const QPoint& topLeft)
    {
        const int width = m_Image.width;
        const int height = m_Image.height;
        if (width <= 0 || height <= 0 ||
                m_Image.bgra.size() != static_cast<size_t>(width) * height * 4U) {
            return false;
        }

        HDC screenDc = GetDC(nullptr);
        if (screenDc == nullptr) {
            return false;
        }
        HDC memoryDc = CreateCompatibleDC(screenDc);
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height; // top-down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bitmap = memoryDc != nullptr ?
                    CreateDIBSection(memoryDc, &info, DIB_RGB_COLORS,
                                     &bits, nullptr, 0) :
                    nullptr;
        bool painted = false;
        if (bitmap != nullptr && bits != nullptr) {
            std::memcpy(bits, m_Image.bgra.data(), m_Image.bgra.size());
            HGDIOBJ previous = SelectObject(memoryDc, bitmap);
            POINT destination = { topLeft.x(), topLeft.y() };
            SIZE size = { width, height };
            POINT source = { 0, 0 };
            BLENDFUNCTION blend = {};
            blend.BlendOp = AC_SRC_OVER;
            blend.SourceConstantAlpha = 255;
            blend.AlphaFormat = AC_SRC_ALPHA;
            painted = UpdateLayeredWindow(m_Hwnd, screenDc, &destination, &size,
                                          memoryDc, &source, 0, &blend,
                                          ULW_ALPHA) != FALSE;
            SelectObject(memoryDc, previous);
        }
        if (!painted) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Unable to paint PLANK viewer cursor: %lu",
                        GetLastError());
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memoryDc != nullptr) {
            DeleteDC(memoryDc);
        }
        ReleaseDC(nullptr, screenDc);
        return painted;
    }

    void hide()
    {
        if (m_Shown && m_Hwnd != nullptr && IsWindow(m_Hwnd)) {
            ShowWindow(m_Hwnd, SW_HIDE);
        }
        m_Shown = false;
    }

    void destroyWindow()
    {
        // Windows destroys an owned window with its owner, so the handle may
        // already be gone when the stream window went first.
        if (m_Hwnd != nullptr && IsWindow(m_Hwnd)) {
            DestroyWindow(m_Hwnd);
        }
        m_Hwnd = nullptr;
        m_Owner = nullptr;
        m_Shown = false;
    }

    Locator m_Locator;
    DWORD m_ThreadId;
    PlankWin32CursorLogic::Image m_Image;
    HWND m_Owner = nullptr;
    HWND m_Hwnd = nullptr;
    QPoint m_TopLeft;
    bool m_ContentDirty = true;
    bool m_Shown = false;
    bool m_Updating = false;
};

std::unique_ptr<PlankWin32Cursor> PlankWin32Cursor::create(Locator locator)
{
    std::unique_ptr<PlankWin32Cursor> cursor(new PlankWin32Cursor(
            std::make_unique<Impl>(std::move(locator))));
    // Follow moves, resizes, minimise and fullscreen changes of the stream
    // window even while the host pointer stands still.
    if (!SDL_AddEventWatch(PlankWin32Cursor::handleSdlEvent, cursor.get())) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Unable to watch window events for the PLANK viewer cursor: %s",
                    SDL_GetError());
    }
    return cursor;
}

PlankWin32Cursor::PlankWin32Cursor(std::unique_ptr<Impl> impl)
    : m_Impl(std::move(impl))
{
}

PlankWin32Cursor::~PlankWin32Cursor()
{
    SDL_RemoveEventWatch(PlankWin32Cursor::handleSdlEvent, this);
}

void PlankWin32Cursor::setImage(const unsigned char* argb, int width, int height,
                                int hotspotX, int hotspotY)
{
    PlankWin32CursorLogic::Image image;
    if (PlankWin32CursorLogic::imageFromWire(argb, width, height,
                                             hotspotX, hotspotY, image)) {
        m_Impl->setImage(std::move(image));
    }
}

void PlankWin32Cursor::update()
{
    m_Impl->update();
}

bool SDLCALL PlankWin32Cursor::handleSdlEvent(void* userdata, SDL_Event* event)
{
    switch (event->type) {
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        // Window events come from the main thread's message pump; update()
        // ignores anything delivered on another thread.
        static_cast<PlankWin32Cursor*>(userdata)->update();
        break;
    default:
        break;
    }
    // Event watches cannot drop events; the return value is ignored.
    return true;
}
