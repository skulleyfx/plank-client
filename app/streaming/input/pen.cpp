/**
 * Pen capture through SDL, for clients with no libinput tablet support.
 *
 * The Linux client captures tablets directly from libinput, which gives it
 * tablet-absolute coordinates and every axis the hardware reports. Windows has
 * no such path, so a pen arrived as plain mouse movement and an artist lost
 * pressure entirely. SDL reports pen events on Windows from Windows Ink, which
 * carries pressure, tilt, rotation, distance, the eraser end and the barrel
 * buttons: everything the pen protocol can send.
 *
 * Coordinates arrive relative to the window, so they go through the same
 * mapping as an absolute mouse position, which accounts for letterboxing and
 * for a two-screen canvas. The protocol wants them normalized to the stream.
 */

#include "input.h"
#include "pentilt.h"
#include "streaming/plankpresentation.h"
#include "streaming/streamutils.h"

#include <Limelight.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN32
#include <windows.h>
#include <atomic>

namespace {

// Where Windows itself puts the pen, in client pixels of the window it is
// over. SDL computes a pen position of its own from the tablet's raw himetric
// coordinates and the tablet-to-screen mapping, and that drifts from where the
// cursor really is (down and right, more toward the edges) whenever the Wacom
// mapping, "force proportions" or display scaling do not match its sums. The
// mouse uses the pointer's pixel location and is exact, so the pen uses the
// same one. The hook runs on the event thread just before SDL turns the
// pointer message into pen events, so the stored point is the one those
// events belong to.
std::atomic<HWND> s_PenPixelWindow {nullptr};
std::atomic<long> s_PenPixelX {0};
std::atomic<long> s_PenPixelY {0};

// Pressure from the Wacom driver's own interface (WinTab, wintab32.dll).
// Windows Ink reports pen pressure in 1024 steps; at a light touch that leaves
// a few dozen levels, and Flame's pressure curve turns them into visible
// stairs. WinTab reports the tablet's full range (8192 levels on current
// Wacoms), as a tablet plugged into the host would. Only pressure is taken
// from it: position, contact and buttons still come from Windows Ink. No
// headers or import library are needed: the few definitions are below and
// the DLL is loaded at run time, so a machine without a Wacom driver simply
// keeps Windows Ink pressure.
namespace Wintab {
using HCTX = HANDLE;
using WTPKT = DWORD;
using FIX32 = DWORD;
constexpr UINT WTI_DEFCONTEXT = 3;
constexpr UINT WTI_DEVICES = 100;
constexpr UINT DVC_NPRESSURE = 15;
constexpr UINT CXO_SYSTEM = 0x0001;
constexpr UINT CXO_MESSAGES = 0x0004;
constexpr WTPKT PK_STATUS = 0x0002;
constexpr WTPKT PK_TIME = 0x0004;
constexpr WTPKT PK_X = 0x0080;
constexpr WTPKT PK_Y = 0x0100;
constexpr WTPKT PK_NORMAL_PRESSURE = 0x0400;
constexpr UINT TPS_PROXIMITY = 0x0001;
constexpr UINT WT_DEFBASE = 0x7FF0;
constexpr UINT WT_PACKET = WT_DEFBASE + 0;

struct AXIS {
    LONG axMin;
    LONG axMax;
    UINT axUnits;
    FIX32 axResolution;
};

struct LOGCONTEXTW {
    WCHAR lcName[40];
    UINT lcOptions;
    UINT lcStatus;
    UINT lcLocks;
    UINT lcMsgBase;
    UINT lcDevice;
    UINT lcPktRate;
    WTPKT lcPktData;
    WTPKT lcPktMode;
    WTPKT lcMoveMask;
    DWORD lcBtnDnMask;
    DWORD lcBtnUpMask;
    LONG lcInOrgX;
    LONG lcInOrgY;
    LONG lcInOrgZ;
    LONG lcInExtX;
    LONG lcInExtY;
    LONG lcInExtZ;
    LONG lcOutOrgX;
    LONG lcOutOrgY;
    LONG lcOutOrgZ;
    LONG lcOutExtX;
    LONG lcOutExtY;
    LONG lcOutExtZ;
    FIX32 lcSensX;
    FIX32 lcSensY;
    FIX32 lcSensZ;
    BOOL lcSysMode;
    int lcSysOrgX;
    int lcSysOrgY;
    int lcSysExtX;
    int lcSysExtY;
    FIX32 lcSysSensX;
    FIX32 lcSysSensY;
};

// Field order follows the PK_ bits, lowest first.
struct PACKET {
    UINT pkStatus;
    DWORD pkTime;
    UINT pkNormalPressure;
};

using WTInfoWFn = UINT (WINAPI*)(UINT, UINT, LPVOID);
using WTOpenWFn = HCTX (WINAPI*)(HWND, LOGCONTEXTW*, BOOL);
using WTCloseFn = BOOL (WINAPI*)(HCTX);
using WTPacketFn = BOOL (WINAPI*)(HCTX, UINT, LPVOID);
using WTOverlapFn = BOOL (WINAPI*)(HCTX, BOOL);
using WTQueueSizeSetFn = BOOL (WINAPI*)(HCTX, int);

HMODULE s_Module = nullptr;
WTInfoWFn s_Info = nullptr;
WTOpenWFn s_Open = nullptr;
WTCloseFn s_Close = nullptr;
WTPacketFn s_Packet = nullptr;
WTOverlapFn s_Overlap = nullptr;
WTQueueSizeSetFn s_QueueSizeSet = nullptr;
HCTX s_Context = nullptr;
HWND s_ContextWindow = nullptr;
float s_PressureMin = 0.0f;
float s_PressureRange = 0.0f;

// Latest WinTab pressure (0..1), written and read on the event thread. It
// stays valid from the first packet until the pen leaves the tablet or the
// window, so a stroke never switches between WinTab's fine steps and Windows
// Ink's coarse ones part way through.
float s_Pressure = 0.0f;
bool s_PressureValid = false;
bool s_LoggedFirstPacket = false;

void close()
{
    if (s_Context != nullptr && s_Close != nullptr) {
        s_Close(s_Context);
    }
    s_Context = nullptr;
    s_ContextWindow = nullptr;
    s_PressureValid = false;
}

void open(HWND hwnd)
{
    if (hwnd == nullptr || hwnd == s_ContextWindow) {
        return;
    }
    close();
    if (s_Module == nullptr) {
        s_Module = LoadLibraryExW(L"wintab32.dll", nullptr,
                                  LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (s_Module == nullptr) {
            SDL_LogInfo(SDL_LOG_CATEGORY_INPUT,
                        "PLANK pen: no Wacom WinTab driver; pressure comes from Windows Ink");
            return;
        }
        s_Info = reinterpret_cast<WTInfoWFn>(GetProcAddress(s_Module, "WTInfoW"));
        s_Open = reinterpret_cast<WTOpenWFn>(GetProcAddress(s_Module, "WTOpenW"));
        s_Close = reinterpret_cast<WTCloseFn>(GetProcAddress(s_Module, "WTClose"));
        s_Packet = reinterpret_cast<WTPacketFn>(GetProcAddress(s_Module, "WTPacket"));
        s_Overlap = reinterpret_cast<WTOverlapFn>(GetProcAddress(s_Module, "WTOverlap"));
        s_QueueSizeSet = reinterpret_cast<WTQueueSizeSetFn>(
                    GetProcAddress(s_Module, "WTQueueSizeSet"));
    }
    if (s_Info == nullptr || s_Open == nullptr || s_Close == nullptr ||
            s_Packet == nullptr) {
        return;
    }
    // WTInfo with a null buffer returns the size it would write: zero means
    // no tablet service is running.
    if (s_Info(0, 0, nullptr) == 0) {
        SDL_LogInfo(SDL_LOG_CATEGORY_INPUT,
                    "PLANK pen: WinTab has no tablet service; pressure comes from Windows Ink");
        return;
    }
    AXIS pressure = {};
    if (s_Info(WTI_DEVICES, DVC_NPRESSURE, &pressure) == 0 ||
            pressure.axMax <= pressure.axMin) {
        return;
    }
    LOGCONTEXTW context = {};
    if (s_Info(WTI_DEFCONTEXT, 0, &context) == 0) {
        return;
    }
    // A digitizing context that only reports packets to this window: it does
    // not move the cursor (Windows Ink keeps doing that).
    wcsncpy_s(context.lcName, L"PLANK pen pressure", _TRUNCATE);
    context.lcOptions = (context.lcOptions | CXO_MESSAGES) & ~CXO_SYSTEM;
    context.lcPktData = PK_STATUS | PK_TIME | PK_NORMAL_PRESSURE;
    context.lcPktMode = 0;
    // Motion as well as pressure changes produce packets, so a stroke at
    // steady pressure keeps delivering them.
    context.lcMoveMask = PK_X | PK_Y | PK_NORMAL_PRESSURE;
    context.lcMsgBase = WT_DEFBASE;
    s_Context = s_Open(hwnd, &context, TRUE);
    if (s_Context == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT,
                    "PLANK pen: unable to open a WinTab context; pressure comes from Windows Ink");
        return;
    }
    s_ContextWindow = hwnd;
    // The default queue holds about 40 ms of packets; a busy event thread
    // would lose some.
    if (s_QueueSizeSet != nullptr) {
        s_QueueSizeSet(s_Context, 128);
    }
    s_PressureMin = static_cast<float>(pressure.axMin);
    s_PressureRange = static_cast<float>(pressure.axMax - pressure.axMin);
    s_LoggedFirstPacket = false;
    SDL_LogInfo(SDL_LOG_CATEGORY_INPUT,
                "PLANK pen: Wacom WinTab pressure enabled (%ld levels)",
                pressure.axMax + 1);
}

void handleMessage(const MSG* msg)
{
    if (s_Context == nullptr) {
        return;
    }
    if (msg->message == WT_PACKET &&
            reinterpret_cast<HCTX>(msg->lParam) == s_Context) {
        PACKET packet = {};
        if (s_Packet(s_Context, static_cast<UINT>(msg->wParam), &packet)) {
            if (packet.pkStatus & TPS_PROXIMITY) {
                // The pen has left the tablet.
                s_PressureValid = false;
                return;
            }
            s_Pressure = std::clamp(
                        (static_cast<float>(packet.pkNormalPressure) - s_PressureMin) /
                            s_PressureRange,
                        0.0f, 1.0f);
            s_PressureValid = true;
            if (!s_LoggedFirstPacket) {
                s_LoggedFirstPacket = true;
                SDL_LogInfo(SDL_LOG_CATEGORY_INPUT,
                            "PLANK pen: first WinTab packet received");
            }
        }
    }
    else if (msg->message == WM_POINTERENTER && s_Overlap != nullptr) {
        // Put this context on top whenever a pen comes over a PLANK window,
        // so the driver delivers to it even after another tablet application
        // was in front. (WM_ACTIVATE is sent, not posted, so the message hook
        // never sees it.)
        s_Overlap(s_Context, TRUE);
    }
    else if (msg->message == WM_POINTERLEAVE) {
        s_PressureValid = false;
    }
}

// The WinTab pressure for an event happening now, or -1 when there is none.
float freshPressure()
{
    if (s_Context == nullptr || !s_PressureValid) {
        return -1.0f;
    }
    return s_Pressure;
}
} // namespace Wintab

bool SDLCALL plankPenMessageHook(void*, MSG* msg)
{
    Wintab::handleMessage(msg);
    switch (msg->message) {
    case WM_POINTERENTER:
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP: {
        const UINT32 pointerId = GET_POINTERID_WPARAM(msg->wParam);
        POINTER_INPUT_TYPE pointerType = PT_POINTER;
        POINTER_INFO info;
        if (GetPointerType(pointerId, &pointerType) && pointerType == PT_PEN &&
                GetPointerInfo(pointerId, &info)) {
            POINT position = info.ptPixelLocation;
            if (ScreenToClient(msg->hwnd, &position)) {
                s_PenPixelX.store(position.x);
                s_PenPixelY.store(position.y);
                s_PenPixelWindow.store(msg->hwnd);
            }
        }
        break;
    }
    case WM_POINTERLEAVE:
        s_PenPixelWindow.store(nullptr);
        break;
    default:
        break;
    }
    return true; // SDL still handles every message
}

// One pump can turn several pointer messages into several queued pen events
// before the first of them is handled, so the latest position is not the
// right one for an older event. An event watch runs inside SDL_PushEvent,
// during the same DispatchMessage that follows the hook, and records the
// position for that event's timestamp; the handler looks it up later.
struct PenPixelSample {
    Uint64 timestamp;
    HWND hwnd;
    long x;
    long y;
    float wintabPressure; // -1 when WinTab had none
};
constexpr int PenPixelSampleCount = 256;
PenPixelSample s_PenPixelSamples[PenPixelSampleCount] = {};
int s_PenPixelNext = 0;

bool SDLCALL plankPenEventWatch(void*, SDL_Event* event)
{
    switch (event->type) {
    case SDL_EVENT_PEN_DOWN:
    case SDL_EVENT_PEN_UP:
    case SDL_EVENT_PEN_MOTION:
    case SDL_EVENT_PEN_BUTTON_DOWN:
    case SDL_EVENT_PEN_BUTTON_UP: {
        HWND hwnd = s_PenPixelWindow.load();
        if (hwnd == nullptr) {
            break;
        }
        PenPixelSample& sample = s_PenPixelSamples[s_PenPixelNext];
        sample.timestamp = event->common.timestamp;
        sample.hwnd = hwnd;
        sample.x = s_PenPixelX.load();
        sample.y = s_PenPixelY.load();
        sample.wintabPressure = Wintab::freshPressure();
        s_PenPixelNext = (s_PenPixelNext + 1) % PenPixelSampleCount;
        break;
    }
    default:
        break;
    }
    return true;
}

} // namespace

void SdlInputHandler::installPenPositionHook()
{
    SDL_SetWindowsMessageHook(plankPenMessageHook, nullptr);
    SDL_AddEventWatch(plankPenEventWatch, nullptr);
}

void SdlInputHandler::removePenPositionHook()
{
    SDL_SetWindowsMessageHook(nullptr, nullptr);
    SDL_RemoveEventWatch(plankPenEventWatch, nullptr);
    s_PenPixelWindow.store(nullptr);
    Wintab::close();
}

void SdlInputHandler::attachPenPressureSource(SDL_Window* window)
{
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    HWND hwnd = properties != 0 ?
                reinterpret_cast<HWND>(SDL_GetPointerProperty(
                    properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)) :
                nullptr;
    Wintab::open(hwnd);
}

static bool windowsPenPixelPosition(SDL_Window* window, Uint64 timestamp,
                                    float& x, float& y, float& wintabPressure)
{
    wintabPressure = -1.0f;
    if (timestamp == 0) {
        return false;
    }
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    HWND hwnd = properties != 0 ?
                reinterpret_cast<HWND>(SDL_GetPointerProperty(
                    properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)) :
                nullptr;
    if (hwnd == nullptr) {
        return false;
    }
    // SDL can replace a window's native handle (a renderer fallback, for
    // one); follow it so WinTab packets keep arriving.
    Wintab::open(hwnd);
    // Newest first: the sample for an event is recent unless the queue is
    // very far behind, and then SDL's own position is the fallback.
    for (int i = 1; i <= PenPixelSampleCount; i++) {
        const PenPixelSample& sample = s_PenPixelSamples[
                (s_PenPixelNext - i + PenPixelSampleCount) % PenPixelSampleCount];
        if (sample.timestamp == timestamp) {
            if (sample.hwnd != hwnd) {
                return false;
            }
            x = static_cast<float>(sample.x);
            y = static_cast<float>(sample.y);
            wintabPressure = sample.wintabPressure;
            return true;
        }
    }
    return false;
}
#else
void SdlInputHandler::installPenPositionHook() {}
void SdlInputHandler::removePenPositionHook() {}
void SdlInputHandler::attachPenPressureSource(SDL_Window*) {}
#endif

void SdlInputHandler::resetPenState()
{
    m_PenTipDown = false;
    m_PenButtons = 0;
    m_PenPressure = 0.0f;
    m_PenDistance = 0.0f;
    m_PenRotation = 0;
    m_PenTilt = 0;
    m_PenTiltX = 0.0f;
    m_PenTiltY = 0.0f;
    m_PenEraser = false;
    // Whether this tablet reports pressure at all is a property of the
    // device, so it is not forgotten when a pen leaves proximity.
}

bool SdlInputHandler::isPenCaptureAvailable() const
{
    if (m_ViewOnly || m_ViewerSession) {
        // A viewer's pen never reaches the host as a pen, even with control;
        // it moves the pointer like a mouse instead.
        return false;
    }
#ifdef HAVE_LIBINPUT_TABLET
    // libinput owns the tablet on this client; two capture paths would send
    // every stroke twice.
    return false;
#else
    return (LiGetHostFeatureFlags() & LI_FF_PEN_TOUCH_EVENTS) != 0;
#endif
}

/**
 * @brief Send the pen's current state to the host.
 *
 * @param eventType Pen event type from the protocol.
 * @param window Window the pen is over.
 * @param windowX Pen position in window coordinates.
 * @param windowY Pen position in window coordinates.
 * @param timestamp The SDL event's timestamp, which finds the pixel position
 *        Windows reported for it (0 when there is no position).
 * @return Whether the event was sent.
 */
bool SdlInputHandler::sendPenEvent(unsigned char eventType, SDL_Window* window,
                                   float windowX, float windowY,
                                   Uint64 timestamp)
{
    if (!isPenCaptureAvailable()) {
        return false;
    }

    const QSize streamSize = streamDimensions();
    if (streamSize.isEmpty()) {
        return false;
    }

    float normalizedX = 0.0f;
    float normalizedY = 0.0f;
    if (eventType != LI_TOUCH_EVENT_HOVER_LEAVE &&
            eventType != LI_TOUCH_EVENT_CANCEL_ALL) {
        const auto* output = presentationOutput(window);
        if (output == nullptr) {
            penLeftPicture();
            return false;
        }
        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        if (windowWidth <= 0 || windowHeight <= 0) {
            return false;
        }

#ifdef Q_OS_WIN32
        float pixelX = windowX;
        float pixelY = windowY;
        float wintabPressure = -1.0f;
        if (windowsPenPixelPosition(window, timestamp, pixelX, pixelY,
                                    wintabPressure)) {
            m_PenSdlOffsetX = windowX - pixelX;
            m_PenSdlOffsetY = windowY - pixelY;
            windowX = pixelX;
            windowY = pixelY;
        }
        m_PenWintabPressure = wintabPressure;
#else
        (void)timestamp;
#endif

        QPointF streamPoint;
        if (!PlankPresentation::mapWindowPointToStream(
                    QPointF(windowX, windowY),
                    QSize(windowWidth, windowHeight), streamSize,
                    m_PresentationLayout.canvasSize, output->canvasRect,
                    streamPoint, false)) {
            // The pen is outside the picture; nothing to draw on.
            penLeftPicture();
            return false;
        }
        normalizedX = static_cast<float>(streamPoint.x() / streamSize.width());
        normalizedY = static_cast<float>(streamPoint.y() / streamSize.height());
    }

    // Pressure while drawing, distance while hovering, exactly as the
    // libinput path reports it. A tablet that reports contact but no pressure
    // axis at all would otherwise draw every stroke at zero pressure, which
    // is indistinguishable from not drawing: treat its contact as full
    // pressure instead, so the pen works while the driver stays quiet about
    // how hard it is pressed.
    float contactPressure = m_PenPressureSeen ? m_PenPressure : 1.0f;
#ifdef Q_OS_WIN32
    // The Wacom driver's full-resolution pressure, when it has one for this
    // moment (see Wintab above); contact itself is still Windows Ink's call.
    if (m_PenWintabPressure >= 0.0f) {
        contactPressure = m_PenWintabPressure;
    }
#endif
    const float pressureOrDistance = m_PenTipDown ? contactPressure : m_PenDistance;

    // One line per second while a pen is in use. Without it a pen that draws
    // nothing is indistinguishable from a pen the client never saw, and a
    // tablet is the one input nobody can reproduce from a log of its own.
    const Uint64 now = SDL_GetTicks();
    if (now - m_PenLastLogTime >= 1000) {
        m_PenLastLogTime = now;
        SDL_LogInfo(SDL_LOG_CATEGORY_INPUT,
                    "PLANK pen: event=%u tip=%s pressure=%.3f%s distance=%.3f "
                    "tilt=%u rotation=%u buttons=0x%x eraser=%s at %.3f,%.3f "
                    "(SDL position was %+.0f,%+.0f px off)%s",
                    eventType, m_PenTipDown ? "down" : "up",
                    pressureOrDistance,
                    m_PenPressureSeen ? "" : " (no pressure axis reported)",
                    m_PenDistance,
                    static_cast<unsigned>(m_PenTilt),
                    static_cast<unsigned>(m_PenRotation),
                    m_PenButtons, m_PenEraser ? "yes" : "no",
                    normalizedX, normalizedY,
                    m_PenSdlOffsetX, m_PenSdlOffsetY,
                    m_PenWintabPressure >= 0.0f ? " [WinTab pressure]" : "");
    }
    const unsigned char toolType = m_PenEraser ? LI_TOOL_TYPE_ERASER : LI_TOOL_TYPE_PEN;

    if (eventType != LI_TOUCH_EVENT_HOVER_LEAVE &&
            eventType != LI_TOUCH_EVENT_CANCEL_ALL) {
        activateWin32PenCursor();
    }

    return LiSendPenEvent(eventType, toolType, m_PenButtons,
                          std::clamp(normalizedX, 0.0f, 1.0f),
                          std::clamp(normalizedY, 0.0f, 1.0f),
                          std::clamp(pressureOrDistance, 0.0f, 1.0f),
                          0.0f, 0.0f, m_PenRotation, m_PenTilt) == 0;
}

void SdlInputHandler::handlePenProximityEvent(SDL_PenProximityEvent* event)
{
    if (!isPenCaptureAvailable()) {
        return;
    }
    SDL_Window* window = presentationWindow(event->windowID);
    if (window == nullptr) {
        return;
    }

    if (event->type == SDL_EVENT_PEN_PROXIMITY_OUT) {
        sendPenEvent(LI_TOUCH_EVENT_HOVER_LEAVE, window, 0.0f, 0.0f);
        resetPenState();
    }
}

void SdlInputHandler::handlePenTouchEvent(SDL_PenTouchEvent* event)
{
    if (!isPenCaptureAvailable()) {
        return;
    }
    SDL_Window* window = presentationWindow(event->windowID);
    if (window == nullptr) {
        return;
    }

    m_PenEraser = event->eraser;
    m_PenTipDown = event->down || (event->pen_state & SDL_PEN_INPUT_DOWN) != 0;
    sendPenEvent(event->down ? LI_TOUCH_EVENT_DOWN : LI_TOUCH_EVENT_UP,
                 window, event->x, event->y, event->timestamp);
}

void SdlInputHandler::handlePenMotionEvent(SDL_PenMotionEvent* event)
{
    if (!isPenCaptureAvailable()) {
        return;
    }
    SDL_Window* window = presentationWindow(event->windowID);
    if (window == nullptr) {
        return;
    }

    // A pen reporting pressure is in contact even if the flag is absent.
    m_PenTipDown = (event->pen_state & SDL_PEN_INPUT_DOWN) != 0 ||
                   m_PenPressure > 0.0f;
    sendPenEvent(m_PenTipDown ? LI_TOUCH_EVENT_MOVE : LI_TOUCH_EVENT_HOVER,
                 window, event->x, event->y, event->timestamp);
}

void SdlInputHandler::setPenSideButtonsSwapped(bool swapped)
{
    if (m_PenSideButtonsSwapped != swapped) {
        SDL_LogInfo(SDL_LOG_CATEGORY_INPUT,
                    "Pen side buttons %s for this host",
                    swapped ? "swapped (first side button right-clicks)" : "as sent");
    }
    m_PenSideButtonsSwapped = swapped;
    m_PenButtons = 0;
}

void SdlInputHandler::handlePenButtonEvent(SDL_PenButtonEvent* event)
{
    if (!isPenCaptureAvailable()) {
        return;
    }
    SDL_Window* window = presentationWindow(event->windowID);
    if (window == nullptr) {
        return;
    }

    unsigned char mask = 0;
    switch (event->button) {
    case 1:
        mask = m_PenSideButtonsSwapped ? LI_PEN_BUTTON_SECONDARY : LI_PEN_BUTTON_PRIMARY;
        break;
    case 2:
        mask = m_PenSideButtonsSwapped ? LI_PEN_BUTTON_PRIMARY : LI_PEN_BUTTON_SECONDARY;
        break;
    case 3:
        mask = LI_PEN_BUTTON_TERTIARY;
        break;
    default:
        // The protocol carries three barrel buttons; anything further is
        // reported by the tablet's own driver on the host.
        return;
    }

    if (event->type == SDL_EVENT_PEN_BUTTON_DOWN) {
        m_PenButtons |= mask;
    }
    else {
        m_PenButtons &= static_cast<unsigned char>(~mask);
    }
    sendPenEvent(LI_TOUCH_EVENT_BUTTON_ONLY, window, event->x, event->y,
                 event->timestamp);
}

void SdlInputHandler::handlePenAxisEvent(SDL_PenAxisEvent* event)
{
    if (!isPenCaptureAvailable()) {
        return;
    }

    // Every pen event carries the full input state, so the tip is tracked
    // from all of them rather than from touch events alone. A tablet that
    // reports pressure without ever setting the down flag would otherwise
    // draw nothing: pressure is only sent while the tip is down, so the host
    // saw a pen hovering across the picture.
    m_PenTipDown = (event->pen_state & SDL_PEN_INPUT_DOWN) != 0 ||
                   (event->axis == SDL_PEN_AXIS_PRESSURE && event->value > 0.0f) ||
                   (m_PenTipDown && m_PenPressure > 0.0f);

    switch (event->axis) {
    case SDL_PEN_AXIS_PRESSURE:
        m_PenPressure = event->value;
        m_PenPressureSeen = true;
        break;
    case SDL_PEN_AXIS_DISTANCE:
        m_PenDistance = event->value;
        break;
    case SDL_PEN_AXIS_XTILT:
        m_PenTiltX = event->value;
        PlankPen::encodeTilt(m_PenTiltX, m_PenTiltY, m_PenRotation, m_PenTilt);
        break;
    case SDL_PEN_AXIS_YTILT:
        m_PenTiltY = event->value;
        PlankPen::encodeTilt(m_PenTiltX, m_PenTiltY, m_PenRotation, m_PenTilt);
        break;
    default:
        // Barrel rotation, slider and tangential pressure have no place in
        // the pen protocol's packet, so they are dropped rather than
        // approximated into a field that means something else.
        break;
    }

    // Axis changes are followed by motion events, which carry the position
    // this new value belongs with, so nothing is sent from here.
}
