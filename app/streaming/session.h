#pragma once

#include <QMutex>
#include <QSemaphore>
#include <QSize>
#include <QStringList>
#include <QVector>
#include <QWindow>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#include <Limelight.h>
#include <opus_multistream.h>
#include "settings/streamingpreferences.h"
#include "backend/planksharedscreen.h"
#include "backend/planksharedscreencontrol.h"
#include "input/input.h"
#include "video/decoder.h"
#include "audio/renderers/renderer.h"
#include "video/overlaymanager.h"
#include "videopacketlosswindow.h"
#include "planktoolbar.h"
#ifdef PLANK_TRANSPORT
#include "plankclipboard.h"
#endif

class ComputerManager;
class NvHTTP;
class PlankToolbar;
#ifdef PLANK_TRANSPORT
struct PlankTransportNativeEndpoint;
#endif

class SupportedVideoFormatList : public QList<int>
{
public:
    operator int() const
    {
        int value = 0;

        for (const int & v : *this) {
            value |= v;
        }

        return value;
    }

    void
    removeByMask(int mask)
    {
        int i = 0;
        while (i < this->length()) {
            if (this->value(i) & mask) {
                this->removeAt(i);
            }
            else {
                i++;
            }
        }
    }

    void
    deprioritizeByMask(int mask)
    {
        QList<int> deprioritizedList;

        int i = 0;
        while (i < this->length()) {
            if (this->value(i) & mask) {
                deprioritizedList.append(this->takeAt(i));
            }
            else {
                i++;
            }
        }

        this->append(std::move(deprioritizedList));
    }

    int maskByServerCodecModes(int serverCodecModes)
    {
        int mask = 0;

        // Feature-only flags don't map to a video format.
        serverCodecModes &= ~SCM_IDENTITY_GBR_444;

        const QMap<int, int> mapping = {
            {SCM_H264, VIDEO_FORMAT_H264},
            {SCM_H264_HIGH8_422, VIDEO_FORMAT_H264_HIGH8_422},
            {SCM_H264_HIGH8_444, VIDEO_FORMAT_H264_HIGH8_444},
            {SCM_H264_HIGH10_422, VIDEO_FORMAT_H264_HIGH10_422},
            {SCM_H264_HIGH10_444, VIDEO_FORMAT_H264_HIGH10_444},
            {SCM_HEVC, VIDEO_FORMAT_H265},
            {SCM_HEVC_MAIN10, VIDEO_FORMAT_H265_MAIN10},
            {SCM_HEVC_REXT8_444, VIDEO_FORMAT_H265_REXT8_444},
            {SCM_HEVC_REXT10_444, VIDEO_FORMAT_H265_REXT10_444},
            {SCM_AV1_MAIN8, VIDEO_FORMAT_AV1_MAIN8},
            {SCM_AV1_MAIN10, VIDEO_FORMAT_AV1_MAIN10},
            {SCM_AV1_HIGH8_444, VIDEO_FORMAT_AV1_HIGH8_444},
            {SCM_AV1_HIGH10_444, VIDEO_FORMAT_AV1_HIGH10_444},
        };

        for (QMap<int, int>::const_iterator it = mapping.cbegin(); it != mapping.cend(); ++it) {
            if (serverCodecModes & it.key()) {
                mask |= it.value();
                serverCodecModes &= ~it.key();
            }
        }

        // Make sure nobody forgets to update this for new SCM values
        SDL_assert(serverCodecModes == 0);

        int val = *this;
        return val & mask;
    }
};

class Session : public QObject
{
    Q_OBJECT

    friend class SdlInputHandler;
    friend class DeferredSessionCleanupTask;
    friend class AsyncConnectionStartThread;
    friend class ExecThread;
    friend class PlankReconnectThread;

public:
    explicit Session(NvComputer* computer, NvApp& app,
                     StreamingPreferences *preferences = nullptr,
                     ComputerManager *computerManager = nullptr);

    // NB: This may not get destroyed for a long time! Don't put any cleanup here.
    // Use Session::exec() or DeferredSessionCleanupTask instead.
    virtual ~Session();

    Q_INVOKABLE void exec(QWindow* qtWindow);

    Q_INVOKABLE void cancelConnectionStart();

    Q_INVOKABLE void respondToActiveSessionTakeover(bool takeOver);

    // Answer to activeSessionChoiceRequested(): 1 = take over, 2 = watch
    // (view only), anything else cancels.
    Q_INVOKABLE void respondToActiveSessionChoice(int choice);

    Q_PROPERTY(bool restartForDisplayChange READ restartForDisplayChange)

    bool restartForDisplayChange() const
    {
        return m_RestartForDisplayChange;
    }

    // The replacement session joins the live session as a viewer.
    Q_PROPERTY(bool restartAsViewer READ restartAsViewer)

    bool restartAsViewer() const
    {
        return m_RestartAsViewer;
    }

    Q_INVOKABLE Session* createDisplayChangeRestartSession();

    static
    void getDecoderInfo(SDL_Window* window,
                        bool& isHardwareAccelerated, bool& isFullScreenOnly,
                        QSize& maxResolution);

    static Session* get()
    {
        return s_ActiveSession;
    }

    Overlay::OverlayManager& getOverlayManager()
    {
        return m_OverlayManager;
    }

    void flushWindowEvents();

    static void postTabletCursorActivationEvent();

    void updateRenderedStats(float fps, float videoMbps)
    {
        m_CurrentRenderedFps.store(fps, std::memory_order_relaxed);
        m_CurrentVideoMbps.store(videoMbps, std::memory_order_relaxed);
    }

    VideoFecLossPercent currentVideoFecLoss() const
    {
        std::lock_guard<std::mutex> lock(m_VideoPacketLossSamplesLock);
        return m_CurrentVideoFecLoss;
    }

    std::uint32_t currentNetworkRttMs() const
    {
        return m_CurrentNetworkRttMs.load(std::memory_order_relaxed);
    }

signals:
    void stageStarting(QString stage);

    void stageFailed(QString stage, int errorCode, QString failingPorts);

    void connectionStarted();

    void sessionCleanupWaitChanged(bool waiting, QString text);

    void activeSessionTakeoverRequested(QString text);

    // Like activeSessionTakeoverRequested(), for a host that also lets this
    // client watch the live session.
    void activeSessionChoiceRequested(QString text, bool canTakeOver);

    void displayLaunchError(QString text);

    void displayLaunchWarning(QString text);


    void sessionFinished();

    // Emitted after sessionFinished() when the session is ready to be destroyed
    void readyForDeletion();

private:
    void execInternal();

    bool initialize();

    bool startConnectionAsync(bool reconnecting = false,
                              bool takeOverActiveSession = false);

    bool prepareViewerRestart(NvHTTP& http);

    struct PlankReconnectState {
        bool retainedRenderer = false;
        bool inputCaptureWasActive = false;
        int videoFormat = 0;
        int videoWidth = 0;
        int videoHeight = 0;
        int videoFrameRate = 0;
    };

    bool beginPlankReconnect(PlankReconnectState& state);
    void setPlankReconnectStatus(const char* text, bool warning);
    void applyToolbarAction(PlankToolbar::Action action);
    void restartForScreenLayout(bool twoScreens, bool fullScreen);

    bool runPlankReconnect();

    bool finishPlankReconnect(bool success,
                                       const PlankReconnectState& state);

    void clearPlankReconnectCredentials();

    bool startPlankTransportDataPlane(quint16 port,
                                 const QString& certificateSha256,
                                 const QString& token,
                                 quint16 quicUdpPayloadMtu);

    bool negotiatePlankTransportSession(quint16 sessionPort, QString& errorMessage);

    void stopPlankTransportDataPlane();

#ifdef PLANK_TRANSPORT
    void startPlankTransportMediaReceivers();
    void stopPlankTransportMediaReceivers();
    void plankTransportVideoReceiveLoop();
    void plankTransportAudioReceiveLoop();
    void plankTransportDataReceiveLoop();
    static int plankTransportNativeControlSender(void* context, uint32_t type,
                                            uint32_t value1, uint32_t value2);
    static int plankTransportNativeInputSender(void* context, uint8_t type,
                                          const unsigned char* payload,
                                          size_t payloadLength);
#endif

    bool validateLaunch(SDL_Window* testWindow);

    void emitLaunchWarning(QString text);

    bool populateDecoderProperties(SDL_Window* window);

    IAudioRenderer* createAudioRenderer(const POPUS_MULTISTREAM_CONFIGURATION opusConfig);

    bool initializeAudioRenderer();

    bool testAudio(int audioConfiguration);

    int getAudioRendererCapabilities(int audioConfiguration);

    void getWindowDimensions(int& x, int& y,
                             int& width, int& height);

    int getTargetDisplayIndex() const;

    bool snapshotClientDisplays();

    void rebuildPresentationLayout();

    bool placeFullscreenWindowOnDisplay(SDL_Window* window,
                                        SDL_DisplayID displayId);

    SDL_Window* windowForEvent(Uint32 windowId) const;

    bool anyPresentationWindowFocused() const;

    void setPresentationWindowsFullscreen(bool fullscreen);

    void minimizePresentationWindows();

    bool configurePlankHostLayout();

    QSize configurePlankDisplayMode();

    bool configurePlankLaunchGeometry();

    void toggleFullscreen();

    void updateOptimalWindowDisplayMode();

    enum class DecoderAvailability {
        None,
        Software,
        Hardware
    };

    DecoderAvailability getDecoderAvailability(SDL_Window* window,
                                               int videoFormat, int width, int height, int frameRate,
                                               bool enableIdentityGbr = false);

    static
    bool chooseDecoder(DecoderSelectionMode selectionMode,
                       SDL_Window* window, int videoFormat, int width, int height,
                       int frameRate, bool enableVsync, bool testOnly,
                       IVideoDecoder*& chosenDecoder,
                       bool enableIdentityGbr = false,
                       DecoderCaptureSource captureSource =
                           DecoderCaptureSource::Nvfbc8Bit,
                       DecoderEncoderBackend encoderBackend =
                           DecoderEncoderBackend::SoftwareCuda);

    bool isIdentityGbrEnabledForFormat(int videoFormat) const;
    DecoderCaptureSource decoderCaptureSource() const;
    DecoderEncoderBackend decoderEncoderBackend() const;

    static
    void clStageStarting(int stage);

    static
    void clStageFailed(int stage, int errorCode);

    static
    void clConnectionTerminated(int errorCode);

    static
    void clLogMessage(const char* format, ...);

    static
    void clConnectionStatusUpdate(int connectionStatus);

    static
    void clSetHdrMode(bool enabled);

    static
    void clRawHidControl(const unsigned char* data, unsigned int length);

    static
    void clVideoBitrateApplied(uint32_t requestedKbps, uint32_t appliedKbps, uint32_t peakKbps);

    static
    void clCursorChunk(const unsigned char* data, unsigned int length);

    static
    void clCursorPosition(const unsigned char* data, unsigned int length);

    static
    void updateVideoFecLoss(VideoFecLossPercent loss);

    static
    int arInit(int audioConfiguration,
               const POPUS_MULTISTREAM_CONFIGURATION opusConfig,
               void* arContext, int arFlags);

    static
    void arCleanup();

    static
    void arDecodeAndPlaySample(char* sampleData, int sampleLength);

    static
    int drSetup(int videoFormat, int width, int height, int frameRate, void*, int);

    static
    void drCleanup();

    static
    int drSubmitDecodeUnit(PDECODE_UNIT du);

    StreamingPreferences* m_Preferences;
    bool m_IsFullScreen;
    SupportedVideoFormatList m_SupportedVideoFormats; // Sorted in order of descending priority
    STREAM_CONFIGURATION m_StreamConfig;
    DECODER_RENDERER_CALLBACKS m_VideoCallbacks;
    AUDIO_RENDERER_CALLBACKS m_AudioCallbacks;
    NvComputer* m_Computer;
    StreamingPreferences::PlankVideoProfile m_PlankVideoProfile;
    StreamingPreferences::PlankCaptureSource m_PlankCaptureSource;
#ifdef PLANK_TRANSPORT
    PlankTransportNativeEndpoint* m_PlankTransportEndpoint = nullptr;
    std::atomic_bool m_PlankTransportReceiversStopping {false};
    std::thread m_PlankTransportVideoThread;
    std::thread m_PlankTransportAudioThread;
    std::thread m_PlankTransportDataThread;
#endif
    int m_PlankBitrateKbps;
    ComputerManager* m_ComputerManager;
    NvApp m_App;
    SDL_Window* m_Window;
    IVideoDecoder* m_VideoDecoder;
    SDL_SpinLock m_DecoderLock;
    bool m_AudioDisabled;
    bool m_AudioMuted;
    Uint32 m_FullScreenFlag;
    QWindow* m_QtWindow;
    bool m_ThreadedExec;
    bool m_UnexpectedTermination;
    std::atomic_bool m_ReconnectRequested;
    std::atomic<Uint64> m_DesktopHandoffNoticeDeadline {0};
    std::atomic_bool m_ReconnectGreeterConfirmed {false};
    std::atomic<Uint64> m_LastPlankVideoReceived {0};
    QString m_PlankWorkerInstance;
    QString m_PlankHostCertificateSha256;
    std::atomic_bool m_Reconnecting;
    std::atomic_bool m_ReconnectCancelled;
    std::atomic_bool m_CanReconnect;
    // Reconnects that end again shortly after completing form a loop (each one
    // may cost the user a second-factor approval), so they are bounded.
    std::atomic<Uint64> m_LastReconnectCompletedTicks {0};
    std::atomic_int m_RapidReconnects {0};
    QString m_ReconnectFailureMessage;
    std::atomic_bool m_ConnectionStartCancelled;
    std::atomic_bool m_WaitingForSessionCleanup;
    std::atomic_bool m_WaitingForActiveSessionTakeoverDecision {false};
    std::atomic_int m_ActiveSessionTakeoverDecision {0};
    // Shared screen: this session watches the workstation's live session and
    // sends nothing that could change it. m_ViewerStream is the controller's
    // stream shape, which a viewer must match exactly.
    bool m_JoinAsViewer = false;
    bool m_RestartAsViewer = false;
    PlankSharedScreen::Stream m_ViewerStream;
    bool m_RestartForDisplayChange = false;
    // Window mode the replacement session starts in after a screen-layout
    // restart: -1 = follow preferences, 0 = windowed, 1 = full screen.
    int m_RestartFullScreen = -1;
    bool m_DisplayChangeRestart = false;
    QString m_PlankUsername;
    QString m_PlankPassword;
    QString m_ResolvedScalingMode;
    QString m_ResolvedHostLayout;
    // Set once a refused layout has been retried as the host's own, so a
    // workstation that keeps refusing is reported rather than retried forever.
    bool m_HostLayoutFallbackApplied = false;
    QStringList m_ResolvedVirtualModes;

    struct ClientDisplaySnapshot {
        SDL_DisplayID displayId = 0;
        SDL_Rect logicalBounds = {};
        QSize nativeSize;
        QRect canvasRect;
    };
    QVector<ClientDisplaySnapshot> m_ClientDisplays;
    SDL_DisplayID m_TargetDisplayId = 0;
    bool m_UseMultiDisplayPresentation = false;
    bool m_PresentationFullscreen = false;
    bool m_HasWindowedPresentationGeometry = false;
    QVector<SDL_Window*> m_SecondaryWindows;
    PlankPresentationLayout m_PresentationLayout;
    SdlInputHandler* m_InputHandler;
    int m_FlushingWindowEventsRef;

    bool m_AsyncConnectionSuccess;
    int m_ActiveVideoFormat;
    int m_ActiveVideoWidth;
    int m_ActiveVideoHeight;
    int m_ActiveVideoFrameRate;

    OpusMSDecoder* m_OpusDecoder;
    IAudioRenderer* m_AudioRenderer;
    OPUS_MULTISTREAM_CONFIGURATION m_ActiveAudioConfig;
    OPUS_MULTISTREAM_CONFIGURATION m_OriginalAudioConfig;
    int m_AudioSampleCount;
    Uint64 m_DropAudioEndTime;
    quint64 m_AudioMediaFramesReceived;
    bool m_AvSyncTelemetryEnabled;
    Uint64 m_LastAudioTelemetryTime;

    Overlay::OverlayManager m_OverlayManager;
    std::unique_ptr<PlankToolbar> m_PlankToolbar;
    std::atomic<float> m_CurrentRenderedFps;
    std::atomic<float> m_CurrentVideoMbps;
    VideoFecLossPercent m_CurrentVideoFecLoss;
    std::atomic<std::uint32_t> m_CurrentNetworkRttMs;
    // Plain-text clipboard sync in the framed format every PLANK endpoint
    // uses (protocol/clipboard-sync.md): host offers arrive on event 5, ours
    // go out on the input channel. Only with hosts that advertise
    // NvOutputTopology::ClipboardSyncFeature.
    static const std::uint16_t PlankClipboardTextEvent = 5;
    // Shared screen consent, watchers and control hand-off: small JSON
    // messages in both directions, only when this launch set the
    // shared-screen feature bit (planksharedscreencontrol.h).
    static const std::uint16_t PlankSharedScreenEvent = PlankSharedScreen::EventType;
    void pollLocalClipboard(Uint64 now);
    void sendSharedScreenMessage(const QJsonObject& message);
    void serviceSharedScreen(Uint64 now);
#ifdef PLANK_TRANSPORT
    void sendPendingClipboardFrames();
#endif
    // Paints the window with the PLANK logo so it is not white while the
    // stream starts.
    void paintStartupSplash(SDL_Window* window);
    static const int PlankClipboardMaxBytes = 512 * 1024;  // PLANK_CLIPBOARD_TEXT_LIMIT
    bool m_PlankClipboardEnabled = false;
#ifdef PLANK_TRANSPORT
    // Host offer being reassembled (transport thread only).
    plank::clipboard::Assembly m_PlankClipboardAssembly;
    std::uint64_t m_PlankClipboardLastHostGeneration = 0;
    // Our offer: frames are kept so queue pressure resumes at the unsent chunk.
    std::uint64_t m_PlankClipboardOutGeneration = 0;
    std::vector<std::vector<std::uint8_t>> m_PlankClipboardOutFrames;
    std::size_t m_PlankClipboardOutNext = 0;
#endif
    // Set when the launch told the host this client understands
    // shared-screen messages; nothing on event 6 is acted on otherwise.
    std::atomic_bool m_SharedScreenMessages {false};
    // Host messages waiting for the main thread (filled by the data receiver).
    QMutex m_SharedScreenInboxLock;
    QVector<QByteArray> m_SharedScreenInbox;
    // Prompts, watchers and control state; main thread only.
    PlankSharedScreen::Panel m_SharedScreenPanel;
    // Viewer only: true while the owner has handed this viewer the mouse and
    // keyboard. Read by the input sender, which drops a viewer's input
    // otherwise.
    std::atomic_bool m_ViewerInputAllowed {false};
    QMutex m_PlankClipboardLock;
    QString m_PlankClipboardIncoming;
    QString m_PlankClipboardLastSeen;
    Uint64 m_PlankClipboardNextPoll = 0;
    mutable std::mutex m_VideoPacketLossSamplesLock;
    VideoPacketLossPeakWindow m_VideoPacketLossPeakWindow;
    VideoPacketLossPeakWindow m_VideoPacketLossAfterFecPeakWindow;
    std::atomic<int> m_ConfirmedBitrateRequestKbps {0};
    std::atomic<int> m_ConfirmedBitrateAppliedKbps {0};
    std::atomic<int> m_ConfirmedBitratePeakKbps {0};

    static CONNECTION_LISTENER_CALLBACKS k_ConnCallbacks;
    static Session* s_ActiveSession;
    static QSemaphore s_ActiveSessionSemaphore;
};
