#pragma once

#include <QList>
#include <QSslCertificate>

// Shapes of TLS identity a PLANK host may present before any credentials are
// sent. Linux and Windows hosts present one self-signed machine certificate.
// PLANK 1.1 Mac hosts present a worker leaf signed directly by a self-signed
// machine authority (protocol/authentication.md). Nothing else is accepted:
// no intermediates, downloaded issuers or system CAs. Callers still pin the
// leaf digest wherever a request carries a bearer token or launches a stream.
namespace PlankCertificate {

// RSA >= 3072 and inside its validity dates; a leaf also needs a DNS SAN and
// no IP SAN.
bool acceptedProfile(const QSslCertificate& certificate, bool leaf);

// The peer chain, leaf first: [self-signed leaf] or [leaf, self-signed root]
// where the root issued the leaf for server authentication.
bool acceptedChain(const QList<QSslCertificate>& chain);

} // namespace PlankCertificate
