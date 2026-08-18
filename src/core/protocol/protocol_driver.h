#pragma once

#include <QObject>
#include <QString>

// What every field-protocol connection can report, regardless of what it connects to.
//
// Deliberately minimal: isReady(), errorString(), and connectionStateChanged are the exact
// three things Step 3b already needed from MqConnection specifically (see
// qt-app-base/.plans/2026-08-13-step3b-durable-consumer/). This interface gives that shape
// a name that is not RabbitMQ-specific, so a future generic UI shell can hold a
// ProtocolDriver* without naming any one protocol -- mirrors MessagePresenter's role for
// presenters (D9, Step 3b).
//
// Deliberately absent: any "start connecting" method. Every protocol's real connect
// parameters differ, and designing a generic shape from a single example (RabbitMQ) risks
// getting it wrong in a way that costs a second round of changes once a real second
// protocol arrives. Each driver keeps its own, protocol-specific connect method until
// there are two real examples to generalize from (see spec.md D2).
class ProtocolDriver : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ProtocolDriver() override = default;

    virtual bool isReady() const = 0;
    virtual QString errorString() const = 0;

signals:
    void connectionStateChanged(bool connected);
};
