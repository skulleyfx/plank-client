#include <QtTest>

#include "macpreviewlaunch.h"

// PLANK 1.1 Mac hosts accept launch schemas 4-7 only. Schema 4 must carry
// exactly 11 fields with boolean clipboard and microphone, and its reply
// echoes both in a six-key services object.
class TestMacPreviewLaunch : public QObject
{
    Q_OBJECT

private slots:
    void sendsSchemaFourWithMediaOff();
    void parsesSchemaFourReply();
    void rejectsOtherReplies();
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

QTEST_GUILESS_MAIN(TestMacPreviewLaunch)
#include "test_macpreviewlaunch.moc"
