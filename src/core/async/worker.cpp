#include "worker.h"

#include <QAtomicInt>
#include <QPointer>
#include <QThread>

namespace {
// Counts jobs accepted but not yet finished. Shared between threads, hence atomic.
QAtomicInt g_pending;
}

Worker::Worker(QObject *parent)
    : QObject(parent),
      m_thread(new QThread),
      m_runner(new QObject) {
    m_runner->moveToThread(m_thread);
    m_thread->start();
}

Worker::~Worker() {
    // Order matters. deleteLater() is queued onto the worker thread, so it must be
    // requested before quit() and collected by wait(); reversing this leaks the runner.
    m_runner->deleteLater();
    m_thread->quit();
    m_thread->wait();
    delete m_thread;
}

void Worker::call(std::function<QVariant()> job, QObject *context,
                  std::function<void(const QVariant &)> onDone) {
    if (!job) {
        return;
    }
    // QPointer, not a raw pointer: the context may be a widget the user closes while the
    // job is still running on the worker thread, and delivering a result to a destroyed
    // object is a crash that only shows up under load.
    QPointer<QObject> guard(context);
    g_pending.ref();

    QMetaObject::invokeMethod(m_runner, [job, guard, onDone]() {
        const QVariant result = job();
        g_pending.deref();
        if (guard.isNull() || !onDone) {
            return;
        }
        QMetaObject::invokeMethod(guard, [onDone, result]() {
            onDone(result);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void Worker::post(std::function<void()> job) {
    if (!job) {
        return;
    }
    g_pending.ref();
    QMetaObject::invokeMethod(m_runner, [job]() {
        job();
        g_pending.deref();
    }, Qt::QueuedConnection);
}

int Worker::pending() const {
    return g_pending.loadAcquire();
}
