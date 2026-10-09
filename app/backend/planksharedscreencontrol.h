#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>
#include <QVector>

#include <cmath>
#include <cstdint>

// Shared screen, slices 2 and 3: the owner's consent, the list of watchers and
// handing the mouse and keyboard to a viewer. The host and this client
// exchange small JSON objects on native event 6 (shared-screen tech design,
// section 11). Pure logic only, so it can be unit tested without SDL: the
// toolbar draws Panel's rows and the session sends Panel's outbox.
namespace PlankSharedScreen {

// Native event type for shared-screen messages, both directions. Like
// clipboard text (event 5) it is not in the shared transport header.
constexpr std::uint16_t EventType = 6;
// Largest message either side accepts.
constexpr int MaxMessageBytes = 4096;
// The owner has this long to answer; silence is a refusal.
constexpr quint64 AnswerTimeoutMs = 30000;
// How long a one-line notice ("The owner took control back") stays up.
constexpr quint64 NoticeMs = 6000;
// A control request the host never answers stops showing as pending.
constexpr quint64 ControlRequestTimeoutMs = 45000;

struct Watcher {
    quint32 id = 0;
    QString account;
    bool control = false;
};

struct HostMessage {
    enum Type {
        Unknown,
        JoinRequest,
        ControlRequest,
        RequestCancel,
        Watchers,
        ControlState,
    };
    Type type = Unknown;
    // The "t" value, so an unknown type can be logged.
    QString name;
    quint32 id = 0;
    QString account;
    QVector<Watcher> watchers;
    bool hasControl = false;
    QString reason;
};

// Request and viewer ids are positive 32-bit integers.
inline bool readId(const QJsonObject& object, quint32& id)
{
    const QJsonValue value = object.value(QStringLiteral("id"));
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!(number >= 1.0 && number <= 4294967295.0) ||
            std::floor(number) != number) {
        return false;
    }
    id = static_cast<quint32>(number);
    return true;
}

/**
 * @brief Decode one host message.
 *
 * @return false when the payload is malformed (not a JSON object, too large,
 *         missing or mistyped fields). A well-formed message of a type this
 *         client does not know returns true with type Unknown, so a newer
 *         host never breaks an older client.
 */
inline bool parseHostMessage(const QByteArray& bytes, HostMessage& message)
{
    if (bytes.isEmpty() || bytes.size() > MaxMessageBytes) {
        return false;
    }
    QJsonParseError error {};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    const QJsonObject object = document.object();
    const QJsonValue typeValue = object.value(QStringLiteral("t"));
    if (!typeValue.isString()) {
        return false;
    }

    HostMessage parsed;
    parsed.name = typeValue.toString();
    if (parsed.name == QLatin1String("join_request") ||
            parsed.name == QLatin1String("control_request")) {
        parsed.type = parsed.name == QLatin1String("join_request") ?
                    HostMessage::JoinRequest : HostMessage::ControlRequest;
        const QJsonValue account = object.value(QStringLiteral("account"));
        if (!readId(object, parsed.id) || !account.isString()) {
            return false;
        }
        parsed.account = account.toString();
    }
    else if (parsed.name == QLatin1String("request_cancel")) {
        parsed.type = HostMessage::RequestCancel;
        if (!readId(object, parsed.id)) {
            return false;
        }
    }
    else if (parsed.name == QLatin1String("watchers")) {
        parsed.type = HostMessage::Watchers;
        const QJsonValue viewers = object.value(QStringLiteral("viewers"));
        if (!viewers.isArray()) {
            return false;
        }
        const QJsonArray list = viewers.toArray();
        for (const QJsonValue& entry : list) {
            if (!entry.isObject()) {
                return false;
            }
            const QJsonObject viewer = entry.toObject();
            Watcher watcher;
            const QJsonValue account = viewer.value(QStringLiteral("account"));
            const QJsonValue control = viewer.value(QStringLiteral("control"));
            if (!readId(viewer, watcher.id) || !account.isString() ||
                    !(control.isUndefined() || control.isBool())) {
                return false;
            }
            watcher.account = account.toString();
            watcher.control = control.toBool(false);
            parsed.watchers.append(watcher);
        }
    }
    else if (parsed.name == QLatin1String("control_state")) {
        parsed.type = HostMessage::ControlState;
        const QJsonValue hasControl = object.value(QStringLiteral("has_control"));
        const QJsonValue reason = object.value(QStringLiteral("reason"));
        if (!hasControl.isBool() ||
                !(reason.isUndefined() || reason.isString())) {
            return false;
        }
        parsed.hasControl = hasControl.toBool();
        parsed.reason = reason.toString();
    }

    message = parsed;
    return true;
}

// Client to host messages.
inline QJsonObject joinAnswer(quint32 id, bool allow)
{
    return QJsonObject {
        {QStringLiteral("t"), QStringLiteral("join_answer")},
        {QStringLiteral("id"), static_cast<double>(id)},
        {QStringLiteral("allow"), allow},
    };
}

inline QJsonObject controlAnswer(quint32 id, bool allow)
{
    return QJsonObject {
        {QStringLiteral("t"), QStringLiteral("control_answer")},
        {QStringLiteral("id"), static_cast<double>(id)},
        {QStringLiteral("allow"), allow},
    };
}

inline QJsonObject kick(quint32 id)
{
    return QJsonObject {
        {QStringLiteral("t"), QStringLiteral("kick")},
        {QStringLiteral("id"), static_cast<double>(id)},
    };
}

inline QJsonObject takeBack()
{
    return QJsonObject {{QStringLiteral("t"), QStringLiteral("take_back")}};
}

inline QJsonObject controlRequest()
{
    return QJsonObject {{QStringLiteral("t"), QStringLiteral("control_request")}};
}

inline QJsonObject controlRelease()
{
    return QJsonObject {{QStringLiteral("t"), QStringLiteral("control_release")}};
}

// The event 6 payload: compact UTF-8 JSON.
inline QByteArray encode(const QJsonObject& message)
{
    return QJsonDocument(message).toJson(QJsonDocument::Compact);
}

/**
 * @brief A clear message for a host refusal of a viewer.
 *
 * The host answers a refused viewer with a short reason in its launch or
 * native setup error; anything else is shown as the host sent it.
 */
inline QString viewerRefusalText(const QString& hostMessage)
{
    if (hostMessage.startsWith(QLatin1String("The owner declined"), Qt::CaseInsensitive)) {
        return QStringLiteral("The person at this workstation declined your request to watch.");
    }
    if (hostMessage.startsWith(QLatin1String("The owner did not answer"), Qt::CaseInsensitive)) {
        return QStringLiteral("The person at this workstation did not answer your request to watch. Try again in a moment.");
    }
    if (hostMessage.startsWith(QLatin1String("The owner's client cannot approve viewers"),
                               Qt::CaseInsensitive)) {
        return QStringLiteral("The person at this workstation is using a PLANK version that cannot approve viewers. "
                              "Ask them to update PLANK, then try again.");
    }
    return hostMessage;
}

/**
 * @brief Shared-screen state shown in the toolbar of one session.
 *
 * Controller: queued watch and control requests (one prompt at a time, a
 * 30 second countdown, silence is a refusal), the "N watching" list and the
 * "has control - Take back" banner. Viewer: the Request control / Give back
 * button and whether its input currently reaches the desktop. Everything the
 * user decides becomes a message in the outbox.
 */
class Panel
{
public:
    enum class Action {
        None,
        Allow,
        Deny,
        TakeBack,
        Kick,
        ToggleWatchers,
        RequestControl,
        ReleaseControl,
    };

    struct Button {
        QString label;
        Action action = Action::None;
        quint32 id = 0;
        // Drawn as the emphasised choice.
        bool primary = false;

        bool operator==(const Button& other) const
        {
            return action == other.action && id == other.id &&
                    label == other.label;
        }
        bool operator!=(const Button& other) const
        {
            return !(*this == other);
        }
    };

    struct Row {
        QString text;
        QVector<Button> buttons;
        // Needs an answer; drawn highlighted.
        bool attention = false;
    };

    enum class ViewerControl {
        None,
        Requested,
        Holding,
    };

    void setViewer(bool viewer)
    {
        m_Viewer = viewer;
    }

    bool viewer() const
    {
        return m_Viewer;
    }

    void handle(const HostMessage& message, quint64 now)
    {
        switch (message.type) {
        case HostMessage::JoinRequest:
        case HostMessage::ControlRequest: {
            if (m_Viewer) {
                return;
            }
            const Prompt::Kind kind = message.type == HostMessage::JoinRequest ?
                        Prompt::Join : Prompt::Control;
            for (Prompt& prompt : m_Prompts) {
                if (prompt.kind == kind && prompt.id == message.id) {
                    // A repeat of a request already waiting for an answer.
                    prompt.account = message.account;
                    return;
                }
            }
            Prompt prompt;
            prompt.kind = kind;
            prompt.id = message.id;
            prompt.account = message.account;
            // The host's own 30 seconds started when it sent this.
            prompt.deadline = now + AnswerTimeoutMs;
            m_Prompts.append(prompt);
            break;
        }
        case HostMessage::RequestCancel:
            // Join and control requests share no id space guarantee, so a
            // cancel ends any request with that id. The host only cancels
            // requests it still holds open.
            for (int index = static_cast<int>(m_Prompts.size()) - 1; index >= 0; --index) {
                if (m_Prompts.at(index).id == message.id) {
                    m_Prompts.remove(index);
                }
            }
            break;
        case HostMessage::Watchers:
            if (m_Viewer) {
                return;
            }
            if (message.watchers.size() > m_Watchers.size()) {
                m_RevealRequested = true;
            }
            m_Watchers = message.watchers;
            if (m_Watchers.isEmpty()) {
                m_WatchersOpen = false;
            }
            break;
        case HostMessage::ControlState:
            if (m_Viewer) {
                const bool hadControl = m_ViewerControl == ViewerControl::Holding;
                m_ViewerControl = message.hasControl ?
                            ViewerControl::Holding : ViewerControl::None;
                if (message.hasControl && !hadControl) {
                    setNotice(QStringLiteral("You have the mouse and keyboard. Click Give back when you are done."),
                              now);
                }
                else if (!message.hasControl) {
                    const QString reason = viewerReasonText(message.reason);
                    if (reason.isEmpty()) {
                        clearNotice();
                    }
                    else {
                        setNotice(reason, now);
                    }
                }
            }
            else {
                const bool hadControl = m_ControllerHasControl;
                m_ControllerHasControl = message.hasControl;
                if (message.hasControl && !hadControl) {
                    setNotice(QStringLiteral("You have the mouse and keyboard again."), now);
                }
            }
            break;
        case HostMessage::Unknown:
            break;
        }
    }

    // Expire unanswered prompts (a refusal), notices and a stale request.
    void tick(quint64 now)
    {
        while (!m_Prompts.isEmpty() && now >= m_Prompts.first().deadline) {
            answer(false);
        }
        if (m_NoticeDeadline != 0 && now >= m_NoticeDeadline) {
            clearNotice();
        }
        if (m_ViewerControl == ViewerControl::Requested &&
                now >= m_ControlRequestDeadline) {
            m_ViewerControl = ViewerControl::None;
        }
    }

    // What the toolbar shows under its main row, top to bottom.
    QVector<Row> rows(quint64 now) const
    {
        QVector<Row> rows;
        if (!m_Prompts.isEmpty()) {
            const Prompt& prompt = m_Prompts.first();
            const quint64 remainingMs = prompt.deadline > now ? prompt.deadline - now : 0;
            const int seconds = static_cast<int>((remainingMs + 999) / 1000);
            Row row;
            row.attention = true;
            row.text = prompt.kind == Prompt::Join ?
                        QStringLiteral("%1 wants to watch your screen (%2 s)") :
                        QStringLiteral("%1 asks for your mouse and keyboard (%2 s)");
            row.text = row.text.arg(displayName(prompt.account)).arg(seconds);
            if (m_Prompts.size() > 1) {
                row.text += QStringLiteral(" - %1 more waiting")
                        .arg(static_cast<int>(m_Prompts.size()) - 1);
            }
            row.buttons.append(Button {QStringLiteral("Allow"), Action::Allow, prompt.id, true});
            row.buttons.append(Button {QStringLiteral("Deny"), Action::Deny, prompt.id, false});
            rows.append(row);
        }
        if (!m_Viewer && !m_ControllerHasControl) {
            Row row;
            row.attention = true;
            row.text = QStringLiteral("%1 has control").arg(displayName(controlHolder()));
            row.buttons.append(Button {QStringLiteral("Take back"), Action::TakeBack, 0, true});
            rows.append(row);
        }
        if (!m_Viewer && m_WatchersOpen) {
            for (const Watcher& watcher : m_Watchers) {
                Row row;
                row.text = displayName(watcher.account);
                if (watcher.control) {
                    row.text += QStringLiteral(" - has control");
                }
                row.buttons.append(Button {QStringLiteral("Remove"), Action::Kick, watcher.id, false});
                rows.append(row);
            }
        }
        if (!m_Notice.isEmpty()) {
            Row row;
            row.text = m_Notice;
            rows.append(row);
        }
        return rows;
    }

    // The button in the toolbar's main row. An empty label hides it; Action
    // None shows it disabled.
    Button slot() const
    {
        if (m_Viewer) {
            switch (m_ViewerControl) {
            case ViewerControl::None:
                return Button {QStringLiteral("Request control"), Action::RequestControl, 0, false};
            case ViewerControl::Requested:
                return Button {QStringLiteral("Requested..."), Action::None, 0, false};
            case ViewerControl::Holding:
                return Button {QStringLiteral("Give back"), Action::ReleaseControl, 0, true};
            }
            return Button {};
        }
        if (m_Watchers.isEmpty()) {
            return Button {};
        }
        return Button {QStringLiteral("%1 watching").arg(static_cast<int>(m_Watchers.size())),
                       Action::ToggleWatchers, 0, m_WatchersOpen};
    }

    // Carry out a button the user clicked.
    void activate(const Button& button, quint64 now)
    {
        switch (button.action) {
        case Action::Allow:
        case Action::Deny:
            if (!m_Prompts.isEmpty() && m_Prompts.first().id == button.id) {
                answer(button.action == Action::Allow);
            }
            break;
        case Action::TakeBack:
            if (!m_Viewer) {
                m_Outbox.append(takeBack());
            }
            break;
        case Action::Kick:
            if (!m_Viewer) {
                m_Outbox.append(kick(button.id));
            }
            break;
        case Action::ToggleWatchers:
            m_WatchersOpen = !m_WatchersOpen && !m_Watchers.isEmpty();
            break;
        case Action::RequestControl:
            if (m_Viewer && m_ViewerControl == ViewerControl::None) {
                m_Outbox.append(controlRequest());
                m_ViewerControl = ViewerControl::Requested;
                m_ControlRequestDeadline = now + ControlRequestTimeoutMs;
                clearNotice();
            }
            break;
        case Action::ReleaseControl:
            if (m_Viewer && m_ViewerControl == ViewerControl::Holding) {
                m_Outbox.append(controlRelease());
                // Stop sending input now rather than when the host confirms.
                m_ViewerControl = ViewerControl::None;
                clearNotice();
            }
            break;
        case Action::None:
            break;
        }
    }

    void closeWatchers()
    {
        m_WatchersOpen = false;
    }

    // True while something waits for the user or a notice is up; the toolbar
    // stays on screen meanwhile.
    bool needsAttention() const
    {
        return !m_Prompts.isEmpty() || !m_Notice.isEmpty() ||
                (!m_Viewer && !m_ControllerHasControl);
    }

    // True once after the number of watchers went up, so the toolbar can
    // show itself briefly.
    bool takeRevealRequest()
    {
        const bool requested = m_RevealRequested;
        m_RevealRequested = false;
        return requested;
    }

    bool viewerHasControl() const
    {
        return m_Viewer && m_ViewerControl == ViewerControl::Holding;
    }

    ViewerControl viewerControl() const
    {
        return m_ViewerControl;
    }

    bool controllerHasControl() const
    {
        return m_ControllerHasControl;
    }

    int watcherCount() const
    {
        return static_cast<int>(m_Watchers.size());
    }

    int pendingPrompts() const
    {
        return static_cast<int>(m_Prompts.size());
    }

    QVector<QJsonObject> takeOutbox()
    {
        QVector<QJsonObject> outbox;
        outbox.swap(m_Outbox);
        return outbox;
    }

private:
    struct Prompt {
        enum Kind {
            Join,
            Control,
        };
        Kind kind = Join;
        quint32 id = 0;
        QString account;
        quint64 deadline = 0;
    };

    static QString displayName(const QString& account)
    {
        return account.isEmpty() ? QStringLiteral("Someone") : account;
    }

    static QString viewerReasonText(const QString& reason)
    {
        if (reason == QLatin1String("released")) {
            // This client gave control back itself.
            return QString();
        }
        if (reason == QLatin1String("denied")) {
            return QStringLiteral("The owner declined your request for control.");
        }
        if (reason == QLatin1String("taken_back")) {
            return QStringLiteral("The owner took control back.");
        }
        if (reason == QLatin1String("returned")) {
            return QStringLiteral("Control went back to the owner.");
        }
        return QStringLiteral("You no longer have control.");
    }

    QString controlHolder() const
    {
        for (const Watcher& watcher : m_Watchers) {
            if (watcher.control) {
                return watcher.account;
            }
        }
        return QStringLiteral("A viewer");
    }

    void answer(bool allow)
    {
        const Prompt prompt = m_Prompts.takeFirst();
        m_Outbox.append(prompt.kind == Prompt::Join ?
                            joinAnswer(prompt.id, allow) :
                            controlAnswer(prompt.id, allow));
    }

    void setNotice(const QString& text, quint64 now)
    {
        m_Notice = text;
        m_NoticeDeadline = now + NoticeMs;
    }

    void clearNotice()
    {
        m_Notice.clear();
        m_NoticeDeadline = 0;
    }

    bool m_Viewer = false;
    QVector<Prompt> m_Prompts;
    QVector<Watcher> m_Watchers;
    bool m_WatchersOpen = false;
    bool m_RevealRequested = false;
    bool m_ControllerHasControl = true;
    ViewerControl m_ViewerControl = ViewerControl::None;
    quint64 m_ControlRequestDeadline = 0;
    QString m_Notice;
    quint64 m_NoticeDeadline = 0;
    QVector<QJsonObject> m_Outbox;
};

}
