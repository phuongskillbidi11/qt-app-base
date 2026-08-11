#pragma once

#include <QLocalServer>
#include <QObject>
#include <QString>

class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(const QString &key, QObject *parent = nullptr);

    // True if we are the first instance and now own the pipe.
    bool isPrimary() const;

    // Called by a secondary instance: ask the primary to show itself.
    // Returns false if the primary could not be reached (hung, or another
    // Windows session) — the caller then shows the message box (spec.md D4).
    bool signalPrimaryToRaise();

signals:
    // Emitted in the PRIMARY instance when a secondary asks it to appear.
    void raiseRequested();

private:
    QString m_key;
    QLocalServer m_server;
    bool m_primary = false;
};
