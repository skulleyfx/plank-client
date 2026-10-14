#pragma once

#include "outputtopology.h"
#include <Limelight.h>
#include <QByteArray>
#include <QStringList>

// Typed launch only. Each Mac host accepts exactly one launch schema, chosen
// here from the version it publishes in /serverinfo:
//   1.0.105 - 1.0.136  schema 2
//   1.0.137 - 1.0.x    schema 3 (adds clipboard)
//   1.1 and later      schema 4 (adds microphone), the oldest a 1.1 host
//                      accepts without /plank/negotiate
// Windows forwards no clipboard, microphone or camera to a Mac host, so they
// stay off. Never reinterpret Linux's PLS1 launch response or infer services
// from a platform name or a decoder's capabilities.
namespace MacPreviewLaunch {

// Unknown or unreadable versions get the newest schema.
inline int launchSchemaFor(const QString& hostVersion)
{
    const QStringList parts = hostVersion.split(QLatin1Char('.'));
    bool ok0 = false, ok1 = false, ok2 = false;
    const int major = parts.value(0).toInt(&ok0);
    const int minor = parts.value(1).toInt(&ok1);
    const int patch = parts.value(2).toInt(&ok2);
    if (parts.size() < 3 || !ok0 || !ok1 || !ok2 || major > 1 || (major == 1 && minor >= 1)) {
        return 4;
    }
    return patch >= 137 ? 3 : 2;
}

// /plank/display: schema 3 (with a backing scale) from 1.0.120, schema 2 before.
inline int displaySchemaFor(const QString& hostVersion)
{
    return launchSchemaFor(hostVersion) >= 3 ||
            hostVersion.section(QLatin1Char('.'), 2, 2).toInt() >= 120 ? 3 : 2;
}

inline QJsonObject request(const NvOutputTopology& topology, int bitrateKbps,
                           int udpPayloadSize, int schema = 4)
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
    if (schema >= 3) body.insert("clipboard", false);
    if (schema >= 4) body.insert("microphone", false);
    return body;
}

inline QJsonObject expectedServices(int schema)
{
    QJsonObject services {{"audio", true}, {"input", true}, {"pen", "normalized"}, {"cursor", "embedded"}};
    if (schema >= 3) services.insert("clipboard", false);
    if (schema >= 4) services.insert("microphone", false);
    return services;
}

struct Reply {
    QByteArray transportToken;
    PLANK_NATIVE_SESSION_CONFIGURATION configuration {};
};

inline bool parseReply(const QJsonObject& object, const NvOutputTopology& topology,
                       int approvedControlPort, int udpPayloadSize, Reply& reply,
                       int schema = 4)
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
            object.value("services") != QJsonValue(expectedServices(schema))) return false;

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

} // namespace MacPreviewLaunch
