#pragma once

#include "worker.h"

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QJsonObject;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTableWidget;
class QVariant;
class MqConnection;
class MqService;
class MqSettings;
class MqStore;

// Generic presentation fields keep the base independent of application payload schemas.
struct PresentedMessage {
    QString displayTitle;
    QString searchText;
    QString detail;
};

using MessagePresenter = std::function<PresentedMessage(const QString &type,
                                                        const QJsonObject &payload)>;

class MqTab : public QWidget {
public:
    MqTab(MqConnection *connection,
          MqService *service,
          MqSettings *settings,
          MqStore *store,
          MessagePresenter presenter,
          const QString &host,
          quint16 port,
          const QString &vhost,
          QWidget *parent = nullptr);

    void raiseToFront();
    void setConnectionError(const QString &message);

private:
    void refreshConnectionState(bool connected);
    void sendCurrentBody();
    PresentedMessage presentOrFallback(const QString &type,
                                       const QJsonObject &payload) const;
    void insertRow(const QString &direction,
                   const QString &type,
                   const PresentedMessage &presented,
                   bool redelivered,
                   bool acked);
    void loadRecent();
    void applyFilter();
    void populateRows(const QVariant &rows);
    void requestDbSize();
    void updateDbSize(qint64 bytes);
    void showPurgeDialog();
    void onDeliveryAcked(quint64 deliveryTag);
    // Non-fatal: one message was discarded. Must not touch the connection state.
    void noteRejectedMessage(const QString &reason);
    void noteReturnedMessage(const QString &routingKey, const QString &description);
    void updateCounters();
    void setConnectionState(const QString &text, const char *state);

    MqConnection *connection_;
    MqService *service_;
    MqSettings *settings_;
    MqStore *store_;
    MessagePresenter presenter_;
    Worker worker_;
    QHash<quint64, QString> pendingAckEnvelopeIds_;
    QLabel *connectionLabel_;
    QLineEdit *bodyEdit_;
    QPushButton *sendButton_;
    QRadioButton *sendTypeLabNote_;
    QRadioButton *sendTypeCardIssued_;
    QRadioButton *sendTypeCardCancelled_;
    QRadioButton *sendTypeCardReplaced_;
    QComboBox *filterTypeCombo_;
    QDateEdit *filterFromDate_;
    QDateEdit *filterToDate_;
    QLineEdit *filterSearchEdit_;
    QCheckBox *filterReceivedOnlyCheck_;
    QTableWidget *table_;
    QLabel *dbSizeLabel_;
    QPushButton *purgeButton_;
    QCheckBox *pauseCheck_;
    QLabel *sentValue_;
    QLabel *receivedValue_;
    QLabel *waitingValue_;
    QLabel *rejectedValue_;
    QLabel *returnedValue_;
    QLabel *duplicateValue_;
    QString serviceError_;
    quint64 nextSequence_ = 1;
    quint64 sentCount_ = 0;
    quint64 receivedCount_ = 0;
    quint64 waitingCount_ = 0;
    quint64 rejectedCount_ = 0;
    quint64 returnedCount_ = 0;
    quint64 duplicateCount_ = 0;
    bool dbWarningLogged_ = false;
};
