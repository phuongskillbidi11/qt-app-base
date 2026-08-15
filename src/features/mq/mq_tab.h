#pragma once

#include <QDateTime>
#include <QString>
#include <QTimer>
#include <QWidget>

class QCheckBox;
class QJsonObject;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class MqConnection;
class MqService;
class MqSettings;

class MqTab : public QWidget {
public:
    MqTab(MqConnection *connection,
          MqService *service,
          MqSettings *settings,
          const QString &host,
          quint16 port,
          const QString &vhost,
          QWidget *parent = nullptr);

    void raiseToFront();
    void setConnectionError(const QString &message);

private:
    void refreshConnectionState();
    void sendCurrentBody();
    void appendMessage(const QString &type,
                       const QJsonObject &payload,
                       bool redelivered,
                       quint64 deliveryTag);
    // Non-fatal: one message was discarded. Must not touch the connection state.
    void noteRejectedMessage(const QString &reason);
    void noteReturnedMessage(const QString &routingKey, const QString &description);
    void updateCounters();
    void setConnectionState(const QString &text, const char *state);

    MqConnection *connection_;
    MqService *service_;
    MqSettings *settings_;
    QLabel *connectionLabel_;
    QLineEdit *bodyEdit_;
    QPushButton *sendButton_;
    QTableWidget *table_;
    QCheckBox *pauseCheck_;
    QLabel *sentValue_;
    QLabel *receivedValue_;
    QLabel *waitingValue_;
    QLabel *rejectedValue_;
    QLabel *returnedValue_;
    QTimer connectionPoll_;
    QString serviceError_;
    quint64 nextSequence_ = 1;
    quint64 sentCount_ = 0;
    quint64 receivedCount_ = 0;
    quint64 waitingCount_ = 0;
    quint64 rejectedCount_ = 0;
    quint64 returnedCount_ = 0;
    bool serviceStarted_ = false;
};
