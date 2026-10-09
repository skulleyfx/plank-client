#include <QtTest>

#include "plankwin32cursorlogic.h"

using namespace PlankWin32CursorLogic;

class TestPlankWin32Cursor : public QObject
{
    Q_OBJECT

private slots:
    void keepsWireByteOrder();
    void clampsColourToAlpha();
    void rejectsBadImages();
    void arrowHasTipHotspot();
    void scalesWindowPointsToClientPixels();
    void rejectsEmptySizes();
    void placesHotspotOnPoint();
};

void TestPlankWin32Cursor::keepsWireByteOrder()
{
    // Little-endian ARGB8888 0x80402010 (A=0x80 R=0x40 G=0x20 B=0x10).
    const unsigned char wire[] = {0x10, 0x20, 0x40, 0x80,
                                  0x00, 0x00, 0x00, 0x00};
    Image image;
    QVERIFY(imageFromWire(wire, 2, 1, 1, 0, image));
    QCOMPARE(image.width, 2);
    QCOMPARE(image.height, 1);
    QCOMPARE(image.hotspotX, 1);
    QCOMPARE(image.hotspotY, 0);
    QCOMPARE(image.bgra.size(), size_t(8));
    QCOMPARE(int(image.bgra[0]), 0x10);
    QCOMPARE(int(image.bgra[1]), 0x20);
    QCOMPARE(int(image.bgra[2]), 0x40);
    QCOMPARE(int(image.bgra[3]), 0x80);
    QCOMPARE(int(image.bgra[7]), 0);
}

void TestPlankWin32Cursor::clampsColourToAlpha()
{
    const unsigned char wire[] = {0xff, 0x30, 0xff, 0x40};
    Image image;
    QVERIFY(imageFromWire(wire, 1, 1, 0, 0, image));
    QCOMPARE(int(image.bgra[0]), 0x40);
    QCOMPARE(int(image.bgra[1]), 0x30);
    QCOMPARE(int(image.bgra[2]), 0x40);
    QCOMPARE(int(image.bgra[3]), 0x40);
}

void TestPlankWin32Cursor::rejectsBadImages()
{
    const unsigned char wire[4] = {};
    Image image;
    QVERIFY(!imageFromWire(nullptr, 1, 1, 0, 0, image));
    QVERIFY(!imageFromWire(wire, 0, 1, 0, 0, image));
    QVERIFY(!imageFromWire(wire, 1, 1, 1, 0, image));
    QVERIFY(!imageFromWire(wire, 1, 1, 0, -1, image));
}

void TestPlankWin32Cursor::arrowHasTipHotspot()
{
    const Image arrow = fallbackArrow();
    QCOMPARE(arrow.width, 12);
    QCOMPARE(arrow.height, 19);
    QCOMPARE(arrow.hotspotX, 0);
    QCOMPARE(arrow.hotspotY, 0);
    QCOMPARE(arrow.bgra.size(), size_t(12 * 19 * 4));
    // Tip is opaque black, the far top-right corner is transparent.
    QCOMPARE(int(arrow.bgra[3]), 255);
    QCOMPARE(int(arrow.bgra[0]), 0);
    QCOMPARE(int(arrow.bgra[11 * 4 + 3]), 0);
    // An interior pixel is opaque white (premultiplied: colour == alpha).
    const size_t interior = (size_t(5) * 12 + 2) * 4;
    QCOMPARE(int(arrow.bgra[interior]), 255);
    QCOMPARE(int(arrow.bgra[interior + 3]), 255);
}

void TestPlankWin32Cursor::scalesWindowPointsToClientPixels()
{
    QPoint client;
    QVERIFY(clientPointFromWindowPoint(QPoint(100, 50), QSize(1280, 720),
                                       QSize(1280, 720), client));
    QCOMPARE(client, QPoint(100, 50));
    // 150% display scale with point-based SDL window sizes.
    QVERIFY(clientPointFromWindowPoint(QPoint(101, 51), QSize(1280, 720),
                                       QSize(1920, 1080), client));
    QCOMPARE(client, QPoint(152, 77));
}

void TestPlankWin32Cursor::rejectsEmptySizes()
{
    QPoint client;
    QVERIFY(!clientPointFromWindowPoint(QPoint(1, 1), QSize(0, 720),
                                        QSize(1920, 1080), client));
    QVERIFY(!clientPointFromWindowPoint(QPoint(1, 1), QSize(1280, 720),
                                        QSize(1920, 0), client));
}

void TestPlankWin32Cursor::placesHotspotOnPoint()
{
    Image image;
    image.hotspotX = 7;
    image.hotspotY = 3;
    QCOMPARE(overlayTopLeft(QPoint(500, 400), image), QPoint(493, 397));
    QCOMPARE(overlayTopLeft(QPoint(-1920, 10), fallbackArrow()),
             QPoint(-1920, 10));
}

QTEST_GUILESS_MAIN(TestPlankWin32Cursor)
#include "test_plankwin32cursor.moc"
