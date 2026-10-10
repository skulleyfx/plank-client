#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <QRectF>
#include <QString>
#include <QVector>

#include "backend/plankndipanel.h"
#include "backend/planksharedscreencontrol.h"
#include "planktoolbarlogic.h"

class QPainter;
class QFont;
class StreamingPreferences;
class SdlInputHandler;
class PlankWaylandToolbar;

namespace Overlay {
class OverlayManager;
}

class PlankToolbar
{
public:
    enum class Action {
        None,
        Consumed,
        ToggleFullscreen,
        ToggleTwoScreens,
        // Explicit choices from the monitor button's picker.
        ShowFullscreen,
        ShowWindowed,
        ShowTwoScreens,
        Minimize,
        Disconnect,
        KeepWaiting,
    };

    PlankToolbar(SDL_Window* window,
                          Overlay::OverlayManager& overlayManager,
                          SdlInputHandler& inputHandler,
                          StreamingPreferences& preferences,
                          int initialBitrateKbps,
                          const QString& codecLabel,
                          bool viewOnly = false);
    ~PlankToolbar();

    void setRenderedStats(float fps, float videoMbps, float packetLossPercent);
    // Network round-trip time in milliseconds; negative when not yet measured.
    void setNetworkLatencyMs(int latencyMs);

    /**
     * @brief Tell the toolbar what the resize button should offer.
     *
     * @param available Whether a second client monitor and a two-screen host are available.
     * @param inUse Whether this session is using two screens.
     */
    void setTwoScreenState(bool available, bool inUse);
    void setAppliedBitrate(int requestedKbps, int appliedKbps, int peakKbps);
    Action update(Uint64 now, bool transportAvailable = true);
    void showReconnectPrompt(int unreachableSeconds);
    void hideReconnectPrompt();
    // Returns true when status is presented independently of video frames.
    bool setReconnectStatus(const QString& text, bool warning);
    void notifyWindowChanged();
    void notifyFocusLost();

    /**
     * @brief Show shared-screen state from this panel (owned by the session).
     *
     * Adds a button to the main row (controller: "N watching"; viewer:
     * Request control / Give back) and, below the main row, the owner's
     * Allow/Deny prompts, the "has control - Take back" banner, the watcher
     * list and short notices. The toolbar stays on screen while anything
     * there needs an answer. Not called for hosts without shared screen.
     *
     * @param hostName Workstation name for the viewer's "CONTROLLING" badge.
     */
    void setSharedScreen(PlankSharedScreen::Panel* panel, const QString& hostName);

    /**
     * @brief Show the host's NDI on/off button (owned by the session).
     *
     * The button appears on the main row, left of the shared-screen button,
     * only while the panel says the host offers it (and never for a
     * view-only viewer). A click is queued on the panel; the session sends it.
     */
    void setNdi(PlankNdi::Panel* panel);

    bool observeMouseMotion(const SDL_MouseMotionEvent& event);
    Action handleMouseButton(const SDL_MouseButtonEvent& event);
    bool handleMouseWheel(const SDL_MouseWheelEvent& event);

    int eventWaitTimeout() const;

private:
    enum class Control {
        None,
        Handle,
        Slider,
        Pin,
        Fullscreen,
        Minimize,
        Disconnect,
        ChoiceFullscreen,
        ChoiceWindowed,
        ChoiceTwoScreens,
        // Shared screen: the main-row button and a button on a row below.
        ShareSlot,
        ShareButton,
        // The NDI on/off button.
        NdiSlot,
    };

    void show(Uint64 now);
    void hide();
    void beginLocalPointerInteraction();
    void endLocalPointerInteraction();
    void createWaylandToolbar();
    void createWaylandReconnectPrompt();
    void redraw();
    void redrawReconnectPrompt();
    void reconnectPromptPointerEnter(int parentX, int surfaceY);
    void reconnectPromptPointerLeave();
    void reconnectPromptPointerMotion(int parentX, int surfaceY);
    void reconnectPromptPointerButton(uint32_t button, bool down);
    int reconnectPromptLeft() const;
    int reconnectPromptTop() const;
    void updateBitrateFromPointer(int x, Uint64 now, bool forceSend);
    void queueBitrateRequest(Uint64 now, bool forceSend);
    void nativePointerEnter(int parentX, int parentY);
    void nativePointerLeave();
    void nativePointerMotion(int parentX, int parentY);
    void nativePointerButton(uint32_t button, bool down);
    void nativePointerWheel(int verticalSteps);
    Action handlePointerButton(const SDL_MouseButtonEvent& event);
    bool handlePointerWheel(const SDL_MouseWheelEvent& event);
    bool contains(int x, int y) const;
    bool sliderContains(int x, int y) const;
    bool handleContains(int x, int y) const;
    bool pinContains(int x, int y) const;
    bool fullscreenContains(int x, int y) const;
    bool minimizeContains(int x, int y) const;
    bool disconnectContains(int x, int y) const;
    QRectF modeChoiceRect(int index) const;
    Control modeChoiceAt(int x, int y) const;
    void closeModePicker();
    Control controlAt(int x, int y) const;
    int toolbarLeft() const;
    int sliderLeft() const;
    int sliderRight() const;
    // Main row plus the shared-screen rows below it.
    int toolbarHeight() const;
    int shareSlotWidth() const;
    // Toolbar-relative rectangles of shared-screen buttons.
    QRectF shareSlotRect() const;
    QRectF shareButtonRect(int row, int button) const;
    bool shareSlotContains(int x, int y) const;
    bool shareButtonAt(int x, int y,
                       PlankSharedScreen::Panel::Button& button) const;
    void drawSharedScreen(QPainter& painter, const QFont& labelFont);
    QString sharedScreenSignature(Uint64 now) const;
    void updateSharedScreen(Uint64 now);
    // NDI button: the panel decides if it exists; the cached flag keeps the
    // layout and hit tests the same as what was last drawn.
    bool ndiVisible() const;
    int ndiSlotWidth() const;
    QRectF ndiSlotRect() const;
    bool ndiSlotContains(int x, int y) const;
    int shareBlockHeight() const;
    QString ndiRowText(Uint64 now, bool* isError = nullptr) const;
    void drawNdi(QPainter& painter, const QFont& labelFont);
    void updateNdi(Uint64 now);

    SDL_Window* m_Window;
    Overlay::OverlayManager& m_OverlayManager;
    SdlInputHandler& m_InputHandler;
    StreamingPreferences& m_Preferences;
    std::unique_ptr<PlankWaylandToolbar> m_WaylandToolbar;
    std::unique_ptr<PlankWaylandToolbar> m_WaylandReconnectPrompt;
    QString m_ReconnectStatus;
    bool m_ReconnectStatusWarning = false;
    Action m_PendingAction;
    bool m_Visible;
    bool m_Pinned;
    bool m_DraggingToolbar;
    bool m_DraggingSlider;
    bool m_PointerInside;
    bool m_PointerInitialized;
    bool m_LocalPointerInteraction;
    // Shared-screen viewer: the bitrate belongs to the controller, so the
    // slider is replaced by a "View only" badge.
    bool m_ViewOnly;
    bool m_BitrateSupported;
    bool m_ReconnectPromptVisible;
    bool m_ReconnectPromptPointerInside;
    bool m_ReconnectPromptButtonDown;
    int m_ReconnectPromptPointerX;
    int m_ReconnectPromptPointerY;
    int m_ReconnectPromptPressedButton;
    int m_ReconnectPromptSeconds;
    int m_WindowWidth;
    int m_WindowHeight;
    int m_WindowPixelWidth;
    int m_WindowPixelHeight;
    float m_PixelDensity;
    int m_Width;
    int m_ToolbarLeft;
    int m_ToolbarDragOffsetX;
    int m_PointerX;
    int m_PointerY;
    Control m_PressedControl;
    PlankToolbarLogic::ButtonRouter m_ButtonRouter;
    int m_BitrateKbps;
    int m_LastSentBitrateKbps;
    int m_AppliedBitrateKbps;
    int m_AppliedPeakKbps;
    float m_RenderedFps;
    float m_VideoMbps;
    float m_PacketLossPercent;
    int m_NetworkLatencyMs;
    int m_LastDrawnNetworkLatencyMs;
    QString m_CodecLabel;
    bool m_TwoScreensAvailable {};
    bool m_TwoScreensInUse {};
    // The monitor button opens a three-way picker over the stats/slider area.
    bool m_ModePickerOpen {};
    Uint64 m_ModePickerDeadline {};
    Uint64 m_LastStayLogTime;
    float m_LastDrawnFps;
    float m_LastDrawnVideoMbps;
    float m_LastDrawnPacketLossPercent;
    Uint64 m_HideDeadline;
    Uint64 m_LastBitrateSendTime;
    Uint64 m_LastBitrateChangeTime;
    Uint64 m_LastToolbarMoveDrawTime;
    Uint64 m_LastRedrawTime;
    Uint64 m_EdgeHoverStartTime;
    // Shared screen; null when the host does not offer it.
    PlankSharedScreen::Panel* m_SharedScreen = nullptr;
    QString m_HostName;
    // The rows as last drawn; hit tests use the same layout.
    QVector<PlankSharedScreen::Panel::Row> m_ShareRows;
    QString m_SharedScreenDrawn;
    bool m_SharedScreenAttention = false;
    PlankSharedScreen::Panel::Button m_PressedShareButton;
    // NDI button; null when the session has none.
    PlankNdi::Panel* m_Ndi = nullptr;
    bool m_NdiShown = false;
    // The line under the main row (error or hover text) as last laid out.
    QString m_NdiRowText;
    bool m_NdiRowError = false;
    QString m_NdiDrawn;
};
