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
 * @return Whether the event was sent.
 */
bool SdlInputHandler::sendPenEvent(unsigned char eventType, SDL_Window* window,
                                   float windowX, float windowY)
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
            return false;
        }
        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        if (windowWidth <= 0 || windowHeight <= 0) {
            return false;
        }

        QPointF streamPoint;
        if (!PlankPresentation::mapWindowPointToStream(
                    QPointF(windowX, windowY),
                    QSize(windowWidth, windowHeight), streamSize,
                    m_PresentationLayout.canvasSize, output->canvasRect,
                    streamPoint, false)) {
            // The pen is outside the picture; nothing to draw on.
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
    const float contactPressure = m_PenPressureSeen ? m_PenPressure : 1.0f;
    const float pressureOrDistance = m_PenTipDown ? contactPressure : m_PenDistance;

    // One line per second while a pen is in use. Without it a pen that draws
    // nothing is indistinguishable from a pen the client never saw, and a
    // tablet is the one input nobody can reproduce from a log of its own.
    const Uint64 now = SDL_GetTicks();
    if (now - m_PenLastLogTime >= 1000) {
        m_PenLastLogTime = now;
        SDL_LogInfo(SDL_LOG_CATEGORY_INPUT,
                    "PLANK pen: event=%u tip=%s pressure=%.3f%s distance=%.3f "
                    "tilt=%u rotation=%u buttons=0x%x eraser=%s at %.3f,%.3f",
                    eventType, m_PenTipDown ? "down" : "up",
                    pressureOrDistance,
                    m_PenPressureSeen ? "" : " (no pressure axis reported)",
                    m_PenDistance,
                    static_cast<unsigned>(m_PenTilt),
                    static_cast<unsigned>(m_PenRotation),
                    m_PenButtons, m_PenEraser ? "yes" : "no",
                    normalizedX, normalizedY);
    }
    const unsigned char toolType = m_PenEraser ? LI_TOOL_TYPE_ERASER : LI_TOOL_TYPE_PEN;

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
                 window, event->x, event->y);
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
                 window, event->x, event->y);
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
        mask = LI_PEN_BUTTON_PRIMARY;
        break;
    case 2:
        mask = LI_PEN_BUTTON_SECONDARY;
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
    sendPenEvent(LI_TOUCH_EVENT_BUTTON_ONLY, window, event->x, event->y);
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
