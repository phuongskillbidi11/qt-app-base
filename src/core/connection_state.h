#pragma once

#include <QObject>
#include <QTimer>

#include <functional>

// Connection lifecycle, with no knowledge of what is being connected to.
//
// Two properties are worth stating because they were learned the expensive way:
//
// 1. ONE chokepoint, ONE signal. Every consumer listens to connectionStateChanged(bool)
//    and nothing else. In the project this came from, heartbeat monitoring and automatic
//    reconnection were added later without changing a single line in any of the seven
//    UI tabs — because they all already reacted to that one signal. That is the test of
//    whether this abstraction is worth keeping.
//
// 2. The backoff ladder is here, not in the app. Every reconnect attempt costs whatever
//    the underlying connect costs, and a flat retry interval against an unreachable
//    device is what made the original app freeze 21% of the time.
//
// The two operations that actually touch hardware are supplied by the application as
// callables, so this class has no device dependency and its whole state machine is
// testable without one.
class ConnectionState : public QObject {
    Q_OBJECT
public:
    // Attempt a connection. Returns true on success. May block — call it from a Worker if
    // it does, and give this class the non-blocking wrapper.
    using ConnectFn = std::function<bool()>;
    // Cheap liveness probe against an established connection. False means it is gone.
    using ProbeFn = std::function<bool()>;
    // Tear down an established connection.
    using DisconnectFn = std::function<void()>;

    explicit ConnectionState(QObject *parent = nullptr);

    // asyncResult: false (default) means ConnectFn's return value IS the final answer,
    // exactly as today. true means the underlying connect is event-driven -- ConnectFn's
    // return value only means "the attempt was launched without an immediate, synchronous
    // error" (almost always true for a socket connect), and the real outcome arrives later
    // via reportPendingConnectResult(). Do not pass true unless you will call that method.
    void setHandlers(ConnectFn connectFn, ProbeFn probeFn, DisconnectFn disconnectFn,
                     bool asyncResult = false);

    // Reports the real outcome of an asynchronously launched connect attempt. Stray or
    // late reports are ignored, including reports that arrive after disconnectNow().
    void reportPendingConnectResult(bool succeeded);

    // A user-initiated attempt. Always cancels any reconnect loop first, so the user's
    // action takes effect immediately rather than queueing behind a timer.
    bool connectNow();

    // Startup path: try once, and on failure enter the reconnect loop rather than giving
    // up. The original app failed to do this and a failed *first* connect left it idle
    // forever, which looked identical to "not configured".
    void beginAutoConnect();

    // The only thing that ends a reconnect loop. A failed manual attempt does not.
    void disconnectNow();

    bool isConnected() const;
    bool isReconnecting() const;

    // Delay before the next attempt, given how many have failed in a row. Public and
    // static so the ladder is checkable without a device or a live timer.
    static int reconnectDelayMs(int consecutiveFailures);

    // Heartbeat cadence. 0 disables probing entirely.
    void setHeartbeatIntervalMs(int intervalMs);

signals:
    void connectionStateChanged(bool connected);
    void reconnectingChanged(bool reconnecting);

private slots:
    void onHeartbeatTick();
    void onReconnectTick();

private:
    void setConnected(bool connected);
    void setReconnecting(bool reconnecting);
    void startReconnecting();
    void stopReconnecting();

    ConnectFn m_connect;
    ProbeFn m_probe;
    DisconnectFn m_disconnect;

    bool m_connected = false;
    bool m_reconnecting = false;
    bool m_asyncResult = false;
    bool m_awaitingAsyncResult = false;
    bool m_asyncAutoRetryOnFailure = false;
    int m_reconnectFailures = 0;
    int m_heartbeatIntervalMs = 5000;
    QTimer m_heartbeatTimer;
    QTimer m_reconnectTimer;
};
