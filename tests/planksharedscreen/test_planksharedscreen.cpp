#include <QtTest>

#include "planksharedscreen.h"
#include "planksharedscreencontrol.h"

using namespace PlankSharedScreen;

class TestPlankSharedScreen : public QObject
{
    Q_OBJECT

private slots:
    void parsesMode();
    void rejectsMalformedModes_data();
    void rejectsMalformedModes();
    void mapsEveryEncodingMode_data();
    void mapsEveryEncodingMode();
    void mapsCaptureSources();
    void parsesLiveSharedStream();
    void missingFieldsMeanNothingToWatch();
    void rejectsMismatchedEncoderBackend();
    void rejectsUnknownValues();
    void viewerCountIsOptional();
    void viewerPolicyChangesNothing();
    void controllerPolicyIsUnchanged();
    void parsesHostMessages();
    void unknownHostMessagesAreNotErrors();
    void rejectsMalformedHostMessages_data();
    void rejectsMalformedHostMessages();
    void encodesClientMessages();
    void mapsViewerRefusals();
    void joinPromptAllowsAndDenies();
    void promptsQueueOneAtATime();
    void unansweredPromptIsDenied();
    void cancelClosesPromptWithoutAnswer();
    void controlPromptAnswersControl();
    void doubleClickDoesNotAnswerNextPrompt();
    void cancelEndsOnlyTheMatchingKind();
    void clicksRightAfterThePromptChangesAreIgnored();
    void watchersListAndKick();
    void controllerTakeBackBanner();
    void viewerRequestsAndReleasesControl();
    void viewerLosesControlWithReason();
    void viewerIgnoresControllerMessages();
};

void TestPlankSharedScreen::parsesMode()
{
    int width = 0;
    int height = 0;
    int fps = 0;
    QVERIFY(parseMode(QStringLiteral("3840x2160x60"), width, height, fps));
    QCOMPARE(width, 3840);
    QCOMPARE(height, 2160);
    QCOMPARE(fps, 60);
    QVERIFY(parseMode(QStringLiteral("5120x1440x30"), width, height, fps));
    QCOMPARE(width, 5120);
    QCOMPARE(height, 1440);
    QCOMPARE(fps, 30);
}

void TestPlankSharedScreen::rejectsMalformedModes_data()
{
    QTest::addColumn<QString>("mode");
    QTest::newRow("empty") << QString();
    QTest::newRow("two parts") << QStringLiteral("3840x2160");
    QTest::newRow("four parts") << QStringLiteral("3840x2160x60x1");
    QTest::newRow("text") << QStringLiteral("widexhighxfast");
    QTest::newRow("zero width") << QStringLiteral("0x2160x60");
    QTest::newRow("negative height") << QStringLiteral("3840x-2160x60");
    QTest::newRow("zero fps") << QStringLiteral("3840x2160x0");
    QTest::newRow("too wide") << QStringLiteral("20000x2160x60");
    QTest::newRow("upper-case separator") << QStringLiteral("3840X2160X60");
}

void TestPlankSharedScreen::rejectsMalformedModes()
{
    QFETCH(QString, mode);
    int width = 7;
    int height = 7;
    int fps = 7;
    QVERIFY(!parseMode(mode, width, height, fps));
    // Outputs are untouched on failure.
    QCOMPARE(width, 7);
    QCOMPARE(height, 7);
    QCOMPARE(fps, 7);
}

void TestPlankSharedScreen::mapsEveryEncodingMode_data()
{
    QTest::addColumn<QString>("encodingMode");
    QTest::addColumn<int>("profile");
    QTest::addColumn<QString>("encoderBackend");
    // The names session.cpp sends for each bookmark profile.
    QTest::newRow("h264 10 444 sw") << QStringLiteral("h264-10-444-software")
                                    << int(ProfileH264_10Bit444) << QStringLiteral("software-cuda");
    QTest::newRow("h264 8 422 sw") << QStringLiteral("h264-8-422-software")
                                   << int(ProfileH264_8Bit422) << QStringLiteral("software-cuda");
    QTest::newRow("h264 8 444 sw") << QStringLiteral("h264-8-444-software")
                                   << int(ProfileH264_8Bit444) << QStringLiteral("software-cuda");
    QTest::newRow("h264 10 422 sw") << QStringLiteral("h264-10-422-software")
                                    << int(ProfileH264_10Bit422) << QStringLiteral("software-cuda");
    QTest::newRow("h264 8 444 nvenc") << QStringLiteral("h264-8-444-nvenc")
                                      << int(ProfileNvencH264_8Bit444) << QStringLiteral("nvenc-direct");
    QTest::newRow("hevc 8 444 nvenc") << QStringLiteral("hevc-8-444-nvenc")
                                      << int(ProfileNvencHevc8Bit444) << QStringLiteral("nvenc-direct");
    QTest::newRow("hevc 10 444 nvenc") << QStringLiteral("hevc-10-444-nvenc")
                                       << int(ProfileNvencHevc10Bit444) << QStringLiteral("nvenc-direct");
    QTest::newRow("hevc 8 420 nvenc") << QStringLiteral("hevc-8-420-nvenc")
                                      << int(ProfileNvencHevc8Bit420) << QStringLiteral("nvenc-direct");
    QTest::newRow("hevc 10 420 vt") << QStringLiteral("hevc-10-420-videotoolbox")
                                    << int(ProfileAppleHevc10Bit420) << QStringLiteral("videotoolbox");
    QTest::newRow("hevc 10 444 vt") << QStringLiteral("hevc-10-444-videotoolbox")
                                    << int(ProfileAppleHevc10Bit444) << QStringLiteral("videotoolbox");
}

void TestPlankSharedScreen::mapsEveryEncodingMode()
{
    QFETCH(QString, encodingMode);
    QFETCH(int, profile);
    QFETCH(QString, encoderBackend);
    QCOMPARE(profileForEncodingMode(encodingMode), profile);
    QCOMPARE(encoderBackendForProfile(profile), encoderBackend);
}

void TestPlankSharedScreen::mapsCaptureSources()
{
    QCOMPARE(captureSourceForName(QStringLiteral("nvfbc")), int(CaptureNvfbc8Bit));
    QCOMPARE(captureSourceForName(QStringLiteral("x11-native10")), int(CaptureX11Native10));
    QCOMPARE(captureSourceForName(QStringLiteral("screencapturekit")), int(CaptureScreenCaptureKit));
    QCOMPARE(captureSourceForName(QStringLiteral("ddup")), int(CaptureDdup));
    QCOMPARE(captureSourceForName(QStringLiteral("wgc")), int(CaptureWgc));
    QCOMPARE(captureSourceForName(QStringLiteral("NVFBC")), -1);
    QCOMPARE(captureSourceForName(QString()), -1);
    QCOMPARE(profileForEncodingMode(QStringLiteral("av1-8-420-nvenc")), -1);
}

void TestPlankSharedScreen::parsesLiveSharedStream()
{
    Stream stream;
    QVERIFY(parse(QStringLiteral("3840x2160x60"), QStringLiteral("hevc-8-444-nvenc"),
                  QStringLiteral("wgc"), QStringLiteral("nvenc-direct"),
                  QStringLiteral("150000"), QStringLiteral("6"), QStringLiteral("1"),
                  stream));
    QCOMPARE(stream.width, 3840);
    QCOMPARE(stream.height, 2160);
    QCOMPARE(stream.fps, 60);
    QCOMPARE(stream.encodingMode, QStringLiteral("hevc-8-444-nvenc"));
    QCOMPARE(stream.captureSource, QStringLiteral("wgc"));
    QCOMPARE(stream.encoderBackend, QStringLiteral("nvenc-direct"));
    QCOMPARE(stream.bitrateKbps, 150000);
    QCOMPARE(stream.audioChannels, 6);
    QCOMPARE(stream.viewers, 1);
    QCOMPARE(stream.profile, int(ProfileNvencHevc8Bit444));
    QCOMPARE(stream.capture, int(CaptureWgc));
}

void TestPlankSharedScreen::missingFieldsMeanNothingToWatch()
{
    Stream stream;
    stream.width = 99;
    // No controller stream: the host leaves every field out.
    QVERIFY(!parse({}, {}, {}, {}, {}, {}, {}, stream));
    QCOMPARE(stream.width, 99);
    // Any single missing field is also refused.
    QVERIFY(!parse(QStringLiteral("1920x1080x60"), {}, QStringLiteral("nvfbc"),
                   QStringLiteral("nvenc-direct"), QStringLiteral("50000"),
                   QStringLiteral("2"), QStringLiteral("0"), stream));
    QVERIFY(!parse(QStringLiteral("1920x1080x60"), QStringLiteral("hevc-8-444-nvenc"),
                   QStringLiteral("nvfbc"), QStringLiteral("nvenc-direct"), {},
                   QStringLiteral("2"), QStringLiteral("0"), stream));
    QVERIFY(!parse(QStringLiteral("1920x1080x60"), QStringLiteral("hevc-8-444-nvenc"),
                   QStringLiteral("nvfbc"), QStringLiteral("nvenc-direct"),
                   QStringLiteral("50000"), {}, QStringLiteral("0"), stream));
}

void TestPlankSharedScreen::rejectsMismatchedEncoderBackend()
{
    Stream stream;
    QVERIFY(!parse(QStringLiteral("1920x1080x60"), QStringLiteral("hevc-8-444-nvenc"),
                   QStringLiteral("nvfbc"), QStringLiteral("software-cuda"),
                   QStringLiteral("50000"), QStringLiteral("2"), QStringLiteral("0"), stream));
    QVERIFY(!parse(QStringLiteral("1920x1080x60"), QStringLiteral("h264-10-444-software"),
                   QStringLiteral("nvfbc"), QStringLiteral("nvenc-direct"),
                   QStringLiteral("50000"), QStringLiteral("2"), QStringLiteral("0"), stream));
}

void TestPlankSharedScreen::rejectsUnknownValues()
{
    Stream stream;
    QVERIFY(!parse(QStringLiteral("1920x1080x60"), QStringLiteral("hevc-8-444-nvenc"),
                   QStringLiteral("dxgi"), QStringLiteral("nvenc-direct"),
                   QStringLiteral("50000"), QStringLiteral("2"), QStringLiteral("0"), stream));
    QVERIFY(!parse(QStringLiteral("1920x1080x60"), QStringLiteral("hevc-8-444-nvenc"),
                   QStringLiteral("nvfbc"), QStringLiteral("nvenc-direct"),
                   QStringLiteral("50000"), QStringLiteral("4"), QStringLiteral("0"), stream));
    QVERIFY(!parse(QStringLiteral("1920x1080x60"), QStringLiteral("hevc-8-444-nvenc"),
                   QStringLiteral("nvfbc"), QStringLiteral("nvenc-direct"),
                   QStringLiteral("0"), QStringLiteral("2"), QStringLiteral("0"), stream));
    QVERIFY(!parse(QStringLiteral("1920x1080x60"), QStringLiteral("hevc-8-444-nvenc"),
                   QStringLiteral("nvfbc"), QStringLiteral("nvenc-direct"),
                   QStringLiteral("fast"), QStringLiteral("2"), QStringLiteral("0"), stream));
}

void TestPlankSharedScreen::viewerCountIsOptional()
{
    Stream stream;
    QVERIFY(parse(QStringLiteral("2560x1440x60"), QStringLiteral("h264-8-444-nvenc"),
                  QStringLiteral("ddup"), QStringLiteral("nvenc-direct"),
                  QStringLiteral("80000"), QStringLiteral("2"), {}, stream));
    QCOMPARE(stream.viewers, 0);
    QCOMPARE(stream.audioChannels, 2);
}

void TestPlankSharedScreen::viewerPolicyChangesNothing()
{
    const RolePolicy policy = policyFor(true);
    QVERIFY(!policy.sendsInput);
    QVERIFY(!policy.syncsClipboard);
    QVERIFY(!policy.changesBitrate);
    QVERIFY(!policy.requestsAdaptiveBitrate);
    QVERIFY(!policy.usesTwoScreens);
    QVERIFY(!policy.requestsHostLayout);
    QVERIFY(!policy.reconnectsAutomatically);
}

void TestPlankSharedScreen::controllerPolicyIsUnchanged()
{
    const RolePolicy policy = policyFor(false);
    QVERIFY(policy.sendsInput);
    QVERIFY(policy.syncsClipboard);
    QVERIFY(policy.changesBitrate);
    QVERIFY(policy.requestsAdaptiveBitrate);
    QVERIFY(policy.usesTwoScreens);
    QVERIFY(policy.requestsHostLayout);
    QVERIFY(policy.reconnectsAutomatically);
}

void TestPlankSharedScreen::parsesHostMessages()
{
    HostMessage message;
    QVERIFY(parseHostMessage(R"({"t":"join_request","id":7,"account":"SKULLEY\\casey"})", message));
    QCOMPARE(int(message.type), int(HostMessage::JoinRequest));
    QCOMPARE(message.id, quint32(7));
    QCOMPARE(message.account, QStringLiteral("SKULLEY\\casey"));

    QVERIFY(parseHostMessage(R"({"t":"control_request","id":3,"account":"ana"})", message));
    QCOMPARE(int(message.type), int(HostMessage::ControlRequest));
    QCOMPARE(message.id, quint32(3));

    QVERIFY(parseHostMessage(R"({"t":"request_cancel","id":4294967295})", message));
    QCOMPARE(int(message.type), int(HostMessage::RequestCancel));
    QCOMPARE(message.id, quint32(4294967295u));
    QCOMPARE(int(message.cancelKind), int(HostMessage::CancelUnspecified));
    QVERIFY(parseHostMessage(R"({"t":"request_cancel","id":2,"kind":"control"})", message));
    QCOMPARE(int(message.cancelKind), int(HostMessage::CancelControl));
    QVERIFY(parseHostMessage(R"({"t":"request_cancel","id":2,"kind":"later"})", message));
    QCOMPARE(int(message.cancelKind), int(HostMessage::CancelUnspecified));

    QVERIFY(parseHostMessage(R"({"t":"watchers","viewers":[{"id":2,"account":"ana","control":true},{"id":5,"account":"bo","control":false}]})",
                             message));
    QCOMPARE(int(message.type), int(HostMessage::Watchers));
    QCOMPARE(int(message.watchers.size()), 2);
    QCOMPARE(message.watchers.at(0).id, quint32(2));
    QVERIFY(message.watchers.at(0).control);
    QCOMPARE(message.watchers.at(1).account, QStringLiteral("bo"));
    QVERIFY(!message.watchers.at(1).control);

    QVERIFY(parseHostMessage(R"({"t":"watchers","viewers":[]})", message));
    QVERIFY(message.watchers.isEmpty());

    QVERIFY(parseHostMessage(R"({"t":"control_state","has_control":false,"reason":"taken_back"})", message));
    QCOMPARE(int(message.type), int(HostMessage::ControlState));
    QVERIFY(!message.hasControl);
    QCOMPARE(message.reason, QStringLiteral("taken_back"));
    QVERIFY(parseHostMessage(R"({"t":"control_state","has_control":true})", message));
    QVERIFY(message.hasControl);
    QVERIFY(message.reason.isEmpty());
}

void TestPlankSharedScreen::unknownHostMessagesAreNotErrors()
{
    HostMessage message;
    QVERIFY(parseHostMessage(R"({"t":"something_new","x":1})", message));
    QCOMPARE(int(message.type), int(HostMessage::Unknown));
    QCOMPARE(message.name, QStringLiteral("something_new"));

    // Unknown messages change nothing.
    Panel panel;
    panel.handle(message, 0);
    QVERIFY(panel.rows(0).isEmpty());
    QVERIFY(panel.takeOutbox().isEmpty());
}

void TestPlankSharedScreen::rejectsMalformedHostMessages_data()
{
    QTest::addColumn<QByteArray>("payload");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("not json") << QByteArray("join_request");
    QTest::newRow("array") << QByteArray(R"([{"t":"watchers"}])");
    QTest::newRow("no type") << QByteArray(R"({"id":1})");
    QTest::newRow("numeric type") << QByteArray(R"({"t":6})");
    QTest::newRow("join without id") << QByteArray(R"({"t":"join_request","account":"a"})");
    QTest::newRow("join zero id") << QByteArray(R"({"t":"join_request","id":0,"account":"a"})");
    QTest::newRow("join negative id") << QByteArray(R"({"t":"join_request","id":-1,"account":"a"})");
    QTest::newRow("join fractional id") << QByteArray(R"({"t":"join_request","id":1.5,"account":"a"})");
    QTest::newRow("join huge id") << QByteArray(R"({"t":"join_request","id":4294967296,"account":"a"})");
    QTest::newRow("join text id") << QByteArray(R"({"t":"join_request","id":"1","account":"a"})");
    QTest::newRow("join without account") << QByteArray(R"({"t":"join_request","id":1})");
    QTest::newRow("cancel without id") << QByteArray(R"({"t":"request_cancel"})");
    QTest::newRow("cancel numeric kind") << QByteArray(R"({"t":"request_cancel","id":1,"kind":1})");
    QTest::newRow("watchers not a list") << QByteArray(R"({"t":"watchers","viewers":{}})");
    QTest::newRow("watcher without id") << QByteArray(R"({"t":"watchers","viewers":[{"account":"a"}]})");
    QTest::newRow("watcher text control") << QByteArray(R"({"t":"watchers","viewers":[{"id":1,"account":"a","control":"yes"}]})");
    QTest::newRow("state without flag") << QByteArray(R"({"t":"control_state","reason":"granted"})");
    QTest::newRow("state numeric flag") << QByteArray(R"({"t":"control_state","has_control":1})");
    QTest::newRow("too large") << QByteArray(R"({"t":"join_request","id":1,"account":")") +
                                  QByteArray(MaxMessageBytes, 'a') + QByteArray(R"("})");
}

void TestPlankSharedScreen::rejectsMalformedHostMessages()
{
    QFETCH(QByteArray, payload);
    HostMessage message;
    message.id = 99;
    QVERIFY(!parseHostMessage(payload, message));
    QCOMPARE(message.id, quint32(99));
}

void TestPlankSharedScreen::encodesClientMessages()
{
    QCOMPARE(encode(joinAnswer(7, true)), QByteArray(R"({"allow":true,"id":7,"t":"join_answer"})"));
    QCOMPARE(encode(joinAnswer(4294967295u, false)),
             QByteArray(R"({"allow":false,"id":4294967295,"t":"join_answer"})"));
    QCOMPARE(encode(controlAnswer(3, false)), QByteArray(R"({"allow":false,"id":3,"t":"control_answer"})"));
    QCOMPARE(encode(kick(12)), QByteArray(R"({"id":12,"t":"kick"})"));
    QCOMPARE(encode(takeBack()), QByteArray(R"({"t":"take_back"})"));
    QCOMPARE(encode(controlRequest()), QByteArray(R"({"t":"control_request"})"));
    QCOMPARE(encode(controlRelease()), QByteArray(R"({"t":"control_release"})"));
}

void TestPlankSharedScreen::mapsViewerRefusals()
{
    QVERIFY(viewerRefusalText(QStringLiteral("The owner declined")).contains(QStringLiteral("declined")));
    QVERIFY(viewerRefusalText(QStringLiteral("The owner did not answer")).contains(QStringLiteral("did not answer")));
    QVERIFY(viewerRefusalText(QStringLiteral("The owner's client cannot approve viewers"))
            .contains(QStringLiteral("update PLANK")));
    QCOMPARE(viewerRefusalText(QStringLiteral("viewer limit reached")), QStringLiteral("viewer limit reached"));
}

static HostMessage hostMessage(const char* json)
{
    HostMessage message;
    if (!parseHostMessage(QByteArray(json), message)) {
        qFatal("bad test message %s", json);
    }
    return message;
}

void TestPlankSharedScreen::joinPromptAllowsAndDenies()
{
    Panel panel;
    panel.handle(hostMessage(R"({"t":"join_request","id":7,"account":"casey"})"), 1000);
    QVERIFY(panel.needsAttention());
    QVector<Panel::Row> rows = panel.rows(1000);
    QCOMPARE(int(rows.size()), 1);
    QCOMPARE(rows.at(0).text, QStringLiteral("casey wants to watch your screen (30 s)"));
    QCOMPARE(int(rows.at(0).buttons.size()), 2);
    QCOMPARE(panel.rows(2500).at(0).text, QStringLiteral("casey wants to watch your screen (29 s)"));

    panel.activate(rows.at(0).buttons.at(0), 3000);
    QVector<QJsonObject> outbox = panel.takeOutbox();
    QCOMPARE(int(outbox.size()), 1);
    QCOMPARE(outbox.at(0), joinAnswer(7, true));
    QVERIFY(panel.rows(3000).isEmpty());
    QVERIFY(!panel.needsAttention());

    panel.handle(hostMessage(R"({"t":"join_request","id":8,"account":"ana"})"), 4000);
    panel.activate(panel.rows(4000).at(0).buttons.at(1), 4500);
    outbox = panel.takeOutbox();
    QCOMPARE(int(outbox.size()), 1);
    QCOMPARE(outbox.at(0), joinAnswer(8, false));

    // A stale button for a prompt that is gone does nothing.
    panel.activate(rows.at(0).buttons.at(0), 5000);
    QVERIFY(panel.takeOutbox().isEmpty());
}

void TestPlankSharedScreen::promptsQueueOneAtATime()
{
    Panel panel;
    panel.handle(hostMessage(R"({"t":"join_request","id":1,"account":"a"})"), 0);
    panel.handle(hostMessage(R"({"t":"join_request","id":2,"account":"b"})"), 100);
    // A repeat of a waiting request is not queued twice.
    panel.handle(hostMessage(R"({"t":"join_request","id":2,"account":"b"})"), 200);
    QCOMPARE(panel.pendingPrompts(), 2);
    QVector<Panel::Row> rows = panel.rows(100);
    QCOMPARE(int(rows.size()), 1);
    QVERIFY(rows.at(0).text.startsWith(QStringLiteral("a wants")));
    QVERIFY(rows.at(0).text.endsWith(QStringLiteral("1 more waiting")));

    panel.activate(rows.at(0).buttons.at(0), 500);
    rows = panel.rows(500);
    QCOMPARE(int(rows.size()), 1);
    QVERIFY(rows.at(0).text.startsWith(QStringLiteral("b wants")));
    QVERIFY(!rows.at(0).text.contains(QStringLiteral("more waiting")));
}

void TestPlankSharedScreen::unansweredPromptIsDenied()
{
    Panel panel;
    panel.handle(hostMessage(R"({"t":"join_request","id":1,"account":"a"})"), 1000);
    panel.handle(hostMessage(R"({"t":"control_request","id":2,"account":"b"})"), 20000);
    panel.tick(30999);
    QVERIFY(panel.takeOutbox().isEmpty());
    panel.tick(31000);
    QVector<QJsonObject> outbox = panel.takeOutbox();
    QCOMPARE(int(outbox.size()), 1);
    QCOMPARE(outbox.at(0), joinAnswer(1, false));
    // The queued request keeps its own deadline.
    QCOMPARE(panel.rows(31000).at(0).text, QStringLiteral("b asks for your mouse and keyboard (19 s)"));
    panel.tick(50000);
    outbox = panel.takeOutbox();
    QCOMPARE(int(outbox.size()), 1);
    QCOMPARE(outbox.at(0), controlAnswer(2, false));
    QCOMPARE(panel.pendingPrompts(), 0);
}

void TestPlankSharedScreen::cancelClosesPromptWithoutAnswer()
{
    Panel panel;
    panel.handle(hostMessage(R"({"t":"join_request","id":1,"account":"a"})"), 0);
    panel.handle(hostMessage(R"({"t":"join_request","id":2,"account":"b"})"), 0);
    panel.handle(hostMessage(R"({"t":"request_cancel","id":2})"), 10);
    QCOMPARE(panel.pendingPrompts(), 1);
    panel.handle(hostMessage(R"({"t":"request_cancel","id":1})"), 20);
    QCOMPARE(panel.pendingPrompts(), 0);
    panel.handle(hostMessage(R"({"t":"request_cancel","id":9})"), 30);
    panel.tick(60000);
    QVERIFY(panel.takeOutbox().isEmpty());
    QVERIFY(panel.rows(60000).isEmpty());
}

void TestPlankSharedScreen::controlPromptAnswersControl()
{
    Panel panel;
    panel.handle(hostMessage(R"({"t":"control_request","id":5,"account":"ana"})"), 0);
    const QVector<Panel::Row> rows = panel.rows(0);
    QCOMPARE(rows.at(0).text, QStringLiteral("ana asks for your mouse and keyboard (30 s)"));
    panel.activate(rows.at(0).buttons.at(0), 500);
    const QVector<QJsonObject> outbox = panel.takeOutbox();
    QCOMPARE(int(outbox.size()), 1);
    QCOMPARE(outbox.at(0), controlAnswer(5, true));
}

void TestPlankSharedScreen::doubleClickDoesNotAnswerNextPrompt()
{
    Panel panel;
    // Join request 1 and a control request from viewer 1: same id.
    panel.handle(hostMessage(R"({"t":"join_request","id":1,"account":"casey"})"), 0);
    panel.handle(hostMessage(R"({"t":"control_request","id":1,"account":"ana"})"), 100);
    const Panel::Button allow = panel.rows(1000).at(0).buttons.at(0);
    panel.activate(allow, 1000);
    QVector<QJsonObject> outbox = panel.takeOutbox();
    QCOMPARE(int(outbox.size()), 1);
    QCOMPARE(outbox.at(0), joinAnswer(1, true));

    // The second click of a double-click, and a late one, land on the
    // control prompt now at the front but were drawn for the join prompt.
    panel.activate(allow, 1100);
    panel.activate(allow, 5000);
    QVERIFY(panel.takeOutbox().isEmpty());
    QCOMPARE(panel.pendingPrompts(), 1);

    const Panel::Button next = panel.rows(5000).at(0).buttons.at(0);
    QVERIFY(next != allow);
    panel.activate(next, 5000);
    outbox = panel.takeOutbox();
    QCOMPARE(int(outbox.size()), 1);
    QCOMPARE(outbox.at(0), controlAnswer(1, true));
}

void TestPlankSharedScreen::cancelEndsOnlyTheMatchingKind()
{
    // A join cancel only comes when the host's 30 s ran out.
    Panel panel;
    panel.handle(hostMessage(R"({"t":"join_request","id":1,"account":"casey"})"), 0);
    panel.handle(hostMessage(R"({"t":"control_request","id":1,"account":"ana"})"), 10000);
    panel.handle(hostMessage(R"({"t":"request_cancel","id":1})"), 29500);
    QCOMPARE(panel.pendingPrompts(), 1);
    QCOMPARE(panel.rows(29500).at(0).text,
             QStringLiteral("ana asks for your mouse and keyboard (11 s)"));

    // A fresh join prompt with the same id as a cancelled control request
    // stays.
    Panel second;
    second.handle(hostMessage(R"({"t":"join_request","id":2,"account":"casey"})"), 0);
    second.handle(hostMessage(R"({"t":"control_request","id":2,"account":"ana"})"), 100);
    second.handle(hostMessage(R"({"t":"request_cancel","id":2})"), 2000);
    QCOMPARE(second.pendingPrompts(), 1);
    QVERIFY(second.rows(2000).at(0).text.startsWith(QStringLiteral("casey wants")));

    // An explicit kind decides.
    Panel third;
    third.handle(hostMessage(R"({"t":"join_request","id":3,"account":"casey"})"), 0);
    third.handle(hostMessage(R"({"t":"control_request","id":3,"account":"ana"})"), 100);
    third.handle(hostMessage(R"({"t":"request_cancel","id":3,"kind":"join"})"), 2000);
    QCOMPARE(third.pendingPrompts(), 1);
    QVERIFY(third.rows(2000).at(0).text.startsWith(QStringLiteral("ana asks")));
    third.handle(hostMessage(R"({"t":"request_cancel","id":3,"kind":"join"})"), 2100);
    QCOMPARE(third.pendingPrompts(), 1);
    third.handle(hostMessage(R"({"t":"request_cancel","id":3,"kind":"control"})"), 2200);
    QCOMPARE(third.pendingPrompts(), 0);
    QVERIFY(third.takeOutbox().isEmpty());
}

void TestPlankSharedScreen::clicksRightAfterThePromptChangesAreIgnored()
{
    Panel panel;
    panel.handle(hostMessage(R"({"t":"join_request","id":4,"account":"casey"})"), 1000);
    const Panel::Button deny = panel.rows(1000).at(0).buttons.at(1);
    panel.activate(deny, 1000 + PromptDebounceMs - 1);
    QVERIFY(panel.takeOutbox().isEmpty());
    panel.activate(deny, 1000 + PromptDebounceMs);
    const QVector<QJsonObject> outbox = panel.takeOutbox();
    QCOMPARE(int(outbox.size()), 1);
    QCOMPARE(outbox.at(0), joinAnswer(4, false));

    // A prompt that moves to the front after a cancel is debounced too.
    panel.handle(hostMessage(R"({"t":"join_request","id":5,"account":"a"})"), 2000);
    panel.handle(hostMessage(R"({"t":"join_request","id":6,"account":"b"})"), 2000);
    panel.handle(hostMessage(R"({"t":"request_cancel","id":5})"), 3000);
    const Panel::Button allow = panel.rows(3000).at(0).buttons.at(0);
    panel.activate(allow, 3100);
    QVERIFY(panel.takeOutbox().isEmpty());
    panel.activate(allow, 3400);
    QCOMPARE(panel.takeOutbox().at(0), joinAnswer(6, true));
}

void TestPlankSharedScreen::watchersListAndKick()
{
    Panel panel;
    QVERIFY(panel.slot().label.isEmpty());
    QVERIFY(!panel.takeRevealRequest());
    panel.handle(hostMessage(R"({"t":"watchers","viewers":[{"id":2,"account":"ana","control":false},{"id":5,"account":"bo","control":true}]})"),
                 0);
    QCOMPARE(panel.watcherCount(), 2);
    QVERIFY(panel.takeRevealRequest());
    QVERIFY(!panel.takeRevealRequest());
    QCOMPARE(panel.slot().label, QStringLiteral("2 watching"));
    // The list is closed until the indicator is clicked.
    QVERIFY(panel.rows(0).isEmpty());
    panel.activate(panel.slot(), 0);
    QVector<Panel::Row> rows = panel.rows(0);
    QCOMPARE(int(rows.size()), 2);
    QCOMPARE(rows.at(0).text, QStringLiteral("ana"));
    QCOMPARE(rows.at(1).text, QStringLiteral("bo - has control"));
    panel.activate(rows.at(1).buttons.at(0), 0);
    const QVector<QJsonObject> outbox = panel.takeOutbox();
    QCOMPARE(int(outbox.size()), 1);
    QCOMPARE(outbox.at(0), kick(5));

    // Fewer watchers do not reveal the toolbar; none hides the indicator.
    panel.handle(hostMessage(R"({"t":"watchers","viewers":[{"id":2,"account":"ana"}]})"), 0);
    QVERIFY(!panel.takeRevealRequest());
    QCOMPARE(panel.slot().label, QStringLiteral("1 watching"));
    panel.handle(hostMessage(R"({"t":"watchers","viewers":[]})"), 0);
    QVERIFY(panel.slot().label.isEmpty());
    QVERIFY(panel.rows(0).isEmpty());
}

void TestPlankSharedScreen::controllerTakeBackBanner()
{
    Panel panel;
    QVERIFY(panel.controllerHasControl());
    panel.handle(hostMessage(R"({"t":"watchers","viewers":[{"id":5,"account":"bo","control":true}]})"), 0);
    panel.handle(hostMessage(R"({"t":"control_state","has_control":false,"reason":"granted"})"), 0);
    QVERIFY(!panel.controllerHasControl());
    QVERIFY(panel.needsAttention());
    const QVector<Panel::Row> rows = panel.rows(0);
    QCOMPARE(int(rows.size()), 1);
    QCOMPARE(rows.at(0).text, QStringLiteral("bo has control"));
    QCOMPARE(int(rows.at(0).buttons.size()), 1);
    panel.activate(rows.at(0).buttons.at(0), 0);
    const QVector<QJsonObject> outbox = panel.takeOutbox();
    QCOMPARE(int(outbox.size()), 1);
    QCOMPARE(outbox.at(0), takeBack());

    panel.handle(hostMessage(R"({"t":"control_state","has_control":true,"reason":"taken_back"})"), 100);
    QVERIFY(panel.controllerHasControl());
    // Only a short notice is left, and it goes away.
    QCOMPARE(int(panel.rows(100).size()), 1);
    QVERIFY(panel.rows(100).at(0).buttons.isEmpty());
    panel.tick(100 + NoticeMs);
    QVERIFY(panel.rows(100 + NoticeMs).isEmpty());
    QVERIFY(!panel.needsAttention());
}

void TestPlankSharedScreen::viewerRequestsAndReleasesControl()
{
    Panel panel;
    panel.setViewer(true);
    QCOMPARE(panel.slot().label, QStringLiteral("Request control"));
    panel.activate(panel.slot(), 0);
    QVector<QJsonObject> outbox = panel.takeOutbox();
    QCOMPARE(int(outbox.size()), 1);
    QCOMPARE(outbox.at(0), controlRequest());
    QCOMPARE(panel.slot().label, QStringLiteral("Requested..."));
    QVERIFY(panel.slot().action == Panel::Action::None);
    // Clicking a pending request again sends nothing.
    panel.activate(Panel::Button {QStringLiteral("Request control"), Panel::Action::RequestControl, 0, false}, 10);
    QVERIFY(panel.takeOutbox().isEmpty());
    QVERIFY(!panel.viewerHasControl());

    panel.handle(hostMessage(R"({"t":"control_state","has_control":true,"reason":"granted"})"), 100);
    QVERIFY(panel.viewerHasControl());
    QCOMPARE(panel.slot().label, QStringLiteral("Give back"));
    panel.activate(panel.slot(), 200);
    outbox = panel.takeOutbox();
    QCOMPARE(int(outbox.size()), 1);
    QCOMPARE(outbox.at(0), controlRelease());
    // Input stops at once, before the host confirms.
    QVERIFY(!panel.viewerHasControl());
    panel.handle(hostMessage(R"({"t":"control_state","has_control":false,"reason":"released"})"), 300);
    QVERIFY(!panel.viewerHasControl());
    QVERIFY(panel.rows(300).isEmpty());
    QCOMPARE(panel.slot().label, QStringLiteral("Request control"));
}

void TestPlankSharedScreen::viewerLosesControlWithReason()
{
    Panel panel;
    panel.setViewer(true);
    panel.activate(panel.slot(), 0);
    panel.takeOutbox();
    panel.handle(hostMessage(R"({"t":"control_state","has_control":false,"reason":"denied"})"), 100);
    QCOMPARE(panel.slot().label, QStringLiteral("Request control"));
    QCOMPARE(int(panel.rows(100).size()), 1);
    QVERIFY(panel.rows(100).at(0).text.contains(QStringLiteral("declined")));

    panel.handle(hostMessage(R"({"t":"control_state","has_control":true,"reason":"granted"})"), 200);
    panel.handle(hostMessage(R"({"t":"control_state","has_control":false,"reason":"taken_back"})"), 300);
    QVERIFY(!panel.viewerHasControl());
    QCOMPARE(panel.rows(300).at(0).text, QStringLiteral("The owner took control back."));

    // A request the host never answers stops showing as pending.
    panel.tick(300 + NoticeMs);
    panel.activate(panel.slot(), 10000);
    panel.tick(10000 + ControlRequestTimeoutMs);
    QCOMPARE(panel.slot().label, QStringLiteral("Request control"));
}

void TestPlankSharedScreen::viewerIgnoresControllerMessages()
{
    Panel panel;
    panel.setViewer(true);
    panel.handle(hostMessage(R"({"t":"join_request","id":1,"account":"a"})"), 0);
    panel.handle(hostMessage(R"({"t":"watchers","viewers":[{"id":2,"account":"ana"}]})"), 0);
    QCOMPARE(panel.pendingPrompts(), 0);
    QCOMPARE(panel.watcherCount(), 0);
    QVERIFY(panel.rows(0).isEmpty());
    panel.tick(60000);
    QVERIFY(panel.takeOutbox().isEmpty());
}

QTEST_GUILESS_MAIN(TestPlankSharedScreen)
#include "test_planksharedscreen.moc"
