#include <QtTest>

#include "plankndipanel.h"

using namespace PlankNdi;

class TestPlankNdiPanel : public QObject
{
    Q_OBJECT

private slots:
    void onlyOffAndOnShowTheButton_data();
    void onlyOffAndOnShowTheButton();
    void labelAndTooltip();
    void clickQueuesOneSwitch();
    void successUpdatesState();
    void failureKeepsStateAndShowsNoticeBriefly();
    void failedRefreshIsQuiet();
    void refreshWhenStale();
    void hostValuesDoNotOverrideARequestUnderWay();
    void failureMessages();
};

void TestPlankNdiPanel::onlyOffAndOnShowTheButton_data()
{
    QTest::addColumn<QString>("state");
    QTest::addColumn<bool>("visible");
    QTest::newRow("absent") << QString() << false;
    QTest::newRow("unavailable") << QStringLiteral("unavailable") << false;
    QTest::newRow("always") << QStringLiteral("always") << false;
    QTest::newRow("unknown") << QStringLiteral("maybe") << false;
    QTest::newRow("off") << QStringLiteral("off") << true;
    QTest::newRow("on") << QStringLiteral("on") << true;
    QTest::newRow("padded") << QStringLiteral(" On ") << true;
}

void TestPlankNdiPanel::onlyOffAndOnShowTheButton()
{
    QFETCH(QString, state);
    QFETCH(bool, visible);
    Panel panel;
    panel.setHost(state, QString());
    QCOMPARE(panel.visible(), visible);
}

void TestPlankNdiPanel::labelAndTooltip()
{
    Panel panel;
    panel.setHost(QStringLiteral("off"), QString());
    QCOMPARE(panel.label(), QStringLiteral("NDI"));
    QVERIFY(!panel.on());
    panel.setHost(QStringLiteral("on"), QStringLiteral("BOX - Screen 1"));
    QCOMPARE(panel.label(), QStringLiteral("NDI ON"));
    QVERIFY(panel.on());
    QVERIFY(panel.tooltip().contains(QStringLiteral("BOX - Screen 1")));
}

void TestPlankNdiPanel::clickQueuesOneSwitch()
{
    Panel panel;
    Request request;
    // Hidden: a click does nothing.
    panel.requestToggle();
    QVERIFY(!panel.takeRequest(1000000, request));

    panel.setHost(QStringLiteral("off"), QString());
    panel.requestToggle();
    panel.requestToggle();  // a double click is one request
    QVERIFY(panel.pending());
    QVERIFY(panel.takeRequest(1000000, request));
    QCOMPARE(request.kind, Request::Switch);
    QVERIFY(request.on);
    QVERIFY(!panel.takeRequest(1000000, request));  // busy until finish
}

void TestPlankNdiPanel::successUpdatesState()
{
    Panel panel;
    Request request;
    panel.setHost(QStringLiteral("off"), QString());
    panel.requestToggle();
    QVERIFY(panel.takeRequest(1000000, request));
    panel.finish(request, true, QStringLiteral("on"), QStringLiteral("S"), QString(), 1000100);
    QVERIFY(panel.on());
    QVERIFY(!panel.pending());
    QCOMPARE(panel.source(), QStringLiteral("S"));
    // Clicking again asks to turn it off.
    panel.requestToggle();
    QVERIFY(panel.takeRequest(1000200, request));
    QCOMPARE(request.kind, Request::Switch);
    QVERIFY(!request.on);
    // The host answered "unavailable" (runtime removed): the button goes away.
    panel.finish(request, true, QStringLiteral("unavailable"), QString(), QString(), 1000300);
    QVERIFY(!panel.visible());
}

void TestPlankNdiPanel::failureKeepsStateAndShowsNoticeBriefly()
{
    Panel panel;
    Request request;
    panel.setHost(QStringLiteral("off"), QString());
    panel.requestToggle();
    QVERIFY(panel.takeRequest(1000000, request));
    panel.finish(request, false, QString(), QString(), failureMessage(403), 1000100);
    QVERIFY(!panel.on());
    QVERIFY(panel.visible());
    QVERIFY(!panel.notice(1000200).isEmpty());
    QVERIFY(panel.notice(1000100 + NoticeMs).isEmpty());
}

void TestPlankNdiPanel::failedRefreshIsQuiet()
{
    Panel panel;
    Request request;
    panel.setHost(QStringLiteral("on"), QString());
    QVERIFY(panel.takeRequest(RefreshMs, request));
    QCOMPARE(request.kind, Request::Refresh);
    panel.finish(request, false, QString(), QString(), QStringLiteral("x"), RefreshMs + 10);
    QVERIFY(panel.notice(RefreshMs + 20).isEmpty());
    QVERIFY(panel.on());
}

void TestPlankNdiPanel::refreshWhenStale()
{
    Panel panel;
    Request request;
    panel.setHost(QStringLiteral("on"), QString());
    QVERIFY(!panel.takeRequest(RefreshMs - 1, request));
    QVERIFY(panel.takeRequest(RefreshMs, request));
    panel.finish(request, true, QStringLiteral("off"), QString(), QString(), RefreshMs + 5);
    QVERIFY(!panel.on());  // switched off automatically on the host
    QVERIFY(!panel.takeRequest(RefreshMs + 100, request));
    QVERIFY(panel.takeRequest(2 * RefreshMs + 5, request));
}

void TestPlankNdiPanel::hostValuesDoNotOverrideARequestUnderWay()
{
    Panel panel;
    Request request;
    panel.setHost(QStringLiteral("off"), QString());
    panel.requestToggle();
    panel.setHost(QStringLiteral("off"), QString());
    QVERIFY(panel.pending());
    QVERIFY(panel.takeRequest(1000000, request));
    panel.setHost(QStringLiteral("unavailable"), QString());
    QVERIFY(panel.visible());
}

void TestPlankNdiPanel::failureMessages()
{
    QVERIFY(failureMessage(403).contains(QStringLiteral("owner")));
    QVERIFY(failureMessage(409).contains(QStringLiteral("not available")));
    QVERIFY(!failureMessage(0).isEmpty());
    QCOMPARE(stateArgument(true), QStringLiteral("on"));
    QCOMPARE(stateArgument(false), QStringLiteral("off"));
}

QTEST_GUILESS_MAIN(TestPlankNdiPanel)
#include "test_plankndipanel.moc"
