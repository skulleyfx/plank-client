#pragma once

#include "outputtopology.h"
#include "macmediafeatures.h"
#include <Limelight.h>
#include <QByteArray>
#include <QStringList>

// Typed launch only. Each Mac host accepts exactly one launch schema, chosen
// here from the version it publishes in /serverinfo:
//   1.0.105 - 1.0.136  schema 2
//   1.0.137 - 1.0.x    schema 3 (adds clipboard)
//   1.1.x              schema 4 (adds microphone), the oldest a 1.1 host
//                      accepts without /plank/negotiate
//   1.2 and later      schema 7: /plank/negotiate first, then a launch that
//                      carries the negotiated media features (no "services")
// Text clipboard is asked for from schema 3 when the user allows it; the Mac
// decides (only when the signed-in account owns the desktop) and says so in
// the reply. Windows forwards no microphone or camera to a Mac host. Never reinterpret Linux's PLS1 launch response or infer services
// from a platform name or a decoder's capabilities.
namespace MacPreviewLaunch {

// Unknown or unreadable versions get the newest schema.
inline int launchSchemaFor(const QString& hostVersion)
{
    // A branch build carries a suffix ("1.0.143-culley-x"); only the
    // numbers decide.
    const QStringList parts = hostVersion.section(QLatin1Char('-'), 0, 0).split(QLatin1Char('.'));
    bool ok0 = false, ok1 = false, ok2 = false;
    const int major = parts.value(0).toInt(&ok0);
    const int minor = parts.value(1).toInt(&ok1);
    const int patch = parts.value(2).toInt(&ok2);
    if (parts.size() < 3 || !ok0 || !ok1 || !ok2 || major > 1 || (major == 1 && minor >= 2)) {
        return 7;
    }
    if (major == 1 && minor == 1) return 4;
    return patch >= 137 ? 3 : 2;
}

// /plank/display: schema 3 (with a backing scale) from 1.0.120, schema 2 before.
inline int displaySchemaFor(const QString& hostVersion)
{
    return launchSchemaFor(hostVersion) >= 3 ||
            hostVersion.section(QLatin1Char('-'), 0, 0).section(QLatin1Char('.'), 2, 2).toInt() >= 120 ? 3 : 2;
}

inline QJsonObject request(const NvOutputTopology& topology, int bitrateKbps,
                           int udpPayloadSize, int schema = 4, bool clipboard = false)
{
    NvOutputTopology checked;
    if (!NvOutputTopology::isFixedCaptureHost(topology.featureFlags) ||
            !NvOutputTopology::fromJson(topology.toJson(), checked) ||
            bitrateKbps < 10000 || bitrateKbps > 150000 ||
            udpPayloadSize < 1200 || udpPayloadSize > 65527) return {};
    if (schema < 2 || schema > 4) return {};
    QJsonObject body {{"schema_version", schema}, {"capture_generation", checked.generation},
            {"capture_id", checked.outputs.first().id},
            {"width", checked.desktopWidth}, {"height", checked.desktopHeight},
            {"encoding_mode", checked.appleEncodingMode}, {"frame_rate", 60},
            {"bitrate_kbps", bitrateKbps}, {"max_udp_payload_size", udpPayloadSize}};
    if (schema >= 3) body.insert("clipboard", clipboard);
    if (schema >= 4) body.insert("microphone", false);
    return body;
}

// The Mac may turn down a clipboard request, never grant one not asked for.
inline QJsonObject expectedServices(int schema, bool clipboard)
{
    QJsonObject services {{"audio", true}, {"input", true}, {"pen", "normalized"}, {"cursor", "embedded"}};
    if (schema >= 3) services.insert("clipboard", clipboard);
    if (schema >= 4) services.insert("microphone", false);
    return services;
}

// Schema 7 (Mac host 1.2 and later): the body carries the profiles chosen by
// /plank/negotiate. Clipboard goes out null unless the user allows it.
inline QJsonObject requestV7(const NvOutputTopology& topology, int bitrateKbps, int udpPayloadSize,
                             const MacMediaFeatures::Agreement& agreement, bool clipboard = false)
{
    NvOutputTopology checked;
    if (agreement.launchSchema != 7 || agreement.encodingMode != topology.appleEncodingMode ||
            !NvOutputTopology::isFixedCaptureHost(topology.featureFlags) ||
            !NvOutputTopology::fromJson(topology.toJson(), checked) ||
            bitrateKbps < 10000 || bitrateKbps > 150000 ||
            udpPayloadSize < 1200 || udpPayloadSize > 65527) return {};
    auto features = agreement.features;
    auto desktop = features.value("desktop").toObject();
    desktop.insert("width", checked.desktopWidth);
    desktop.insert("height", checked.desktopHeight);
    desktop.insert("encoding_mode", checked.appleEncodingMode);
    desktop.insert("frame_rate", 60);
    desktop.insert("bitrate_kbps", bitrateKbps);
    features.insert("desktop", desktop);
    if (!clipboard) features.insert("clipboard", QJsonValue::Null);
    return {{"schema_version", 7}, {"capture_generation", checked.generation},
            {"capture_id", checked.outputs.first().id}, {"max_udp_payload_size", udpPayloadSize},
            {"transport", MacMediaFeatures::transport()}, {"required_features", agreement.requiredFeatures},
            {"features", features}};
}

struct Reply {
    QByteArray transportToken;
    bool clipboard = false;
    PLANK_NATIVE_SESSION_CONFIGURATION configuration {};
};

inline bool fillReply(const QJsonObject& object, const NvOutputTopology& topology,
                      int approvedControlPort, Reply& reply)
{
    const QString token = object.value("transport_token").toString();
    if (token.size() != 44) return false;
    const QByteArray encoded = token.toLatin1();
    const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.size() != 32 || decoded.decoded.toBase64() != encoded) return false;
    reply.transportToken = encoded;
    reply.configuration.structSize = sizeof(reply.configuration);
    reply.configuration.negotiatedVideoFormat = topology.appleEncodingMode == QLatin1String("hevc-10-444-videotoolbox") ?
                VIDEO_FORMAT_H265_REXT10_444 : VIDEO_FORMAT_H265_MAIN10;
    // Schema 2 and later explicitly support normalized pen and PLD1 bitrate updates.
    reply.configuration.hostFeatureFlags = LI_FF_DYNAMIC_VIDEO_BITRATE | LI_FF_ENCODER_TARGET_ACK | LI_FF_PEN_TOUCH_EVENTS;
    reply.configuration.sessionPort = static_cast<uint32_t>(approvedControlPort);
    reply.configuration.serviceFlags = PLANK_NATIVE_SERVICE_AUDIO | PLANK_NATIVE_SERVICE_INPUT;
    reply.configuration.audioPacketDurationMs = 5;
    reply.configuration.opusConfiguration.sampleRate = 48000;
    reply.configuration.opusConfiguration.channelCount = 2;
    reply.configuration.opusConfiguration.streams = 1;
    reply.configuration.opusConfiguration.coupledStreams = 1;
    reply.configuration.opusConfiguration.mapping[0] = 0;
    reply.configuration.opusConfiguration.mapping[1] = 1;
    return true;
}

inline bool parseReply(const QJsonObject& object, const NvOutputTopology& topology,
                       int approvedControlPort, int udpPayloadSize, Reply& reply,
                       int schema = 4, bool clipboardRequested = false)
{
    // Always clear an earlier successful result before parsing a new response.
    reply.transportToken.fill('\0');
    reply = {};
    if (request(topology, 10000, udpPayloadSize, schema).isEmpty() ||
            approvedControlPort < 1 || approvedControlPort > 65535 ||
            object.size() != 7 || object.value("schema_version") != QJsonValue(schema) ||
            object.value("state") != QJsonValue("connecting") ||
            object.value("udp_port") != QJsonValue(approvedControlPort) ||
            object.value("max_udp_payload_size") != QJsonValue(udpPayloadSize) ||
            object.value("capture") != topology.toJson().value("capture") ||
            (object.value("services") != QJsonValue(expectedServices(schema, false)) &&
             !(clipboardRequested && schema >= 3 &&
               object.value("services") == QJsonValue(expectedServices(schema, true))))) return false;
    reply.clipboard = clipboardRequested && schema >= 3 &&
            object.value("services").toObject().value("clipboard") == QJsonValue(true);

    return fillReply(object, topology, approvedControlPort, reply);
}

// Schema 7 reply: nine keys and "features" instead of "services". Optional
// features this client did not ask for must be null; unknown names are ignored.
inline bool parseReplyV7(const QJsonObject& object, const NvOutputTopology& topology,
                         int approvedControlPort, int udpPayloadSize, Reply& reply,
                         const MacMediaFeatures::Agreement& agreement, int bitrateKbps, bool clipboardRequested = false)
{
    reply.transportToken.fill('\0');
    reply = {};
    const auto sent = requestV7(topology, bitrateKbps, udpPayloadSize, agreement, clipboardRequested);
    if (sent.isEmpty() || approvedControlPort < 1 || approvedControlPort > 65535 ||
            object.size() > 16 || object.contains("services") ||
            object.value("schema_version") != QJsonValue(7) ||
            object.value("state") != QJsonValue("connecting") ||
            object.value("udp_port") != QJsonValue(approvedControlPort) ||
            object.value("max_udp_payload_size") != QJsonValue(udpPayloadSize) ||
            object.value("capture") != topology.toJson().value("capture") ||
            object.value("transport") != MacMediaFeatures::transport() ||
            !MacMediaFeatures::validRequired(object.value("required_features")) ||
            !object.value("features").isObject() || object.value("features").toObject().size() > 32) return false;
    const auto features = object.value("features").toObject();
    const auto expected = sent.value("features").toObject();
    for (const auto& name : agreement.requiredFeatures)
        if (!object.value("required_features").toArray().contains(name)) return false;
    for (const auto& name : MacMediaFeatures::names()) {
        const auto value = features.value(name);
        if ((value.isNull() || value.isUndefined()) && !object.value("required_features").toArray().contains(name)) continue;
        if (!expected.value(name).isObject() ||
                !MacMediaFeatures::containsProfile(value, expected.value(name).toObject())) return false;
    }
    reply.clipboard = features.value("clipboard").isObject();
    return fillReply(object, topology, approvedControlPort, reply);
}

} // namespace MacPreviewLaunch
