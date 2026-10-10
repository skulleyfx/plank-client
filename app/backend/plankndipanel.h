#pragma once

#include <QString>
#include <QtGlobal>

#include <mutex>

// The "NDI" button of the PLANK toolbar and the host tile menu: state kept in
// step with the host's serverinfo <PlankNdi> element and the /plank/ndi
// endpoint. Pure logic only (no SDL, no networking), so it can be unit tested:
// the toolbar draws it, the session does the HTTPS calls on a worker thread
// and reports back with finish().
//
// The host reports PlankNdi as unavailable | off | on | always. Only off and on
// can be switched; anything else, or a host that does not send the element at
// all, hides the button.
namespace PlankNdi {

// How long a short error ("Only the desktop owner ...") stays up.
constexpr quint64 NoticeMs = 6000;
// While the button is shown, ask the host for its state this often, so an
// automatic switch-off (session ended, timeout) shows up.
constexpr quint64 RefreshMs = 15000;

enum class HostState {
    Hidden,
    Off,
    On,
};

inline HostState parseHostState(const QString& text)
{
    const QString value = text.trimmed().toLower();
    if (value == QLatin1String("off")) {
        return HostState::Off;
    }
    if (value == QLatin1String("on")) {
        return HostState::On;
    }
    return HostState::Hidden;
}

// The value of the state= query argument for a switch request.
inline QString stateArgument(bool on)
{
    return on ? QStringLiteral("on") : QStringLiteral("off");
}

// Short, plain text for a refused or failed request. httpStatus is 0 when the
// host could not be reached at all.
inline QString failureMessage(int httpStatus)
{
    switch (httpStatus) {
    case 401:
        return QStringLiteral("Sign in again to change NDI");
    case 403:
        return QStringLiteral("Only the desktop owner or an administrator can change NDI");
    case 404:
        return QStringLiteral("This host cannot switch NDI");
    case 409:
        return QStringLiteral("NDI is not available on this host");
    default:
        return QStringLiteral("Could not change NDI");
    }
}

struct Request {
    enum Kind {
        None,
        Refresh,
        Switch,
    };
    Kind kind = None;
    bool on = false;  // Switch only
};

class Panel
{
public:
    // Take the host's serverinfo values (session start, host list). Ignored
    // while a request is under way: its answer is newer.
    void setHost(const QString& state, const QString& source)
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        if (m_Busy || m_SwitchQueued) {
            return;
        }
        m_State = parseHostState(state);
        m_Source = source;
    }

    bool visible() const
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        return m_State != HostState::Hidden;
    }

    bool on() const
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        return m_State == HostState::On;
    }

    bool pending() const
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        return m_Busy || m_SwitchQueued;
    }

    QString source() const
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        return m_Source;
    }

    // Button text.
    QString label() const
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        return m_State == HostState::On ? QStringLiteral("NDI ON") : QStringLiteral("NDI");
    }

    // Hover text: what the button does and, when on, where to find the source.
    QString tooltip() const
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        if (m_State == HostState::On) {
            return m_Source.isEmpty() ?
                        QStringLiteral("NDI is ON. Click to turn it off") :
                        QStringLiteral("NDI is ON as \"%1\". Click to turn it off").arg(m_Source);
        }
        return QStringLiteral("NDI is off. Click to publish this host's screen over NDI");
    }

    // The user clicked the button. One request at a time.
    void requestToggle()
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        if (m_State == HostState::Hidden || m_Busy || m_SwitchQueued) {
            return;
        }
        m_SwitchQueued = true;
        m_SwitchOn = m_State != HostState::On;
        m_Notice.clear();
    }

    // Session thread: the next request to send, if any. A queued click comes
    // first; otherwise a refresh once the state is RefreshMs old. Marks the
    // panel busy until finish().
    bool takeRequest(quint64 now, Request& request)
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        if (m_Busy || m_Stopped) {
            return false;
        }
        // The first look at the host happens at once, even while the button is
        // hidden (serverinfo seen so far may predate sign-in); after that the
        // state is re-read every RefreshMs, hidden or not, so NDI Tools being
        // installed or a switch made elsewhere shows up live.
        if (!m_Synced && !m_SwitchQueued) {
            m_Busy = true;
            request.kind = Request::Refresh;
            request.on = false;
            return true;
        }
        if (m_State == HostState::Hidden && !m_SwitchQueued) {
            if (now >= m_LastSync + RefreshMs) {
                m_Busy = true;
                request.kind = Request::Refresh;
                request.on = false;
                return true;
            }
            return false;
        }
        if (m_SwitchQueued) {
            m_SwitchQueued = false;
            m_Busy = true;
            request.kind = Request::Switch;
            request.on = m_SwitchOn;
            return true;
        }
        if (now >= m_LastSync + RefreshMs) {
            m_Busy = true;
            request.kind = Request::Refresh;
            request.on = false;
            return true;
        }
        return false;
    }

    // Worker thread: the request is over. On success state/source come from
    // the host's answer; otherwise the old state stays. A failed switch shows
    // errorText for NoticeMs; a failed refresh stays quiet.
    // httpStatus is the failure's HTTP status when known (0 otherwise). A 404
    // means an older host without the endpoint: polling stops for good.
    void finish(const Request& request, bool ok, const QString& state,
                const QString& source, const QString& errorText, quint64 now,
                int httpStatus = 0)
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        m_Busy = false;
        m_LastSync = now;
        m_Synced = true;
        if (ok) {
            m_State = parseHostState(state);
            m_Source = source;
            // "always" cannot change without a host restart (which ends the
            // session); nothing to poll for.
            if (state.trimmed().toLower() == QLatin1String("always")) {
                m_Stopped = true;
            }
        }
        else if (httpStatus == 404) {
            m_Stopped = true;
            m_State = HostState::Hidden;
        }
        else if (request.kind == Request::Switch) {
            m_Notice = errorText;
            m_NoticeUntil = now + NoticeMs;
        }
    }

    // The current error text, empty once it has been up for NoticeMs.
    QString notice(quint64 now) const
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        return now < m_NoticeUntil ? m_Notice : QString();
    }

    // Everything the toolbar draws for this button, so a change redraws it.
    QString signature(quint64 now) const
    {
        const QString note = notice(now);
        std::lock_guard<std::mutex> lock(m_Lock);
        return QString::number(static_cast<int>(m_State)) +
                (m_Busy || m_SwitchQueued ? QLatin1Char('*') : QLatin1Char('.')) +
                m_Source + QLatin1Char('|') + note;
    }

private:
    mutable std::mutex m_Lock;
    HostState m_State = HostState::Hidden;
    QString m_Source;
    bool m_Busy = false;
    bool m_SwitchQueued = false;
    bool m_SwitchOn = false;
    quint64 m_LastSync = 0;
    bool m_Synced = false;   // a request to the host has finished at least once
    bool m_Stopped = false;  // the host has no such endpoint; stop asking
    QString m_Notice;
    quint64 m_NoticeUntil = 0;
};

}
