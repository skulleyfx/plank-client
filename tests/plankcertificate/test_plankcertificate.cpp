#include <QtTest>

#include "plankcertificate.h"

// Fixtures hold public certificates only (100-year validity); their private
// keys were discarded after signing.
class TestPlankCertificate : public QObject
{
    Q_OBJECT

private slots:
    void acceptsSelfSignedMachineCertificate();
    void acceptsMacWorkerLeafWithItsAuthority();
    void rejectsOtherChainShapes();
    void rejectsWeakOrAddressCertificates();
};

static QSslCertificate fixture(const char* name)
{
    QFile file(QStringLiteral(PLANK_CERTIFICATE_FIXTURES "/") + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly)) return QSslCertificate();
    return QSslCertificate(file.readAll(), QSsl::Pem);
}

void TestPlankCertificate::acceptsSelfSignedMachineCertificate()
{
    const auto machine = fixture("linux-self-signed.pem");
    QVERIFY(!machine.isNull());
    QVERIFY(PlankCertificate::acceptedChain({machine}));
}

void TestPlankCertificate::acceptsMacWorkerLeafWithItsAuthority()
{
    const auto leaf = fixture("mac-leaf.pem"), authority = fixture("mac-authority.pem");
    QVERIFY(!leaf.isNull() && !authority.isNull());
    QVERIFY(PlankCertificate::acceptedChain({leaf, authority}));
}

void TestPlankCertificate::rejectsOtherChainShapes()
{
    const auto leaf = fixture("mac-leaf.pem"), authority = fixture("mac-authority.pem");
    QVERIFY(!PlankCertificate::acceptedChain({}));
    // A worker leaf alone is not self-signed.
    QVERIFY(!PlankCertificate::acceptedChain({leaf}));
    // The authority has no DNS SAN, so it cannot be the presented leaf.
    QVERIFY(!PlankCertificate::acceptedChain({authority}));
    QVERIFY(!PlankCertificate::acceptedChain({authority, leaf}));
    // Same subject name, different key: the signature must verify.
    QVERIFY(!PlankCertificate::acceptedChain({leaf, fixture("other-authority.pem")}));
    // Server authentication is required of the leaf.
    QVERIFY(!PlankCertificate::acceptedChain({fixture("mac-leaf-client-auth.pem"), authority}));
    // No intermediates.
    QVERIFY(!PlankCertificate::acceptedChain({leaf, authority, authority}));
}

void TestPlankCertificate::rejectsWeakOrAddressCertificates()
{
    QVERIFY(!PlankCertificate::acceptedChain({fixture("weak-self-signed.pem")}));
    QVERIFY(!PlankCertificate::acceptedChain({fixture("ip-san-self-signed.pem")}));
    QVERIFY(!PlankCertificate::acceptedProfile(QSslCertificate(), false));
}

QTEST_GUILESS_MAIN(TestPlankCertificate)
#include "test_plankcertificate.moc"
