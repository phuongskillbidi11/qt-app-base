#pragma once

#include <QObject>
#include <QVariant>

#include <functional>

class QThread;

// The one place in an application allowed to run a blocking call.
//
// This exists because of a measured failure in the project this base came from: a device
// SDK's connect() was called synchronously from the GUI thread, and when the device was
// unreachable each attempt froze the window for 2.0 seconds. With a retry every 5 s the
// app was unresponsive 21% of the time, permanently — and nobody noticed for months,
// because a *reachable* device connects instantly and never exercises the path.
//
// A base that ships without this boundary hands that bug to every application built on
// it. So: any call that can block — a device SDK, a serial port, a synchronous C library,
// a filesystem scan — goes through Worker::call(). Qt's own asynchronous APIs
// (QNetworkAccessManager, QProcess) do not need it and should not be wrapped in it.
class Worker : public QObject {
    Q_OBJECT
public:
    explicit Worker(QObject *parent = nullptr);
    ~Worker() override;

    // Runs `job` on the worker thread, then delivers its result to `onDone` on
    // `context`'s thread. If `context` is destroyed first, `onDone` is never called —
    // which is the point of requiring it rather than accepting a bare lambda.
    //
    // QVariant rather than a template so the whole mechanism stays in one .cpp and the
    // header stays readable. Wrap richer types in QVariant::fromValue.
    void call(std::function<QVariant()> job, QObject *context,
              std::function<void(const QVariant &)> onDone);

    // Fire-and-forget: runs `job` on the worker thread with no result.
    void post(std::function<void()> job);

    // Jobs queued but not yet started. Useful for a UI that wants to show it is busy.
    int pending() const;

private:
    QThread *m_thread = nullptr;
    QObject *m_runner = nullptr;   // lives on m_thread; owns nothing else
};
