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
std::atomic<float> s_PenPixelX {0.0f};
std::atomic<float> s_PenPixelY {0.0f};
// Sub-pixel position: the HIMETRIC location mapped to pixels carries the
// fraction ptPixelLocation lacks, but for a pen it can sit a pixel or two
// off (the Wacom driver's own mapping), and the error depends on where on
// the screen the pen is, not on time. The difference to the whole-pixel
// position is learned over the session, weighted by distance moved so that a
// slow stroke inside one pixel does not teach it that pixel's rounding, and
// is taken off the mapped point; the result never strays more than a pixel
// from the whole-pixel position. It starts again only when the tablet or
// display mapping changes. Only the event thread touches these.
bool s_PenOffsetValid = false;
double s_PenOffsetX = 0.0;
double s_PenOffsetY = 0.0;
double s_PenLastMappedX = 0.0;
double s_PenLastMappedY = 0.0;
RECT s_PenDeviceRect = {};
RECT s_PenDisplayRect = {};
// Size of the raw mapping error, for the pen log line.
std::atomic<float> s_PenMapErrorMax {0.0f};
std::atomic<float> s_PenMapErrorLast {0.0f};

// The pen through the Wacom driver's own interface (WinTab, wintab32.dll).
// Windows Ink reports pressure in 1024 steps, which leaves a few dozen levels
// at a light touch (Flame's pressure curve shows them as stairs), and it only
// reports contact once the pen passes the driver's click threshold, so the
// light start of a stroke is lost. WinTab reports the tablet's full range
// (8192 levels on current Wacoms) from the first trace of pressure, as a
// tablet plugged into the host would.
//
// The context is a *system* context: the driver keeps moving the cursor for
// it. (Client .56 opened a digitizing context; being on top, it took the
// whole tablet away from the system context, and the pen stopped working.)
// While Windows Ink keeps delivering pen messages, Ink still gives position,
// contact and buttons and WinTab only pressure. If the driver hands the pen to
// WinTab instead and Ink falls silent, WinTab drives the pen on its own: the
// cursor gives the position, the packets give pressure, contact, eraser and
// buttons. No headers or import library are needed: the few definitions are
// below and the DLL is loaded at run time; without a Wacom driver, or with
// PLANK_WINTAB=0, the pen is Windows Ink's alone.
namespace Wintab {
using HCTX = HANDLE;
using WTPKT = DWORD;
using FIX32 = DWORD;
constexpr UINT WTI_DEFSYSCTX = 4;
constexpr UINT WTI_DEVICES = 100;
constexpr UINT DVC_NPRESSURE = 15;
constexpr UINT CXO_MESSAGES = 0x0004;
constexpr WTPKT PK_STATUS = 0x0002;
constexpr WTPKT PK_TIME = 0x0004;
constexpr WTPKT PK_CURSOR = 0x0020;
constexpr WTPKT PK_BUTTONS = 0x0040;
constexpr WTPKT PK_NORMAL_PRESSURE = 0x0400;
constexpr UINT TPS_PROXIMITY = 0x0001;
constexpr UINT WT_DEFBASE = 0x7FF0;
constexpr UINT WT_PACKET = WT_DEFBASE + 0;
constexpr UINT WT_PROXIMITY = WT_DEFBASE + 5;

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
    UINT pkCursor;
    DWORD pkButtons;
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

// Everything below is written and read on the event thread only.

// Latest pressure (0..1). It stays valid from the first packet until the pen
// leaves the tablet or the window, so a stroke never switches between
// WinTab's fine steps and Ink's coarse ones part way through.
float s_Pressure = 0.0f;
bool s_PressureValid = false;

// Who drives the pen. Ink pen messages seen within InkQuietMs mean Ink does;
// WinTab drives only once it has delivered packets for DriveAfterMs with Ink
// silent, so the first packet of a touch never races Ink's first message.
constexpr Uint64 InkQuietMs = 250;
constexpr Uint64 DriveAfterMs = 120;
Uint64 s_LastInkTicks = 0;
Uint64 s_StreakStartTicks = 0;
Uint64 s_LastPacketTicks = 0;
bool s_Driving = false;
bool s_InProximity = false;
// Set when WinTab takes over with the tip already down: that stroke began
// somewhere else (another application, or under Ink), so it is not carried
// on; the pen counts as hovering until it lifts.
bool s_WaitForLift = false;
bool s_LoggedFirstPacket = false;
// No packet for this long: the pen has gone, whether or not the driver said
// so (another WinTab application on top, a fast exit).
constexpr Uint64 PacketQuietMs = 250;

SdlInputHandler* s_Handler = nullptr;

void close()
{
    if (s_Context != nullptr && s_Close != nullptr) {
        s_Close(s_Context);
    }
    s_Context = nullptr;
    s_ContextWindow = nullptr;
    s_PressureValid = false;
    s_Driving = false;
    s_InProximity = false;
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
                        "PLANK pen: no Wacom WinTab driver; the pen comes from Windows Ink");
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
                    "PLANK pen: WinTab has no tablet service; the pen comes from Windows Ink");
        return;
    }
    AXIS pressure = {};
    if (s_Info(WTI_DEVICES, DVC_NPRESSURE, &pressure) == 0 ||
            pressure.axMax <= pressure.axMin) {
        return;
    }
    LOGCONTEXTW context = {};
    if (s_Info(WTI_DEFSYSCTX, 0, &context) == 0) {
        return;
    }
    // The default system context, unchanged except that it also reports
    // packets to this window: the driver keeps its own mapping and keeps
    // moving the cursor.
    wcsncpy_s(context.lcName, L"PLANK pen", _TRUNCATE);
    context.lcOptions |= CXO_MESSAGES;
    context.lcPktData = PK_STATUS | PK_TIME | PK_CURSOR | PK_BUTTONS | PK_NORMAL_PRESSURE;
    context.lcPktMode = 0;
    context.lcMoveMask |= PK_BUTTONS | PK_NORMAL_PRESSURE;
    context.lcMsgBase = WT_DEFBASE;
    s_Context = s_Open(hwnd, &context, TRUE);
    if (s_Context == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT,
                    "PLANK pen: unable to open a WinTab context; the pen comes from Windows Ink");
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
                "PLANK pen: Wacom WinTab system context open (%ld pressure levels)",
                pressure.axMax - pressure.axMin + 1);
}

void stopDriving()
{
    if (!s_Driving) {
        return;
    }
    s_Driving = false;
    if (s_Handler != nullptr) {
        // Lift the pen on the host and let it go; whoever drives next starts
        // a fresh stroke.
        s_Handler->handleWintabPen(true, 0.0f, 0, false);
    }
}

void handlePacket(const PACKET& packet)
{
    const Uint64 now = SDL_GetTicks();
    s_LastPacketTicks = now;
    const bool outOfProximity = (packet.pkStatus & TPS_PROXIMITY) != 0;
    s_InProximity = !outOfProximity;
    if (outOfProximity) {
        s_PressureValid = false;
    }
    else {
        s_Pressure = std::clamp(
                    (static_cast<float>(packet.pkNormalPressure) - s_PressureMin) /
                        s_PressureRange,
                    0.0f, 1.0f);
        s_PressureValid = true;
    }

    // WinTab may drive only while Ink is silent *and* the pen is over a
    // PLANK window that is in front and capturing input. Otherwise Ink's
    // silence just means the pen is drawing in another application.
    const bool inkActive = s_LastInkTicks != 0 && now - s_LastInkTicks < InkQuietMs;
    const bool target = !outOfProximity && s_Handler != nullptr && s_Handler->isWintabPenTarget();
    if (inkActive && target && !s_Driving) {
        // Ink drives, but sends nothing while the pen is held still: a press
        // or lift without movement still has to reach the host.
        s_Handler->checkWintabContact(s_Pressure);
    }
    if (inkActive || !target) {
        s_StreakStartTicks = 0;
        if (s_Driving) {
            stopDriving();
            SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, inkActive ?
                            "PLANK pen: Windows Ink drives the pen, WinTab gives pressure" :
                            "PLANK pen: the pen left PLANK; WinTab stops driving");
        }
        return;
    }
    if (s_StreakStartTicks == 0) {
        s_StreakStartTicks = now;
    }
    if (!s_Driving) {
        if (now - s_StreakStartTicks < DriveAfterMs) {
            return;
        }
        s_Driving = true;
        s_WaitForLift = s_Pressure > 0.0f;
        SDL_LogInfo(SDL_LOG_CATEGORY_INPUT,
                    "PLANK pen: Windows Ink is silent; WinTab drives the pen");
    }
    if (s_WaitForLift && s_Pressure <= 0.0f) {
        s_WaitForLift = false;
    }

    // Wacom numbers cursors in threes per tablet: puck, pen tip, eraser.
    const bool eraser = packet.pkCursor % 3 == 2;
    s_Handler->handleWintabPen(false, s_WaitForLift ? 0.0f : s_Pressure,
                               static_cast<unsigned>(packet.pkButtons), eraser);
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
            if (!s_LoggedFirstPacket) {
                s_LoggedFirstPacket = true;
                SDL_LogInfo(SDL_LOG_CATEGORY_INPUT,
                            "PLANK pen: first WinTab packet received");
            }
            handlePacket(packet);
        }
    }
    else if (msg->message == WM_POINTERENTER && s_Overlap != nullptr) {
        // Put this context on top whenever a pen comes over a PLANK window,
        // so the driver delivers to it even after another tablet application
        // was in front. (WM_ACTIVATE is sent, not posted, so the message hook
        // never sees it.)
        s_Overlap(s_Context, TRUE);
    }
    else if (msg->message == WT_PROXIMITY &&
             reinterpret_cast<HCTX>(msg->wParam) == s_Context &&
             LOWORD(msg->lParam) == 0) {
        // The pen left the context.
        s_InProximity = false;
        s_PressureValid = false;
        stopDriving();
    }
    else if (msg->message == WM_POINTERLEAVE) {
        s_PressureValid = false;
    }
}

// Ink delivered a pen message: Ink drives.
void noteInkPen()
{
    s_LastInkTicks = SDL_GetTicks();
    s_StreakStartTicks = 0;
    if (s_Driving) {
        stopDriving();
        SDL_LogInfo(SDL_LOG_CATEGORY_INPUT,
                    "PLANK pen: Windows Ink drives the pen, WinTab gives pressure");
    }
}

// Whether WinTab is driving the pen right now.
bool driving()
{
    // The driver sends nothing while the pen is held still, so silence with
    // the tip down is a held stroke, not a pen that has gone: only a silent
    // pen that is up stops driving (and releases the mouse).
    if (s_Driving && s_Pressure <= 0.0f &&
            SDL_GetTicks() - s_LastPacketTicks > PacketQuietMs) {
        stopDriving();
        s_InProximity = false;
        s_StreakStartTicks = 0;
    }
    return s_Driving && s_InProximity;
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
        const bool isPen = GetPointerType(pointerId, &pointerType) && pointerType == PT_PEN;
        if (isPen) {
            Wintab::noteInkPen();
        }
        if (isPen && GetPointerInfo(pointerId, &info)) {
            POINT position = info.ptPixelLocation;
            POINT origin = {0, 0};
            if (ScreenToClient(msg->hwnd, &position) &&
                    ClientToScreen(msg->hwnd, &origin)) {
                // ptPixelLocation is whole pixels, so a slow stroke moves in
                // one-pixel stairs; see s_PenOffsetValid for the fraction.
                float x = static_cast<float>(position.x);
                float y = static_cast<float>(position.y);
                RECT deviceRect;
                RECT displayRect;
                if (GetPointerDeviceRects(info.sourceDevice, &deviceRect, &displayRect) &&
                        deviceRect.right > deviceRect.left &&
                        deviceRect.bottom > deviceRect.top) {
                    const double fx = displayRect.left +
                            static_cast<double>(info.ptHimetricLocation.x - deviceRect.left) *
                            (displayRect.right - displayRect.left) /
                            (deviceRect.right - deviceRect.left) - origin.x;
                    const double fy = displayRect.top +
                            static_cast<double>(info.ptHimetricLocation.y - deviceRect.top) *
                            (displayRect.bottom - displayRect.top) /
                            (deviceRect.bottom - deviceRect.top) - origin.y;
                    const double errorX = fx - x;
                    const double errorY = fy - y;
                    const float error = static_cast<float>(std::max(std::fabs(errorX), std::fabs(errorY)));
                    s_PenMapErrorLast.store(error);
                    if (error > s_PenMapErrorMax.load()) {
                        s_PenMapErrorMax.store(error);
                    }
                    if (error < 16.0f) {
                        // The whole-pixel position truncates, so a correct
                        // mapping sits up to a pixel right of / below it:
                        // aim the offset at half a pixel.
                        if (!s_PenOffsetValid ||
                                !EqualRect(&deviceRect, &s_PenDeviceRect) ||
                                !EqualRect(&displayRect, &s_PenDisplayRect)) {
                            s_PenOffsetX = errorX - 0.5;
                            s_PenOffsetY = errorY - 0.5;
                            s_PenDeviceRect = deviceRect;
                            s_PenDisplayRect = displayRect;
                            s_PenOffsetValid = true;
                        }
                        else {
                            // Screen position, not client: the error belongs
                            // to the screen, whichever window the pen is over.
                            const double screenX = fx + origin.x;
                            const double screenY = fy + origin.y;
                            const double moved = std::hypot(screenX - s_PenLastMappedX,
                                                            screenY - s_PenLastMappedY);
                            // About 64 px of movement to settle, and never
                            // more than a small step from one sample, so a jump
                            // (the pen coming back elsewhere) cannot replace
                            // what was learned with one sample's rounding.
                            const double follow = std::min(0.05, moved / 640.0);
                            s_PenOffsetX += follow * ((errorX - 0.5) - s_PenOffsetX);
                            s_PenOffsetY += follow * ((errorY - 0.5) - s_PenOffsetY);
                        }
                        s_PenLastMappedX = fx + origin.x;
                        s_PenLastMappedY = fy + origin.y;
                        x = static_cast<float>(std::clamp(fx - s_PenOffsetX, x - 0.5, x + 1.5));
                        y = static_cast<float>(std::clamp(fy - s_PenOffsetY, y - 0.5, y + 1.5));
                    }
                }
                s_PenPixelX.store(x);
                s_PenPixelY.store(y);
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
    float x;
    float y;
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
    Wintab::stopDriving();
    Wintab::s_Handler = nullptr;
    Wintab::close();
}

void SdlInputHandler::attachPenPressureSource(SDL_Window* window)
{
    // PLANK_WINTAB=0 leaves the pen to Windows Ink alone.
    if (qEnvironmentVariableIsSet("PLANK_WINTAB") &&
            qEnvironmentVariableIntValue("PLANK_WINTAB") == 0) {
        return;
    }
    Wintab::s_Handler = this;
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    HWND hwnd = properties != 0 ?
                reinterpret_cast<HWND>(SDL_GetPointerProperty(
                    properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)) :
                nullptr;
    Wintab::open(hwnd);
}

bool SdlInputHandler::isWintabPenDriving()
{
    // While WinTab drives the pen, the cursor the driver moves for it can
    // arrive as plain mouse input; that must not reach the host as well.
    return isPenCaptureAvailable() && Wintab::driving();
}

void SdlInputHandler::checkWintabContact(float pressure)
{
    if (m_LastPenWindow == nullptr || (pressure > 0.0f) == m_PenContactSent) {
        return;
    }
    // Only where the pen last drew: over the letterbox, the toolbar or
    // another PLANK window there is nothing on the host to press.
    POINT cursor;
    const SDL_PropertiesID properties = SDL_GetWindowProperties(m_LastPenWindow);
    HWND hwnd = properties != 0 ?
                reinterpret_cast<HWND>(SDL_GetPointerProperty(
                    properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)) :
                nullptr;
    if (hwnd == nullptr || !GetCursorPos(&cursor) ||
            GetAncestor(WindowFromPoint(cursor), GA_ROOT) != GetAncestor(hwnd, GA_ROOT)) {
        return;
    }
    m_PenWintabPressureForced = pressure;
    sendPenEvent(m_PenContactSent ? LI_TOUCH_EVENT_UP : LI_TOUCH_EVENT_DOWN,
                 m_LastPenWindow, m_LastPenX, m_LastPenY);
    m_PenWintabPressureForced = -1.0f;
}

bool SdlInputHandler::isWintabPenTarget()
{
    if (!isPenCaptureAvailable() || !isCaptureActive()) {
        return false;
    }
    POINT cursor;
    if (!GetCursorPos(&cursor)) {
        return false;
    }
    HWND foreground = GetForegroundWindow();
    HWND under = GetAncestor(WindowFromPoint(cursor), GA_ROOT);
    if (foreground == nullptr || under != foreground) {
        return false;
    }
    const auto isOurs = [foreground](SDL_Window* window) {
        if (window == nullptr) {
            return false;
        }
        const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        HWND hwnd = properties != 0 ?
                    reinterpret_cast<HWND>(SDL_GetPointerProperty(
                        properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)) :
                    nullptr;
        return hwnd != nullptr && GetAncestor(hwnd, GA_ROOT) == foreground;
    };
    if (isOurs(m_Window)) {
        return true;
    }
    for (const auto& output : m_PresentationLayout.outputs) {
        if (isOurs(output.window)) {
            return true;
        }
    }
    return false;
}

void SdlInputHandler::handleWintabPen(bool outOfProximity, float pressure,
                                      unsigned buttons, bool eraser)
{
    if (!isPenCaptureAvailable()) {
        // Nothing can reach the host, but local state must not stay down.
        m_PenTipDown = false;
        m_WintabPenWindow = nullptr;
        return;
    }
    // The window under the cursor, which the driver keeps on the pen.
    SDL_Window* window = nullptr;
    float x = 0.0f;
    float y = 0.0f;
    POINT cursor;
    if (!outOfProximity && GetCursorPos(&cursor)) {
        const auto tryWindow = [&](SDL_Window* candidate) {
            if (window != nullptr || candidate == nullptr) {
                return;
            }
            const SDL_PropertiesID properties = SDL_GetWindowProperties(candidate);
            HWND hwnd = properties != 0 ?
                        reinterpret_cast<HWND>(SDL_GetPointerProperty(
                            properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)) :
                        nullptr;
            RECT client;
            POINT point = cursor;
            if (hwnd != nullptr && GetClientRect(hwnd, &client) &&
                    ScreenToClient(hwnd, &point) && PtInRect(&client, point)) {
                window = candidate;
                x = static_cast<float>(point.x);
                y = static_cast<float>(point.y);
            }
        };
        if (m_PresentationLayout.outputs.isEmpty()) {
            tryWindow(m_Window);
        }
        for (const auto& output : m_PresentationLayout.outputs) {
            tryWindow(output.window);
        }
    }

    if (window == nullptr) {
        // Off the tablet or off PLANK's windows: lift the pen if it was down
        // and let the host know it has gone.
        if (m_PenTipDown && m_WintabPenWindow != nullptr) {
            m_PenTipDown = false;
            sendPenEvent(LI_TOUCH_EVENT_UP, m_WintabPenWindow, m_WintabPenX, m_WintabPenY);
        }
        if (m_WintabPenWindow != nullptr) {
            sendPenEvent(LI_TOUCH_EVENT_HOVER_LEAVE, m_WintabPenWindow, 0.0f, 0.0f);
            m_WintabPenWindow = nullptr;
            resetPenState();
        }
        return;
    }

    // Wacom buttons: bit 0 is the tip, bits 1 and 2 the lower and upper side
    // buttons (SDL's barrel buttons 1 and 2 on the Ink path).
    unsigned char mask = 0;
    if (buttons & 0x2) {
        mask |= m_PenSideButtonsSwapped ? LI_PEN_BUTTON_SECONDARY : LI_PEN_BUTTON_PRIMARY;
    }
    if (buttons & 0x4) {
        mask |= m_PenSideButtonsSwapped ? LI_PEN_BUTTON_PRIMARY : LI_PEN_BUTTON_SECONDARY;
    }
    m_PenButtons = mask;
    m_PenEraser = eraser;
    m_PenPressure = pressure;
    m_PenPressureSeen = true;

    // Contact from the first trace of pressure, not from the driver's click
    // threshold, so the light start of a stroke is kept; the host's Wacom
    // driver applies its own (low) threshold for the click.
    const bool contact = pressure > 0.0f;
    unsigned char eventType;
    if (contact) {
        eventType = m_PenTipDown ? LI_TOUCH_EVENT_MOVE : LI_TOUCH_EVENT_DOWN;
    }
    else {
        eventType = m_PenTipDown ? LI_TOUCH_EVENT_UP : LI_TOUCH_EVENT_HOVER;
    }
    m_PenTipDown = contact;
    m_WintabPenWindow = window;
    m_WintabPenX = x;
    m_WintabPenY = y;
    sendPenEvent(eventType, window, x, y);
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
            x = sample.x;
            y = sample.y;
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
bool SdlInputHandler::isWintabPenDriving() { return false; }
bool SdlInputHandler::isWintabPenTarget() { return false; }
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
    if ((eventType == LI_TOUCH_EVENT_HOVER_LEAVE ||
            eventType == LI_TOUCH_EVENT_CANCEL_ALL) && m_PenContactSent) {
        liftPenAtLastPosition();
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
        if (wintabPressure < 0.0f && m_PenWintabPressureForced >= 0.0f) {
            wintabPressure = m_PenWintabPressureForced;
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
    bool tipDown = m_PenTipDown;
#ifdef Q_OS_WIN32
    // With WinTab pressure, WinTab also decides contact: Windows Ink only
    // reports the tip down once it passes the driver's click threshold
    // (15-25% on Culley's Intuos), so the light start and end of every
    // stroke were lost. Ink still gives the position.
    if (m_PenWintabPressure >= 0.0f) {
        tipDown = m_PenWintabPressure > 0.0f;
    }
#endif
    // The event follows from what the host was last told, so it can never
    // see two presses, or a hover with the tip still down.
    switch (eventType) {
    case LI_TOUCH_EVENT_DOWN:
    case LI_TOUCH_EVENT_UP:
    case LI_TOUCH_EVENT_MOVE:
    case LI_TOUCH_EVENT_HOVER:
        eventType = tipDown ?
                    (m_PenContactSent ? LI_TOUCH_EVENT_MOVE : LI_TOUCH_EVENT_DOWN) :
                    (m_PenContactSent ? LI_TOUCH_EVENT_UP : LI_TOUCH_EVENT_HOVER);
        break;
    default:
        break;
    }
    const float pressureOrDistance = tipDown ? contactPressure : m_PenDistance;

    // One line per second while a pen is in use. Without it a pen that draws
    // nothing is indistinguishable from a pen the client never saw, and a
    // tablet is the one input nobody can reproduce from a log of its own.
    const Uint64 now = SDL_GetTicks();
    if (now - m_PenLastLogTime >= 1000) {
        m_PenLastLogTime = now;
        SDL_LogInfo(SDL_LOG_CATEGORY_INPUT,
                    "PLANK pen: event=%u tip=%s pressure=%.3f%s distance=%.3f "
                    "tilt=%u rotation=%u buttons=0x%x eraser=%s at %.3f,%.3f "
                    "(SDL position was %+.0f,%+.0f px off)%s map-error=%.1f max %.1f px",
                    eventType, tipDown ? "down" : "up",
                    pressureOrDistance,
                    m_PenPressureSeen ? "" : " (no pressure axis reported)",
                    m_PenDistance,
                    static_cast<unsigned>(m_PenTilt),
                    static_cast<unsigned>(m_PenRotation),
                    m_PenButtons, m_PenEraser ? "yes" : "no",
                    normalizedX, normalizedY,
                    m_PenSdlOffsetX, m_PenSdlOffsetY,
                    m_PenWintabPressure >= 0.0f ? " [WinTab pressure]" : ""
#ifdef Q_OS_WIN32
                    , static_cast<double>(s_PenMapErrorLast.load()),
                    static_cast<double>(s_PenMapErrorMax.load())
#else
                    , 0.0, 0.0
#endif
                    );
    }
    const unsigned char toolType = m_PenEraser ? LI_TOOL_TYPE_ERASER : LI_TOOL_TYPE_PEN;

    if (eventType != LI_TOUCH_EVENT_HOVER_LEAVE &&
            eventType != LI_TOUCH_EVENT_CANCEL_ALL) {
        activateWin32PenCursor();
    }

    switch (eventType) {
    case LI_TOUCH_EVENT_DOWN:
    case LI_TOUCH_EVENT_MOVE:
        m_PenContactSent = true;
        break;
    case LI_TOUCH_EVENT_UP:
    case LI_TOUCH_EVENT_HOVER:
    case LI_TOUCH_EVENT_HOVER_LEAVE:
    case LI_TOUCH_EVENT_CANCEL_ALL:
        m_PenContactSent = false;
        break;
    default:
        break;
    }
    if (eventType != LI_TOUCH_EVENT_HOVER_LEAVE &&
            eventType != LI_TOUCH_EVENT_CANCEL_ALL) {
        m_LastPenWindow = window;
        m_LastPenX = windowX;
        m_LastPenY = windowY;
    }
    else {
        m_LastPenWindow = nullptr;
    }

    return LiSendPenEvent(eventType, toolType, m_PenButtons,
                          std::clamp(normalizedX, 0.0f, 1.0f),
                          std::clamp(normalizedY, 0.0f, 1.0f),
                          std::clamp(pressureOrDistance, 0.0f, 1.0f),
                          0.0f, 0.0f, m_PenRotation, m_PenTilt) == 0;
}

void SdlInputHandler::liftPenAtLastPosition()
{
    // Lift the tip on the host where it last was, and forget that place so
    // nothing can press there again until the pen draws in the picture.
    SDL_Window* window = m_LastPenWindow;
    m_LastPenWindow = nullptr;
    if (!m_PenContactSent || window == nullptr) {
        m_PenContactSent = false;
        return;
    }
    const bool tipDown = m_PenTipDown;
    m_PenTipDown = false;
    sendPenEvent(LI_TOUCH_EVENT_UP, window, m_LastPenX, m_LastPenY);
    m_PenTipDown = tipDown;
    m_PenContactSent = false;
    m_LastPenWindow = nullptr;
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
