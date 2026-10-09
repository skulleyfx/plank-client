#pragma once

#include <QString>
#include <QStringList>

// Shared screen: a second client watches a workstation's live PLANK session.
// The host encodes once for the controller and sends the same stream to every
// viewer, so a viewer must launch with exactly the controller's stream shape,
// which the host publishes in its authorized serverinfo while a controller
// stream is live. Pure logic only, so it can be unit tested without SDL.
namespace PlankSharedScreen {

// Mirrors StreamingPreferences::PlankVideoProfile and PlankCaptureSource.
// session.cpp checks the values with static_assert.
enum Profile {
    ProfileH264_10Bit444,
    ProfileH264_8Bit422,
    ProfileH264_8Bit444,
    ProfileH264_10Bit422,
    ProfileNvencH264_8Bit444,
    ProfileNvencHevc8Bit444,
    ProfileNvencHevc10Bit444,
    ProfileAppleHevc10Bit420,
    ProfileAppleHevc10Bit444,
    ProfileNvencHevc8Bit420,
};

enum CaptureSource {
    CaptureNvfbc8Bit,
    CaptureX11Native10,
    CaptureScreenCaptureKit,
    CaptureDdup,
    CaptureWgc,
};

struct Stream {
    int width = 0;
    int height = 0;
    int fps = 0;
    QString encodingMode;
    QString captureSource;
    QString encoderBackend;
    int bitrateKbps = 0;
    int audioChannels = 0;
    int viewers = 0;
    // Derived from encodingMode and captureSource by parse().
    int profile = -1;
    int capture = -1;
};

// The inverse of the encoding-mode names session.cpp sends at launch.
inline int profileForEncodingMode(const QString& encodingMode)
{
    static const struct {
        const char* name;
        int profile;
    } modes[] = {
        {"h264-10-444-software", ProfileH264_10Bit444},
        {"h264-8-422-software", ProfileH264_8Bit422},
        {"h264-8-444-software", ProfileH264_8Bit444},
        {"h264-10-422-software", ProfileH264_10Bit422},
        {"h264-8-444-nvenc", ProfileNvencH264_8Bit444},
        {"hevc-8-444-nvenc", ProfileNvencHevc8Bit444},
        {"hevc-10-444-nvenc", ProfileNvencHevc10Bit444},
        {"hevc-10-420-videotoolbox", ProfileAppleHevc10Bit420},
        {"hevc-10-444-videotoolbox", ProfileAppleHevc10Bit444},
        {"hevc-8-420-nvenc", ProfileNvencHevc8Bit420},
    };
    for (const auto& mode : modes) {
        if (encodingMode == QLatin1String(mode.name)) {
            return mode.profile;
        }
    }
    return -1;
}

inline int captureSourceForName(const QString& captureSource)
{
    static const struct {
        const char* name;
        int capture;
    } sources[] = {
        {"nvfbc", CaptureNvfbc8Bit},
        {"x11-native10", CaptureX11Native10},
        {"screencapturekit", CaptureScreenCaptureKit},
        {"ddup", CaptureDdup},
        {"wgc", CaptureWgc},
    };
    for (const auto& source : sources) {
        if (captureSource == QLatin1String(source.name)) {
            return source.capture;
        }
    }
    return -1;
}

// The encoder backend session.cpp sends for a profile.
inline QString encoderBackendForProfile(int profile)
{
    if (profile == ProfileAppleHevc10Bit420 || profile == ProfileAppleHevc10Bit444) {
        return QStringLiteral("videotoolbox");
    }
    if ((profile >= ProfileNvencH264_8Bit444 && profile <= ProfileNvencHevc10Bit444) ||
            profile == ProfileNvencHevc8Bit420) {
        return QStringLiteral("nvenc-direct");
    }
    return QStringLiteral("software-cuda");
}

// "WxHxFPS", for example "3840x2160x60".
inline bool parseMode(const QString& mode, int& width, int& height, int& fps)
{
    const QStringList parts = mode.split(QLatin1Char('x'));
    if (parts.size() != 3) {
        return false;
    }
    bool widthOk = false;
    bool heightOk = false;
    bool fpsOk = false;
    const int parsedWidth = parts.at(0).toInt(&widthOk);
    const int parsedHeight = parts.at(1).toInt(&heightOk);
    const int parsedFps = parts.at(2).toInt(&fpsOk);
    if (!widthOk || !heightOk || !fpsOk ||
            parsedWidth <= 0 || parsedWidth > 16384 ||
            parsedHeight <= 0 || parsedHeight > 16384 ||
            parsedFps <= 0 || parsedFps > 480) {
        return false;
    }
    width = parsedWidth;
    height = parsedHeight;
    fps = parsedFps;
    return true;
}

/**
 * @brief Build the viewer stream from the host's PlankSharedStream* values.
 *
 * Fails when any value is missing or is not one this client can launch with
 * exactly, including an encoder backend that does not follow from the
 * encoding mode. An empty mode means no controller stream is live.
 */
inline bool parse(const QString& mode,
                  const QString& encodingMode,
                  const QString& captureSource,
                  const QString& encoderBackend,
                  const QString& bitrateKbps,
                  const QString& audioChannels,
                  const QString& viewers,
                  Stream& stream)
{
    Stream parsed;
    if (!parseMode(mode, parsed.width, parsed.height, parsed.fps)) {
        return false;
    }
    parsed.encodingMode = encodingMode;
    parsed.captureSource = captureSource;
    parsed.encoderBackend = encoderBackend;
    parsed.profile = profileForEncodingMode(encodingMode);
    parsed.capture = captureSourceForName(captureSource);
    if (parsed.profile < 0 || parsed.capture < 0 ||
            encoderBackend != encoderBackendForProfile(parsed.profile)) {
        return false;
    }

    bool bitrateOk = false;
    parsed.bitrateKbps = bitrateKbps.toInt(&bitrateOk);
    if (!bitrateOk || parsed.bitrateKbps <= 0) {
        return false;
    }

    bool channelsOk = false;
    parsed.audioChannels = audioChannels.toInt(&channelsOk);
    if (!channelsOk || (parsed.audioChannels != 2 && parsed.audioChannels != 6 &&
                        parsed.audioChannels != 8)) {
        return false;
    }

    // Informational only; an older host may leave it out.
    bool viewersOk = false;
    parsed.viewers = viewers.toInt(&viewersOk);
    if (!viewersOk || parsed.viewers < 0) {
        parsed.viewers = 0;
    }

    stream = parsed;
    return true;
}

// What a session may do to the workstation. A viewer only watches: nothing it
// does may reach the desktop, change the shared encoder or the host's
// displays, and a dropped viewer must never come back as a controller.
struct RolePolicy {
    bool sendsInput = true;
    bool syncsClipboard = true;
    bool changesBitrate = true;
    bool requestsAdaptiveBitrate = true;
    bool usesTwoScreens = true;
    bool requestsHostLayout = true;
    bool reconnectsAutomatically = true;
};

inline RolePolicy policyFor(bool viewer)
{
    RolePolicy policy;
    if (viewer) {
        policy.sendsInput = false;
        policy.syncsClipboard = false;
        policy.changesBitrate = false;
        policy.requestsAdaptiveBitrate = false;
        policy.usesTwoScreens = false;
        policy.requestsHostLayout = false;
        policy.reconnectsAutomatically = false;
    }
    return policy;
}

}
