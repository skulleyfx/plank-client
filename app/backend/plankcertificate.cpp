#include "plankcertificate.h"

#include <QByteArray>
#include <QDateTime>
#include <QSslKey>

#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <memory>

namespace {
using X509Pointer = std::unique_ptr<X509, decltype(&X509_free)>;

X509Pointer parse(const QSslCertificate& certificate)
{
    const QByteArray bytes = certificate.toDer();
    const auto* cursor = reinterpret_cast<const unsigned char*>(bytes.constData());
    X509Pointer parsed(d2i_X509(nullptr, &cursor, bytes.size()), X509_free);
    if (cursor != reinterpret_cast<const unsigned char*>(bytes.constData() + bytes.size()))
        parsed.reset();
    return parsed;
}
}

namespace PlankCertificate {

bool acceptedProfile(const QSslCertificate& certificate, bool leaf)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (certificate.isNull() || certificate.publicKey().algorithm() != QSsl::Rsa ||
            certificate.publicKey().length() < 3072 ||
            certificate.effectiveDate() > now || certificate.expiryDate() <= now) return false;
    if (!leaf) return true;
    const auto alternativeNames = certificate.subjectAlternativeNames();
    return !alternativeNames.values(QSsl::DnsEntry).isEmpty() &&
            alternativeNames.values(QSsl::IpAddressEntry).isEmpty();
}

bool acceptedChain(const QList<QSslCertificate>& chain)
{
    // Same rule as the 1.1 client's HostTlsGuard::identityKey.
    if (chain.isEmpty() || chain.size() > 2 || !acceptedProfile(chain.first(), true) ||
            !acceptedProfile(chain.last(), false)) return false;
    const auto leaf = parse(chain.first()), root = parse(chain.last());
    if (!leaf || !root) return false;
    EVP_PKEY* rootKey = X509_get0_pubkey(root.get());
    if (!rootKey || X509_NAME_cmp(X509_get_subject_name(root.get()), X509_get_issuer_name(root.get())) ||
            X509_verify(root.get(), rootKey) != 1) return false;
    if (chain.size() == 1) return true;
    // OpenSSL checks the issuer signature, dates, CA constraints, key usage and
    // server purpose against this one candidate anchor. The anchor is not
    // trusted on its own; the caller pins the leaf.
    std::unique_ptr<X509_STORE, decltype(&X509_STORE_free)> store(X509_STORE_new(), X509_STORE_free);
    std::unique_ptr<X509_STORE_CTX, decltype(&X509_STORE_CTX_free)> context(X509_STORE_CTX_new(), X509_STORE_CTX_free);
    return store && context && X509_STORE_add_cert(store.get(), root.get()) == 1 &&
            X509_STORE_CTX_init(context.get(), store.get(), leaf.get(), nullptr) == 1 &&
            X509_STORE_CTX_set_purpose(context.get(), X509_PURPOSE_SSL_SERVER) == 1 &&
            X509_verify_cert(context.get()) == 1 && X509_check_ca(leaf.get()) == 0;
}

} // namespace PlankCertificate
