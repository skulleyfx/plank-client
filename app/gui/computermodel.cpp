#include "computermodel.h"
#include "backend/relaywakeclient.h"
#include "backend/nvhttp.h"
#include "backend/plankndipanel.h"
#include "settings/plankclientpolicy.h"

#include <utility>

#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QRunnable>
#include <QThreadPool>

#ifndef PLANK_VERSION_STR
#define PLANK_VERSION_STR "development"
#endif

namespace {
// True when the host reports a newer PLANK version than this client. Windows
// builds carry a fourth, build-revision field; it is compared only when this
// client has one, so clients of other platforms are not told to update for
// a Windows-only revision.
bool hostVersionIsNewer(const QString& hostVersion)
{
    const QRegularExpression pattern(QStringLiteral("^(\\d+(?:\\.\\d+)*)"));
    const auto hostMatch = pattern.match(hostVersion);
    const auto clientMatch = pattern.match(QString::fromLatin1(PLANK_VERSION_STR));
    if (!hostMatch.hasMatch() || !clientMatch.hasMatch()) {
        return false;
    }
    const QStringList host = hostMatch.captured(1).split('.');
    const QStringList client = clientMatch.captured(1).split('.');
    const int fields = client.size() >= 4 ? 4 : 3;
    for (int i = 0; i < fields; i++) {
        const int h = i < host.size() ? host.at(i).toInt() : 0;
        const int c = i < client.size() ? client.at(i).toInt() : 0;
        if (h != c) {
            return h > c;
        }
    }
    return false;
}

QString hostLayoutFromChoice(int choice)
{
    switch (choice) {
    case 0: return QString::fromLatin1(NvOutputTopology::MatchClientHostLayout);
    case 1: return QString::fromLatin1(NvOutputTopology::PhysicalHostLayout);
    case 2: return QString::fromLatin1(NvOutputTopology::SingleHostLayout);
    case 3: return QString::fromLatin1(NvOutputTopology::DualHorizontalHostLayout);
    default: return QString();
    }
}

QString virtualModeFromChoice(int choice)
{
    return NvOutputTopology::qualifiedVirtualModes().value(choice);
}
}

ComputerModel::ComputerModel(QObject* object)
    : QAbstractListModel(object) {}

void ComputerModel::initialize(ComputerManager* computerManager)
{
    m_ComputerManager = computerManager;
    connect(m_ComputerManager, &ComputerManager::computerStateChanged,
            this, &ComputerModel::handleComputerStateChanged);
    connect(m_ComputerManager, &ComputerManager::authenticationCompleted,
            this, &ComputerModel::handleAuthenticationCompleted);
    connect(m_ComputerManager, &ComputerManager::authenticationProgress,
            this, [this](NvComputer*, QString message) {
        emit authenticationProgress(message);
    });

    m_Computers = m_ComputerManager->getComputers();
}

QVariant ComputerModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    Q_ASSERT(index.row() < m_Computers.count());

    NvComputer* computer = m_Computers[index.row()];
    QReadLocker lock(&computer->lock);

    switch (role) {
    case NameRole:
        return computer->name;
    case OnlineRole:
        return computer->state == NvComputer::CS_ONLINE;
    case AuthorizedRole:
        return computer->authorizationState == NvComputer::AS_AUTHORIZED;
    case StatusUnknownRole:
        return computer->state == NvComputer::CS_UNKNOWN;
    case PlankHostVersionRole:
        return computer->plankHostMetadataVersion >= 1 ?
                    computer->plankHostVersion : QString();
    case ManualBookmarkRole:
        return computer->manualBookmark;
    case InUseRole:
        return computer->state == NvComputer::CS_ONLINE && computer->plankStreamActive;
    case SignedInUserRole:
        return computer->state == NvComputer::CS_ONLINE ? computer->plankSignedInUser : QString();
    case NdiStateRole:
        // Only while signed in to the host, which is when the endpoint accepts us.
        return computer->state == NvComputer::CS_ONLINE &&
                computer->authorizationState == NvComputer::AS_AUTHORIZED ?
                    computer->plankNdi : QString();
    case ClientUpdateRole:
        return computer->plankHostMetadataVersion >= 1 && hostVersionIsNewer(computer->plankHostVersion);
    case AddressRole:
        // New PLANK bookmarks have a durable manual address, but
        // workstation records created before bookmarks do not. Never expose
        // NvAddress's diagnostic <NULL> sentinel in the main workstation row.
        if (!computer->manualAddress.isNull()) {
            return computer->manualAddress.toString();
        }
        if (!computer->activeAddress.isNull()) {
            return computer->activeAddress.toString();
        }
        if (!computer->localAddress.isNull()) {
            return computer->localAddress.toString();
        }
        if (!computer->remoteAddress.isNull()) {
            return computer->remoteAddress.toString();
        }
        if (!computer->ipv6Address.isNull()) {
            return computer->ipv6Address.toString();
        }
        return QString();
    default:
        return QVariant();
    }
}

int ComputerModel::rowCount(const QModelIndex& parent) const
{
    // We should not return a count for valid index values,
    // only the parent (which will not have a "valid" index).
    if (parent.isValid()) {
        return 0;
    }

    return m_Computers.count();
}

QHash<int, QByteArray> ComputerModel::roleNames() const
{
    QHash<int, QByteArray> names;

    names[NameRole] = "name";
    names[OnlineRole] = "online";
    names[AuthorizedRole] = "authorized";
    names[StatusUnknownRole] = "statusUnknown";
    names[PlankHostVersionRole] = "plankHostVersion";
    names[ManualBookmarkRole] = "manualBookmark";
    names[AddressRole] = "address";
    names[InUseRole] = "inUse";
    names[SignedInUserRole] = "signedInUser";
    names[ClientUpdateRole] = "clientUpdateAvailable";
    names[NdiStateRole] = "ndiState";

    return names;
}

Session* ComputerModel::createSessionForPlankDesktop(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    for (NvApp& app : computer->appList) {
        if (app.name == QStringLiteral("Desktop")) {
            return new Session(computer, app, nullptr, m_ComputerManager);
        }
    }

    return nullptr;
}

int ComputerModel::plankScalingChoice(int computerIndex) const
{
    Q_ASSERT(computerIndex >= 0 && computerIndex < m_Computers.count());
    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    return computer->plankScalingMode == NvOutputTopology::NativeScalingMode ? 0 : 1;
}

int ComputerModel::plankVideoProfile(int computerIndex) const
{
    Q_ASSERT(computerIndex >= 0 && computerIndex < m_Computers.count());
    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    return computer->plankVideoProfile;
}

int ComputerModel::plankCaptureSource(int computerIndex) const
{
    Q_ASSERT(computerIndex >= 0 && computerIndex < m_Computers.count());
    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    return computer->plankCaptureSource;
}

QStringList ComputerModel::plankEncodingModes(int computerIndex) const
{
    Q_ASSERT(computerIndex >= 0 && computerIndex < m_Computers.count());
    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    return computer->plankEncodingModes;
}

QStringList ComputerModel::plankCaptureSources(int computerIndex) const
{
    Q_ASSERT(computerIndex >= 0 && computerIndex < m_Computers.count());
    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    return computer->plankCaptureSources;
}

QVariantList ComputerModel::plankProfileBitratesKbps(
        int computerIndex) const
{
    Q_ASSERT(computerIndex >= 0 && computerIndex < m_Computers.count());
    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    return StreamingPreferences::plankProfileBitratesToVariantList(
                computer->plankProfileBitratesKbps);
}

int ComputerModel::plankHostLayoutChoice(int computerIndex) const
{
    Q_ASSERT(computerIndex >= 0 && computerIndex < m_Computers.count());
    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    if (computer->plankHostLayout == NvOutputTopology::PhysicalHostLayout) {
        return 1;
    }
    if (computer->plankHostLayout == QStringLiteral("fixed")) {
        return 1; // Mac model: Match Client, then fixed virtual display.
    }
    if (computer->plankHostLayout == NvOutputTopology::SingleHostLayout) {
        return 2;
    }
    if (computer->plankHostLayout == NvOutputTopology::DualHorizontalHostLayout) {
        return 3;
    }
    return 0;
}

int ComputerModel::plankHostDisplayPolicy(int computerIndex) const
{
    Q_ASSERT(computerIndex >= 0 && computerIndex < m_Computers.count());
    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    if (computer->state != NvComputer::CS_ONLINE ||
            computer->authorizationState != NvComputer::AS_AUTHORIZED ||
            !computer->outputTopology.displayPolicyKnown()) {
        return -1;
    }
    return computer->outputTopology.allowedLayoutKinds.contains(
                NvOutputTopology::PhysicalHostLayout) ? 1 : 0;
}

int ComputerModel::plankVirtualMode1Choice(int computerIndex) const
{
    Q_ASSERT(computerIndex >= 0 && computerIndex < m_Computers.count());
    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    return NvOutputTopology::qualifiedVirtualModes().indexOf(
                computer->plankVirtualMode1);
}

int ComputerModel::plankVirtualMode2Choice(int computerIndex) const
{
    Q_ASSERT(computerIndex >= 0 && computerIndex < m_Computers.count());
    NvComputer* computer = m_Computers[computerIndex];
    QReadLocker lock(&computer->lock);
    return NvOutputTopology::qualifiedVirtualModes().indexOf(
                computer->plankVirtualMode2);
}

bool ComputerModel::editComputerBookmark(int computerIndex, QString address,
                                         QString nickname, int scalingChoice,
                                         int hostLayoutChoice,
                                         int virtualMode1Choice,
                                         int virtualMode2Choice,
                                         int videoProfile, int captureSource,
                                         const QVariantList& profileBitratesKbps)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) {
        return false;
    }

    NvComputer* computer = m_Computers[computerIndex];
    const QString hostLayout = hostLayoutFromChoice(hostLayoutChoice);
    const QString virtualMode1 = virtualModeFromChoice(virtualMode1Choice);
    const QString virtualMode2 = virtualModeFromChoice(virtualMode2Choice);
    if (hostLayout.isEmpty() || virtualMode1.isEmpty() || virtualMode2.isEmpty()) {
        return false;
    }
    if (scalingChoice < 0 || scalingChoice > 1) {
        return false;
    }
    const QString scalingMode = scalingChoice == 0 ?
                QString::fromLatin1(NvOutputTopology::NativeScalingMode) :
                QString::fromLatin1(NvOutputTopology::ScaledSpanMode);

    return m_ComputerManager->editManualBookmark(computer, std::move(address),
                                                  std::move(nickname), scalingMode,
                                                  hostLayout,
                                                  virtualMode1, virtualMode2,
                                                  videoProfile, captureSource,
                                                  profileBitratesKbps);
}

void ComputerModel::deleteComputer(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    beginRemoveRows(QModelIndex(), computerIndex, computerIndex);

    // m_Computer[computerIndex] will be deleted by this call
    m_ComputerManager->deleteHost(m_Computers[computerIndex]);

    // Remove the now invalid item
    m_Computers.removeAt(computerIndex);

    endRemoveRows();
}

void ComputerModel::renameComputer(int computerIndex, QString name)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    m_ComputerManager->renameHost(m_Computers[computerIndex], name);
}

bool ComputerModel::relayWakeEnabled() const
{
    return PlankClientPolicy().relayWakeEnabled();
}

void ComputerModel::requestRelayWake(int computerIndex)
{
    // Recheck policy at dispatch, even if an already-open menu is still visible.
    if (!relayWakeEnabled()) {
        emit relayWakeCompleted(tr("Wake PC is disabled by administrator policy."));
        return;
    }

    if (computerIndex < 0 || computerIndex >= m_Computers.count()) {
        emit relayWakeCompleted(tr("The selected workstation bookmark is unavailable."));
        return;
    }

    NvComputer* computer = m_Computers[computerIndex];
    QString address;
    {
        QReadLocker lock(&computer->lock);
        if (!computer->manualBookmark || computer->manualAddress.isNull()) {
            emit relayWakeCompleted(tr("Wake PC requires a manually configured bookmark."));
            return;
        }
        if (computer->state != NvComputer::CS_OFFLINE) {
            emit relayWakeCompleted(tr("Wake PC requires an offline workstation."));
            return;
        }
        address = computer->manualAddress.address();
    }

    auto* request = new RelayWakeClient(
                address, PlankClientPolicy().relayWakePort(), this);
    connect(request, &RelayWakeClient::completed,
            this, &ComputerModel::relayWakeCompleted);
    request->start();
}

void ComputerModel::authenticateComputer(int computerIndex, QString username,
                                         QString password)
{
    Q_ASSERT(computerIndex < m_Computers.count());
    m_ComputerManager->authenticateHost(m_Computers[computerIndex],
                                        std::move(username), std::move(password));
}

QString ComputerModel::lastUsername(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());
    return m_ComputerManager->lastPlankUsername(m_Computers[computerIndex]);
}

void ComputerModel::handleAuthenticationCompleted(NvComputer*, QString error)
{
    emit authenticationCompleted(error.isEmpty() ? QVariant() : error);
}

void ComputerModel::handleComputerStateChanged(NvComputer* computer)
{
    QVector<NvComputer*> newComputerList = m_ComputerManager->getComputers();

    // Reset the model if the structural layout of the list has changed
    if (m_Computers != newComputerList) {
        beginResetModel();
        m_Computers = newComputerList;
        endResetModel();
    }
    else {
        // Let the view know that this specific computer changed
        int index = m_Computers.indexOf(computer);
        emit dataChanged(createIndex(index, 0), createIndex(index, 0));
    }
}

void ComputerModel::switchNdi(int computerIndex, bool on)
{
    startNdiRequest(computerIndex, PlankNdi::stateArgument(on), false);
}

// Menu opened: if the cached serverinfo said nothing about NDI (it is only in
// authorized serverinfo), ask the host once, quietly.
void ComputerModel::refreshNdi(int computerIndex)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) {
        return;
    }
    NvComputer* computer = m_Computers[computerIndex];
    {
        QReadLocker lock(&computer->lock);
        if (!computer->plankNdi.isEmpty()) {
            return;
        }
    }
    startNdiRequest(computerIndex, QString(), true);
}

void ComputerModel::startNdiRequest(int computerIndex, const QString& stateArgument, bool quiet)
{
    if (computerIndex < 0 || computerIndex >= m_Computers.count()) {
        return;
    }

    NvComputer* computer = m_Computers[computerIndex];
    NvAddress address;
    QString token;
    {
        QReadLocker lock(&computer->lock);
        if (computer->state != NvComputer::CS_ONLINE ||
                computer->authorizationState != NvComputer::AS_AUTHORIZED ||
                computer->sessionToken.isEmpty()) {
            if (!quiet) {
                emit ndiSwitchCompleted(tr("Sign in to this workstation first"));
            }
            return;
        }
        address = computer->activeAddress;
        token = computer->sessionToken;
    }
    if (address.isNull()) {
        if (!quiet) {
            emit ndiSwitchCompleted(tr("The workstation is not reachable"));
        }
        return;
    }

    // The request blocks until the host answers, so it runs off the UI thread.
    QPointer<ComputerModel> self(this);
    class Task : public QRunnable
    {
    public:
        Task(QPointer<ComputerModel> model, NvComputer* computer,
             NvAddress address, QString token, QString stateArgument, bool quiet) :
            m_Model(std::move(model)), m_Computer(computer),
            m_Address(std::move(address)), m_Token(std::move(token)),
            m_StateArgument(std::move(stateArgument)), m_Quiet(quiet) {}

        void run() override
        {
            QString state;
            QString source;
            QString error;
            try {
                NvHTTP http(m_Address);
                http.setPlankSessionToken(m_Token);
                const QJsonObject result = http.plankNdi(m_StateArgument);
                state = result.value(QStringLiteral("state")).toString();
                source = result.value(QStringLiteral("source")).toString();
            } catch (const GfeHttpResponseException& e) {
                error = PlankNdi::failureMessage(e.getStatusCode());
            } catch (const QtNetworkReplyException& e) {
                error = PlankNdi::failureMessage(NvHTTP::httpStatusForNetworkError(e.getError()));
            }

            QPointer<ComputerModel> model = m_Model;
            NvComputer* computer = m_Computer;
            const bool quiet = m_Quiet;
            if (model) {
                QMetaObject::invokeMethod(model.data(), [model, computer, state, source, error, quiet]() {
                    if (model) {
                        model->finishNdiSwitch(computer, state, source, error, quiet);
                    }
                }, Qt::QueuedConnection);
            }
        }

    private:
        QPointer<ComputerModel> m_Model;
        NvComputer* m_Computer;
        NvAddress m_Address;
        QString m_Token;
        QString m_StateArgument;
        bool m_Quiet;
    };
    QThreadPool::globalInstance()->start(new Task(self, computer, address, token, stateArgument, quiet));
}

void ComputerModel::finishNdiSwitch(NvComputer* computer, const QString& state,
                                    const QString& source, const QString& error,
                                    bool quiet)
{
    const int row = m_Computers.indexOf(computer);
    if (row >= 0 && error.isEmpty()) {
        {
            QWriteLocker lock(&computer->lock);
            computer->plankNdi = state;
            computer->plankNdiSource = source;
        }
        emit dataChanged(index(row), index(row), {NdiStateRole});
    }
    if (quiet) {
        return;  // a background look; no dialog either way
    }
    if (error.isEmpty()) {
        emit ndiSwitchCompleted(QVariant());
    } else {
        emit ndiSwitchCompleted(error);
    }
}
