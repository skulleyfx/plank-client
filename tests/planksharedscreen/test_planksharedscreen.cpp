#include <QtTest>

#include "planksharedscreen.h"

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

QTEST_GUILESS_MAIN(TestPlankSharedScreen)
#include "test_planksharedscreen.moc"
