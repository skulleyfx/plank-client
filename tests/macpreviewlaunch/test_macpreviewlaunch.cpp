#include <QtTest>

#include "macpreviewlaunch.h"
#include "macmediafeatures.h"

// PLANK 1.2 and later Mac hosts accept launch schema 7 only, after
// /plank/negotiate. PLANK 1.1 Mac hosts accept launch schemas 4-7 only. Schema 4 must carry
// exactly 11 fields with boolean clipboard and microphone, and its reply
// echoes both in a six-key services object.
class TestMacPreviewLaunch : public QObject
{
    Q_OBJECT

private slots:
    void sendsSchemaFourWithMediaOff();
    void parsesSchemaFourReply();
    void rejectsOtherReplies();
    void picksSchemaFromHostVersion();
    void asksForClipboardOnlyWhenAllowed();
    void offersOnlyWhatWindowsForwards();
    void agreesOnProfilesAndRejectsMismatches();
    void sendsSchemaSevenFromAgreement();
    void parsesSchemaSevenReply();
    void rejectsBadSchemaSevenReplies();
};

static NvOutputTopology fixedCaptureTopology()
{
    QFile file(QString::fromUtf8(qgetenv("PLANK_REPO_ROOT")) + "/tests/protocol/fixed-capture-v13.json");
    NvOutputTopology topology;
    if (file.open(QIODevice::ReadOnly))
        NvOutputTopology::fromJson(QJsonDocument::fromJson(file.readAll()).object(), topology);
    return topology;
}

static QJsonObject validReply(const NvOutputTopology& topology)
{
    return {{"schema_version", 4}, {"state", "connecting"},
            {"transport_token", QString::fromLatin1(QByteArray(32, '\x5a').toBase64())},
            {"udp_port", 47990}, {"max_udp_payload_size", 1452},
            {"capture", topology.toJson().value("capture")},
            {"services", QJsonObject {{"audio", true}, {"input", true}, {"pen", "normalized"},
                                      {"cursor", "embedded"}, {"clipboard", false}, {"microphone", false}}}};
}

void TestMacPreviewLaunch::sendsSchemaFourWithMediaOff()
{
    const auto topology = fixedCaptureTopology();
    QVERIFY(!topology.outputs.isEmpty());
    const auto request = MacPreviewLaunch::request(topology, 50000, 1452);
    QCOMPARE(request.size(), 11);
    QCOMPARE(request.value("schema_version"), QJsonValue(4));
    QCOMPARE(request.value("clipboard"), QJsonValue(false));
    QCOMPARE(request.value("microphone"), QJsonValue(false));
    QVERIFY(!request.contains("camera"));
    QCOMPARE(request.value("frame_rate"), QJsonValue(60));
    QVERIFY(MacPreviewLaunch::request(topology, 9999, 1452).isEmpty());
}

void TestMacPreviewLaunch::parsesSchemaFourReply()
{
    const auto topology = fixedCaptureTopology();
    MacPreviewLaunch::Reply reply;
    QVERIFY(MacPreviewLaunch::parseReply(validReply(topology), topology, 47990, 1452, reply));
    QCOMPARE(reply.transportToken.size(), 44);
    QCOMPARE(reply.configuration.sessionPort, 47990U);
}

void TestMacPreviewLaunch::rejectsOtherReplies()
{
    const auto topology = fixedCaptureTopology();
    MacPreviewLaunch::Reply reply;
    auto oldSchema = validReply(topology);
    oldSchema.insert("schema_version", 2);
    QVERIFY(!MacPreviewLaunch::parseReply(oldSchema, topology, 47990, 1452, reply));
    auto oldServices = validReply(topology);
    oldServices.insert("services", QJsonObject {{"audio", true}, {"input", true},
                                                {"pen", "normalized"}, {"cursor", "embedded"}});
    QVERIFY(!MacPreviewLaunch::parseReply(oldServices, topology, 47990, 1452, reply));
    // Never accept a clipboard or microphone this client did not ask for.
    auto unrequested = validReply(topology);
    unrequested.insert("services", QJsonObject {{"audio", true}, {"input", true}, {"pen", "normalized"},
                                                {"cursor", "embedded"}, {"clipboard", true}, {"microphone", false}});
    QVERIFY(!MacPreviewLaunch::parseReply(unrequested, topology, 47990, 1452, reply));
    QVERIFY(!MacPreviewLaunch::parseReply(validReply(topology), topology, 47991, 1452, reply));
    QVERIFY(reply.transportToken.isEmpty());
}

void TestMacPreviewLaunch::picksSchemaFromHostVersion()
{
    QCOMPARE(MacPreviewLaunch::launchSchemaFor("1.0.105"), 2);
    QCOMPARE(MacPreviewLaunch::launchSchemaFor("1.0.135"), 2);
    QCOMPARE(MacPreviewLaunch::launchSchemaFor("1.0.137"), 3);
    QCOMPARE(MacPreviewLaunch::launchSchemaFor("1.0.143"), 3);
    QCOMPARE(MacPreviewLaunch::launchSchemaFor("1.1.039"), 4);
    QCOMPARE(MacPreviewLaunch::launchSchemaFor("1.1.099-x"), 4);
    QCOMPARE(MacPreviewLaunch::launchSchemaFor("1.2.05"), 7);
    QCOMPARE(MacPreviewLaunch::launchSchemaFor("1.4.18"), 7);
    QCOMPARE(MacPreviewLaunch::launchSchemaFor("2.0.1"), 7);
    QCOMPARE(MacPreviewLaunch::launchSchemaFor(""), 7);
    QCOMPARE(MacPreviewLaunch::launchSchemaFor("garbage"), 7);
    QCOMPARE(MacPreviewLaunch::launchSchemaFor("1.0.143-culley-x"), 3);
    QCOMPARE(MacPreviewLaunch::launchSchemaFor("1.0.120-test"), 2);
    QCOMPARE(MacPreviewLaunch::displaySchemaFor("1.0.110-x"), 2);
    QCOMPARE(MacPreviewLaunch::displaySchemaFor("1.0.106"), 2);
    QCOMPARE(MacPreviewLaunch::displaySchemaFor("1.0.120"), 3);
    QCOMPARE(MacPreviewLaunch::displaySchemaFor("1.0.143"), 3);
    QCOMPARE(MacPreviewLaunch::displaySchemaFor("1.1.039"), 3);
    QCOMPARE(MacPreviewLaunch::displaySchemaFor("1.4.18"), 3);
}

void TestMacPreviewLaunch::asksForClipboardOnlyWhenAllowed()
{
    QCOMPARE(MacPreviewLaunch::expectedServices(3, true).value("clipboard"), QJsonValue(true));
    QCOMPARE(MacPreviewLaunch::expectedServices(3, false).value("clipboard"), QJsonValue(false));
    QVERIFY(!MacPreviewLaunch::expectedServices(2, true).contains("clipboard"));
}

static const QString Mode420 = QStringLiteral("hevc-10-420-videotoolbox");

// What a 1.4 Mac host answers to MacMediaFeatures::offer(): the chosen
// profile of each wanted feature, null for the rest.
static QJsonObject hostNegotiation(bool clipboard = true, const QString& mode = Mode420)
{
    const auto offered = MacMediaFeatures::offer(mode).value("features").toObject();
    QJsonObject features;
    for (const auto& name : MacMediaFeatures::names()) {
        const auto choices = offered.value(name).toArray();
        features.insert(name, choices.isEmpty() || (name == "clipboard" && !clipboard) ?
                            QJsonValue(QJsonValue::Null) : choices.first());
    }
    return {{"schema_version", 1}, {"launch_schema", 7}, {"transport", "plank-native/2"},
            {"required_features", MacMediaFeatures::required()}, {"features", features}};
}

static QJsonObject validReplyV7(const NvOutputTopology& topology, bool clipboard)
{
    // The host echoes the launch profiles it was sent, sizes included.
    MacMediaFeatures::Agreement agreement;
    MacMediaFeatures::select(hostNegotiation(true, topology.appleEncodingMode), topology.appleEncodingMode, agreement);
    auto features = MacPreviewLaunch::requestV7(topology, 50000, 1452, agreement, clipboard).value("features").toObject();
    return {{"schema_version", 7}, {"state", "connecting"},
            {"transport_token", QString::fromLatin1(QByteArray(32, '\x5a').toBase64())},
            {"udp_port", 47990}, {"max_udp_payload_size", 1452},
            {"capture", topology.toJson().value("capture")}, {"transport", "plank-native/2"},
            {"required_features", MacMediaFeatures::required()},
            {"features", features}};
}

void TestMacPreviewLaunch::offersOnlyWhatWindowsForwards()
{
    const auto offer = MacMediaFeatures::offer(Mode420);
    QCOMPARE(offer.value("schema_version"), QJsonValue(1));
    QCOMPARE(offer.value("transport"), QJsonValue("plank-native/2"));
    const auto features = offer.value("features").toObject();
    QCOMPARE(features.size(), 6);
    QCOMPARE(features.value("desktop").toArray().first().toObject().value("encoding_mode"), QJsonValue(Mode420));
    QCOMPARE(features.value("clipboard").toArray().size(), 1);
    QVERIFY(features.value("microphone").toArray().isEmpty());
    QVERIFY(features.value("camera").toArray().isEmpty());
    QVERIFY(MacMediaFeatures::offer("h264-8-420-software").isEmpty());
    QVERIFY(!MacMediaFeatures::offer("hevc-10-444-videotoolbox").isEmpty());
}

void TestMacPreviewLaunch::agreesOnProfilesAndRejectsMismatches()
{
    MacMediaFeatures::Agreement agreement;
    QVERIFY(MacMediaFeatures::select(hostNegotiation(), Mode420, agreement));
    QCOMPARE(agreement.launchSchema, 7);
    QVERIFY(agreement.enabled("desktop") && agreement.enabled("clipboard"));
    QVERIFY(!agreement.enabled("microphone") && !agreement.enabled("camera"));
    QVERIFY(MacMediaFeatures::select(hostNegotiation(false), Mode420, agreement));
    QVERIFY(!agreement.enabled("clipboard"));
    // An unknown extra feature is ignored.
    auto extra = hostNegotiation();
    auto extraFeatures = extra.value("features").toObject();
    extraFeatures.insert("hologram", QJsonObject {{"schema_version", 1}});
    extra.insert("features", extraFeatures);
    QVERIFY(MacMediaFeatures::select(extra, Mode420, agreement));
    // A host that grants a feature Windows did not offer is refused.
    auto microphone = hostNegotiation();
    auto microphoneFeatures = microphone.value("features").toObject();
    microphoneFeatures.insert("microphone", QJsonObject {{"schema_version", 2}});
    microphone.insert("features", microphoneFeatures);
    QVERIFY(!MacMediaFeatures::select(microphone, Mode420, agreement));
    auto noInput = hostNegotiation();
    auto noInputFeatures = noInput.value("features").toObject();
    noInputFeatures.insert("input", QJsonValue::Null);
    noInput.insert("features", noInputFeatures);
    QVERIFY(!MacMediaFeatures::select(noInput, Mode420, agreement));
    auto missingRequired = hostNegotiation();
    missingRequired.insert("required_features", QJsonArray {"desktop", "audio"});
    QVERIFY(!MacMediaFeatures::select(missingRequired, Mode420, agreement));
    auto wrongTransport = hostNegotiation();
    wrongTransport.insert("transport", "plank-native/1");
    QVERIFY(!MacMediaFeatures::select(wrongTransport, Mode420, agreement));
    auto wrongLaunch = hostNegotiation();
    wrongLaunch.insert("launch_schema", 4);
    QVERIFY(!MacMediaFeatures::select(wrongLaunch, Mode420, agreement));
    // The host must answer with the encoding mode that was offered.
    QVERIFY(!MacMediaFeatures::select(hostNegotiation(), "hevc-10-444-videotoolbox", agreement));
    QCOMPARE(agreement.launchSchema, 0);
}

void TestMacPreviewLaunch::sendsSchemaSevenFromAgreement()
{
    const auto topology = fixedCaptureTopology();
    MacMediaFeatures::Agreement agreement;
    QVERIFY(MacMediaFeatures::select(hostNegotiation(), topology.appleEncodingMode, agreement));
    const auto request = MacPreviewLaunch::requestV7(topology, 50000, 1452, agreement, true);
    QCOMPARE(request.size(), 7);
    QCOMPARE(request.value("schema_version"), QJsonValue(7));
    QCOMPARE(request.value("transport"), QJsonValue("plank-native/2"));
    QVERIFY(!request.contains("services") && !request.contains("clipboard"));
    const auto features = request.value("features").toObject();
    const auto desktop = features.value("desktop").toObject();
    QCOMPARE(desktop.value("frame_rate"), QJsonValue(60));
    QCOMPARE(desktop.value("bitrate_kbps"), QJsonValue(50000));
    QCOMPARE(desktop.value("width"), QJsonValue(topology.desktopWidth));
    QVERIFY(features.value("clipboard").isObject());
    QVERIFY(features.value("microphone").isNull() && features.value("camera").isNull());
    // Clipboard goes out null unless the user allows it.
    QVERIFY(MacPreviewLaunch::requestV7(topology, 50000, 1452, agreement).value("features")
            .toObject().value("clipboard").isNull());
    QVERIFY(MacPreviewLaunch::requestV7(topology, 9999, 1452, agreement).isEmpty());
    QVERIFY(MacPreviewLaunch::requestV7(topology, 50000, 1452, {}).isEmpty());
}

void TestMacPreviewLaunch::parsesSchemaSevenReply()
{
    const auto topology = fixedCaptureTopology();
    MacMediaFeatures::Agreement agreement;
    QVERIFY(MacMediaFeatures::select(hostNegotiation(), topology.appleEncodingMode, agreement));
    MacPreviewLaunch::Reply reply;
    QVERIFY(MacPreviewLaunch::parseReplyV7(validReplyV7(topology, true), topology, 47990, 1452, reply,
                                           agreement, 50000, true));
    QVERIFY(reply.clipboard);
    QCOMPARE(reply.transportToken.size(), 44);
    QCOMPARE(reply.configuration.sessionPort, 47990U);
    // The Mac may turn the clipboard down (account does not own the desktop).
    QVERIFY(MacPreviewLaunch::parseReplyV7(validReplyV7(topology, false), topology, 47990, 1452, reply,
                                           agreement, 50000, true));
    QVERIFY(!reply.clipboard);
    // Unknown optional features and keys are ignored.
    auto extra = validReplyV7(topology, true);
    auto features = extra.value("features").toObject();
    features.insert("hologram", QJsonObject {{"schema_version", 1}});
    extra.insert("features", features);
    QVERIFY(MacPreviewLaunch::parseReplyV7(extra, topology, 47990, 1452, reply, agreement, 50000, true));
}

void TestMacPreviewLaunch::rejectsBadSchemaSevenReplies()
{
    const auto topology = fixedCaptureTopology();
    MacMediaFeatures::Agreement agreement;
    QVERIFY(MacMediaFeatures::select(hostNegotiation(), topology.appleEncodingMode, agreement));
    MacPreviewLaunch::Reply reply;
    auto parse = [&](const QJsonObject& object, bool clipboard = true) {
        return MacPreviewLaunch::parseReplyV7(object, topology, 47990, 1452, reply, agreement, 50000, clipboard);
    };
    QVERIFY(parse(validReplyV7(topology, true)));
    // Schema 7 has no "services".
    auto services = validReplyV7(topology, true);
    services.insert("services", QJsonObject {{"audio", true}, {"input", true}});
    QVERIFY(!parse(services));
    // Never accept a clipboard this client did not ask for.
    QVERIFY(!parse(validReplyV7(topology, true), false));
    auto noRequired = validReplyV7(topology, true);
    noRequired.insert("required_features", QJsonArray {"desktop", "audio"});
    QVERIFY(!parse(noRequired));
    auto changedProfile = validReplyV7(topology, true);
    auto features = changedProfile.value("features").toObject();
    auto audio = features.value("audio").toObject();
    audio.insert("sample_rate", 44100);
    features.insert("audio", audio);
    changedProfile.insert("features", features);
    QVERIFY(!parse(changedProfile));
    auto oldSchema = validReplyV7(topology, true);
    oldSchema.insert("schema_version", 4);
    QVERIFY(!parse(oldSchema));
    QVERIFY(reply.transportToken.isEmpty());
}

QTEST_GUILESS_MAIN(TestMacPreviewLaunch)
#include "test_macpreviewlaunch.moc"
