#pragma once

#include "settings/streamingpreferences.h"
#include "backend/computermanager.h"
#include "streaming/plankpresentation.h"
#include "streaming/plankembeddedcursor.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#ifdef HAVE_LIBINPUT_TABLET
class LinuxWacomInput;
class LinuxRawWacomInput;
#endif
class PlankWaylandCursor;
#ifdef Q_OS_WIN32
class PlankWin32Cursor;
#endif

class SdlInputHandler
{
public:
    explicit SdlInputHandler(StreamingPreferences& prefs,
                             int streamWidth,
                             int streamHeight);

    ~SdlInputHandler();

    void setWindow(SDL_Window* window);

    void setStreamDimensions(int streamWidth, int streamHeight);

    void setPresentationLayout(const PlankPresentationLayout& layout);

    /**
     * @brief Watch the host without controlling it (shared-screen viewer).
     *
     * Nothing is captured or sent: no keyboard, mouse, pen or pasted text,
     * and the local pointer stays an ordinary system pointer. Where a native
     * cursor surface exists (Wayland, or a Windows overlay window) the host's
     * pointer is drawn at the host's position. Call before setWindow() for a
     * viewer. It may change while streaming when the owner hands over the
     * mouse and keyboard: leaving view-only lets keyboard and mouse through
     * (capture resumes on the next click, or setCaptureActive()); entering it
     * again releases held keys and the capture first. Pen, tablet and pasted
     * text stay off for a viewer session throughout.
     */
    void setViewOnly(bool viewOnly);

    void refreshWaylandTabletCursorParents();

    void handleKeyEvent(SDL_KeyboardEvent* event);

    void handleMouseButtonEvent(SDL_MouseButtonEvent* event);

    void handleMouseMotionEvent(SDL_MouseMotionEvent* event,
                                bool batchPendingEvents = true);

    void handleMouseWheelEvent(SDL_MouseWheelEvent* event);

    void handlePenProximityEvent(SDL_PenProximityEvent* event);

    void handlePenTouchEvent(SDL_PenTouchEvent* event);

    void handlePenMotionEvent(SDL_PenMotionEvent* event);

    void handlePenButtonEvent(SDL_PenButtonEvent* event);

    void handlePenAxisEvent(SDL_PenAxisEvent* event);

    /**
     * @brief Whether this client captures a pen itself.
     *
     * False where libinput owns the tablet, and where the host does not
     * accept pen events.
     */
    bool isPenCaptureAvailable() const;

    /**
     * @brief Send the pen's first side button as the host's right-click button.
     *
     * Windows reports one side button, and treats it as right-click. A Windows
     * host does the same with the first barrel button, but a Linux host turns
     * it into BTN_STYLUS, which X maps to middle-click. For a Linux host the
     * first and second barrel buttons are swapped so the button an artist
     * uses for right-click still right-clicks.
     */
    void setPenSideButtonsSwapped(bool swapped);

    // Windows: read the pen's position the way the mouse's is read (see pen.cpp).
    static void installPenPositionHook();
    static void removePenPositionHook();

    void sendText(QString& string);

    void handleRawHidControl(const unsigned char* data, unsigned int length);

    bool handleRemoteCursorChunk(const unsigned char* data, unsigned int length);

    bool handleRemoteCursorPosition(const unsigned char* data, unsigned int length);

    void applyPendingRemoteCursor();

    void applyPendingRemoteCursorPosition();

    void applyPendingTabletCursorActivation();

    void beginRawHidReconnect();
    void finishRawHidReconnect();

    void resetRemoteCursorPositionEpoch();

    void raiseAllKeys();

    void notifyMouseLeave();

    void notifyFocusLost();

    void notifyFocusGained();

    bool isCaptureActive();

    void setToolbarInteractionActive(bool active);

    void setLocalToolbarAvailable(bool available);

    bool isSystemKeyCaptureActive();

    void setCaptureActive(bool active);

    bool isMouseInVideoRegion(int mouseX, int mouseY,
                              Uint32 windowId = 0,
                              int windowWidth = -1,
                              int windowHeight = -1);

    void updateKeyboardGrabState();

    void updatePointerRegionLock();

private:
    enum KeyCombo {
        KeyComboQuit,
        KeyComboUngrabInput,
        KeyComboToggleFullScreen,
        KeyComboToggleStatsOverlay,
        KeyComboToggleMinimize,
        KeyComboPasteText,
        KeyComboTogglePointerRegionLock,
        KeyComboToggleKeyboardGrab,
        KeyComboMax
    };

    void performSpecialKeyCombo(KeyCombo combo);

    SDL_Window* m_Window;
    PlankPresentationLayout m_PresentationLayout;
    bool m_NeedsManualCaptureOnLeave;
    bool m_MouseWasInVideoRegion;
    bool m_PendingMouseButtonsAllUpOnVideoRegionLeave;
    bool m_PointerRegionLockActive;
    bool m_PointerRegionLockToggledByUser;
    bool m_LocalToolbarAvailable;
    bool m_LocalCursorSupported;
    PlankEmbeddedCursor m_EmbeddedCursor;
    bool m_RemoteCursorVisible;
    bool m_CompositorCursorRequestedVisible;
    bool m_TabletCursorActive;
    bool m_ViewOnly = false;
    // Set once a session has been view-only; pen capture stays off.
    bool m_ViewerSession = false;
    bool m_UngrabComboEnabledBeforeViewOnly = false;

    QSet<short> m_KeysDown;
    bool m_FakeMouseCaptureActive;
    bool m_KeyboardCaptureActive;
    StreamingPreferences::CaptureSysKeysMode m_CaptureSystemKeysMode;
    bool m_MouseCursorCapturedVisibilityState;

    struct RemoteCursorState {
        std::uint64_t generation = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t hotspotX = 0;
        std::uint32_t hotspotY = 0;
        std::uint32_t flags = 0;
        std::uint32_t nextOffset = 0;
        std::vector<unsigned char> pixels;
    };

    std::mutex m_RemoteCursorMutex;
    RemoteCursorState m_RemoteCursorAssembly;
    RemoteCursorState m_ReadyRemoteCursor;
    RemoteCursorState m_AppliedRemoteCursor;
    bool m_RemoteCursorAssemblyActive = false;
    bool m_ReadyRemoteCursorValid = false;
    bool m_AppliedRemoteCursorValid = false;
    std::atomic_bool m_RemoteCursorUpdatePending {false};
    SDL_Cursor* m_RemoteCursor = nullptr;

    struct RemoteCursorPosition {
        std::uint64_t sequence = 0;
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t frameWidth = 0;
        std::uint32_t frameHeight = 0;
    };

    std::mutex m_RemoteCursorPositionMutex;
    RemoteCursorPosition m_ReadyRemoteCursorPosition;
    RemoteCursorPosition m_AppliedRemoteCursorPosition;
    std::uint64_t m_HighestRemoteCursorPositionSequence = 0;
    std::uint64_t m_AppliedRemoteCursorPositionSequence = 0;
    std::uint64_t m_TabletCursorActivationSequence = 0;
    bool m_ReadyRemoteCursorPositionValid = false;
    bool m_AppliedRemoteCursorPositionValid = false;
    std::atomic_bool m_RemoteCursorPositionUpdatePending {false};
    std::atomic_bool m_TabletCursorActivationPending {false};
    struct WaylandTabletCursorOutput {
        SDL_Window* window = nullptr;
        std::unique_ptr<PlankWaylandCursor> cursor;
    };
    std::vector<WaylandTabletCursorOutput> m_WaylandTabletCursorOutputs;
#ifdef Q_OS_WIN32
    // Draws the host pointer over the video: always for a shared-screen
    // viewer, and for the controller while a pen is in use.
    std::unique_ptr<PlankWin32Cursor> m_Win32ViewerCursor;
#endif

    void setCursorVisible(bool visible);
    void activateCompositorCursor();
    void activateWin32PenCursor();
    PlankWaylandCursor* ensureWaylandTabletCursorAttached(
        SDL_Window* targetWindow);
    void reconcileWaylandTabletCursorOutputs();
    bool mapRemoteCursorPositionToWindow(const RemoteCursorPosition& position,
                                         SDL_Window*& window,
                                         int& x, int& y) const;
    void updateTabletCursorVisibility();
    bool hostCursorSurfaceVisible();
    void updateWin32ViewerCursor(bool imageChanged);
    bool setLocalCursorShape(RemoteCursorState& cursor);
    QSize streamDimensions() const;
    bool sendAbsoluteMousePosition(SDL_Window* window,
                                   int windowX, int windowY,
                                   bool allowClampedPosition);

    bool sendPenEvent(unsigned char eventType, SDL_Window* window,
                      float windowX, float windowY, Uint64 timestamp = 0);
    void resetPenState();

    SDL_Window* presentationWindow(Uint32 windowId) const;
    const PlankPresentationOutput* presentationOutput(
        SDL_Window* window) const;

    struct {
        KeyCombo keyCombo;
        SDL_Keycode keyCode;
        SDL_Scancode scanCode;
        bool enabled;
    } m_SpecialKeyCombos[KeyComboMax];

    std::atomic_uint64_t m_StreamDimensions;

    // Pen state between events: SDL reports each axis on its own, and a
    // position only with motion, so the latest of each is kept here.
    bool m_PenTipDown;
    unsigned char m_PenButtons;
    float m_PenPressure;
    float m_PenDistance;
    float m_PenTiltX;
    float m_PenTiltY;
    unsigned short m_PenRotation;
    unsigned char m_PenTilt;
    bool m_PenEraser;
    Uint64 m_PenLastLogTime;
    bool m_PenPressureSeen;
    bool m_PenSideButtonsSwapped;
    float m_PenSdlOffsetX;
    float m_PenSdlOffsetY;

#ifdef HAVE_LIBINPUT_TABLET
    std::unique_ptr<LinuxWacomInput> m_LinuxWacomInput;
    std::unique_ptr<LinuxRawWacomInput> m_LinuxRawWacomInput;
#endif

    static const int k_ButtonMap[];
};
