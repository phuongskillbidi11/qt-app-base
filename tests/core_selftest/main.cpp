#include "connection_state.h"
#include "worker.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <cstdio>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    bool allPassed = true;
    const auto check = [&allPassed](bool passed, const char *message) {
        std::printf("%s: %s\n", passed ? "PASS" : "FAIL", message);
        allPassed = allPassed && passed;
    };

    // --- the backoff ladder ------------------------------------------------
    // The first three rungs must stay short: recovery from a peer that briefly went away
    // has to be fast. The tail must stop growing, or a long outage would leave the app
    // effectively asleep.
    const int expected[] = {5000, 5000, 5000, 15000, 15000, 30000, 60000};
    bool ladderOk = true;
    for (int i = 0; i < 7; ++i) {
        ladderOk = ladderOk && ConnectionState::reconnectDelayMs(i) == expected[i];
    }
    check(ladderOk, "backoff ladder is 5s x3, 15s x2, 30s, 60s");
    check(ConnectionState::reconnectDelayMs(500) == 60000, "ladder holds past its end");
    check(ConnectionState::reconnectDelayMs(-1) == 5000, "ladder clamps a negative count");

    // --- connection state machine, with no device at all -------------------
    ConnectionState state;
    int connectedSignals = 0;
    int reconnectingSignals = 0;
    QObject::connect(&state, &ConnectionState::connectionStateChanged,
                     [&connectedSignals](bool) { ++connectedSignals; });
    QObject::connect(&state, &ConnectionState::reconnectingChanged,
                     [&reconnectingSignals](bool) { ++reconnectingSignals; });

    bool connectShouldSucceed = false;
    state.setHandlers([&connectShouldSucceed]() { return connectShouldSucceed; },
                      []() { return true; },
                      []() {});
    state.setHeartbeatIntervalMs(0);   // no probing in this test

    check(!state.connectNow() && !state.isConnected() && connectedSignals == 0,
          "a failed connect changes nothing and fires no signal");

    connectShouldSucceed = true;
    check(state.connectNow() && state.isConnected() && connectedSignals == 1,
          "a successful connect fires connectionStateChanged exactly once");

    check(state.connectNow() && connectedSignals == 1,
          "connecting while already connected does not fire the signal again");

    state.disconnectNow();
    check(!state.isConnected() && connectedSignals == 2 && !state.isReconnecting(),
          "disconnect clears the state and leaves no reconnect loop running");

    connectShouldSucceed = false;
    state.beginAutoConnect();
    check(state.isReconnecting() && reconnectingSignals == 1,
          "a failed auto-connect enters the reconnect loop instead of giving up");

    state.disconnectNow();
    check(!state.isReconnecting(),
          "disconnect is the thing that ends a reconnect loop");

    // --- the worker actually runs off the calling thread --------------------
    // The whole reason this class exists is to keep blocking work away from the thread
    // that draws the UI, so the test that matters is the thread identity, not the result.
    Worker worker;
    QThread *callerThread = QThread::currentThread();
    QThread *jobThread = nullptr;
    QVariant received;

    QEventLoop loop;
    worker.call([&jobThread]() -> QVariant {
        jobThread = QThread::currentThread();
        QThread::msleep(150);          // stands in for a blocking device call
        return QVariant(42);
    }, &loop, [&received, &loop](const QVariant &result) {
        received = result;
        loop.quit();
    });

    QElapsedTimer elapsed;
    elapsed.start();
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);   // never hang the test
    loop.exec();

    check(jobThread != nullptr && jobThread != callerThread,
          "the job ran on a different thread from the caller");
    check(received.toInt() == 42, "the result came back to the calling thread");
    check(elapsed.elapsed() >= 150 && elapsed.elapsed() < 5000,
          "the callback arrived after the work, not on a timeout");

    return allPassed ? 0 : 1;
}
