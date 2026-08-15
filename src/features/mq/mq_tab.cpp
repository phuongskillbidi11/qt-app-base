#include "mq_tab.h"

#include "app_log.h"
#include "mq_connection.h"
#include "mq_service.h"
#include "mq_settings_dialog.h"
#include "table_style.h"

#include <QCheckBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWindow>

namespace {
constexpr int kMaximumRows = 5000;

QString utf16(const char16_t *text) {
    return QString::fromUtf16(reinterpret_cast<const ushort *>(text));
}

QLabel *makeCounterValue(QWidget *parent) {
    auto *value = new QLabel("0", parent);
    value->setMinimumWidth(48);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setProperty("metric", true);
    return value;
}
}

MqTab::MqTab(MqConnection *connection,
             MqService *service,
             MqSettings *settings,
             const QString &host,
             quint16 port,
             const QString &vhost,
             QWidget *parent)
    : QWidget(parent),
      connection_(connection),
      service_(service),
      settings_(settings),
      connectionLabel_(new QLabel(this)),
      bodyEdit_(new QLineEdit(this)),
      sendButton_(new QPushButton(utf16(u"G\u1EEDi"), this)),
      table_(new QTableWidget(this)),
      pauseCheck_(new QCheckBox(utf16(u"T\u1EA1m d\u1EEBng nh\u1EADn"), this)),
      sentValue_(makeCounterValue(this)),
      receivedValue_(makeCounterValue(this)),
      waitingValue_(makeCounterValue(this)),
      rejectedValue_(makeCounterValue(this)),
      returnedValue_(makeCounterValue(this)) {
    setWindowTitle("Qt MQ Lab");
    resize(920, 580);

    auto *mainLayout = new QVBoxLayout(this);

    auto *connectionRow = new QHBoxLayout;
    connectionRow->addWidget(connectionLabel_);
    connectionRow->addSpacing(12);
    connectionRow->addWidget(new QLabel(QString("%1:%2  vhost %3").arg(host).arg(port).arg(vhost), this));
    connectionRow->addStretch();
    auto *settingsButton = new QPushButton(utf16(u"C\u00E0i \u0111\u1EB7t"), this);
    UiStyle::makeBarButton(settingsButton);
    connectionRow->addWidget(settingsButton);
    mainLayout->addLayout(connectionRow);

    auto *inputRow = new QHBoxLayout;
    inputRow->addWidget(new QLabel(utf16(u"N\u1ED9i dung:"), this));
    bodyEdit_->setPlaceholderText(utf16(u"Nh\u1EADp n\u1ED9i dung tin nh\u1EAFn"));
    inputRow->addWidget(bodyEdit_, 1);
    UiStyle::makeBarButton(sendButton_, true);
    inputRow->addWidget(sendButton_);
    mainLayout->addLayout(inputRow);

    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({"#",
                                       utf16(u"L\u00FAc nh\u1EADn"),
                                       "type / payload",
                                       "redelivered",
                                       "ack"});
    UiStyle::tuneTable(table_);
    UiStyle::attachEmptyHint(table_, utf16(u"Ch\u01B0a c\u00F3 tin nh\u1EAFn"));
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    mainLayout->addWidget(table_, 1);

    auto *footer = new QHBoxLayout;
    footer->addWidget(pauseCheck_);
    footer->addStretch();
    footer->addWidget(new QLabel(utf16(u"\u0110\u00E3 g\u1EEDi:"), this));
    footer->addWidget(sentValue_);
    footer->addSpacing(16);
    footer->addWidget(new QLabel(utf16(u"\u0110\u00E3 nh\u1EADn:"), this));
    footer->addWidget(receivedValue_);
    footer->addSpacing(16);
    footer->addWidget(new QLabel(utf16(u"Ch\u1EDD:"), this));
    footer->addWidget(waitingValue_);
    footer->addSpacing(16);
    footer->addWidget(new QLabel(utf16(u"B\u1ECF qua:"), this));
    footer->addWidget(rejectedValue_);
    footer->addSpacing(16);
    footer->addWidget(new QLabel(utf16(u"Tr\u1EA3 v\u1EC1:"), this));
    footer->addWidget(returnedValue_);
    mainLayout->addLayout(footer);

    connect(settingsButton, &QPushButton::clicked, this, [this]() {
        MqSettingsDialog dialog(settings_, this);
        dialog.exec();
    });
    connect(sendButton_, &QPushButton::clicked, this, &MqTab::sendCurrentBody);
    connect(bodyEdit_, &QLineEdit::returnPressed, this, &MqTab::sendCurrentBody);
    connect(pauseCheck_, &QCheckBox::toggled, service_, &MqService::pauseConsuming);
    connect(service_, &MqService::published, this, [this](const QString &) {
        ++sentCount_;
        ++waitingCount_;
        updateCounters();
    });
    connect(service_, &MqService::messageReceived, this,
            [this](const QString &, const QDateTime &, const QString &type,
                   const QJsonObject &payload,
                   bool redelivered, quint64 deliveryTag) {
                appendMessage(type, payload, redelivered, deliveryTag);
            });
    connect(service_, &MqService::errorOccurred, this, &MqTab::setConnectionError);
    // Deliberately NOT setConnectionError: a discarded message is not a connection fault (F4b).
    connect(service_, &MqService::messageRejected, this, &MqTab::noteRejectedMessage);
    connect(service_, &MqService::messageReturned, this, &MqTab::noteReturnedMessage);

    connectionPoll_.setInterval(250);
    connect(&connectionPoll_, &QTimer::timeout, this, &MqTab::refreshConnectionState);
    connectionPoll_.start();
    refreshConnectionState();
}

void MqTab::raiseToFront() {
    showNormal();
    raise();
    activateWindow();
}

void MqTab::setConnectionError(const QString &message) {
    serviceError_ = message;
    refreshConnectionState();
}

void MqTab::refreshConnectionState() {
    if (!connection_->errorString().isEmpty()) {
        setConnectionState(utf16(u"\u25CF L\u1ED7i k\u1EBFt n\u1ED1i: ")
                               + connection_->errorString(),
                           "warn");
        sendButton_->setEnabled(false);
        return;
    }
    if (!serviceError_.isEmpty()) {
        setConnectionState(utf16(u"\u25CF L\u1ED7i AMQP: ") + serviceError_, "warn");
        sendButton_->setEnabled(false);
        return;
    }
    if (!connection_->isReady()) {
        setConnectionState(utf16(u"\u25CF \u0110ang k\u1EBFt n\u1ED1i..."), "off");
        sendButton_->setEnabled(false);
        return;
    }

    setConnectionState(utf16(u"\u25CF \u0110\u00E3 k\u1EBFt n\u1ED1i"), "on");
    sendButton_->setEnabled(true);
    if (!serviceStarted_ && service_->startConsuming()) {
        serviceStarted_ = true;
    }
}

void MqTab::sendCurrentBody() {
    const QString body = bodyEdit_->text();
    if (body.trimmed().isEmpty() || !connection_->isReady()) {
        return;
    }
    if (service_->publish(body)) {
        bodyEdit_->clear();
    }
}

void MqTab::appendMessage(const QString &type,
                          const QJsonObject &payload,
                          bool redelivered,
                          quint64 deliveryTag) {
    if (table_->rowCount() >= kMaximumRows) {
        table_->removeRow(0);
    }

    const int row = table_->rowCount();
    table_->insertRow(row);

    auto *sequenceItem = new QTableWidgetItem(QString::number(nextSequence_++));
    sequenceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(row, 0, sequenceItem);

    auto *receivedItem = new QTableWidgetItem(QDateTime::currentDateTime().toString("HH:mm:ss"));
    receivedItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(row, 1, receivedItem);
    const QString payloadText = QString::fromUtf8(
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    table_->setItem(row, 2, new QTableWidgetItem(type + "  " + payloadText));

    auto *redeliveredItem = new QTableWidgetItem(redelivered ? utf16(u"c\u00F3") : QString());
    redeliveredItem->setTextAlignment(Qt::AlignCenter);
    table_->setItem(row, 3, redeliveredItem);

    auto *ackItem = new QTableWidgetItem(utf16(u"\u2713"));
    ackItem->setTextAlignment(Qt::AlignCenter);
    table_->setItem(row, 4, ackItem);

    ++receivedCount_;
    if (waitingCount_ > 0) {
        --waitingCount_;
    }
    updateCounters();

    // Every item is in the model before this call; only now may the broker delivery be acked.
    service_->confirmInserted(deliveryTag);
}

void MqTab::noteRejectedMessage(const QString &reason) {
    // Touches neither connectionLabel_, sendButton_ nor serviceError_ — that separation is the
    // whole point of F4b. One unreadable message must not disable a healthy application.
    ++rejectedCount_;
    updateCounters();
    AppLog::warn(QString("rejected an undecodable message: %1").arg(reason));
}

void MqTab::noteReturnedMessage(const QString &routingKey, const QString &description) {
    ++returnedCount_;
    updateCounters();
    AppLog::warn(QString("message returned for routing key %1: %2")
                     .arg(routingKey, description));
}

void MqTab::updateCounters() {
    sentValue_->setText(QString::number(sentCount_));
    receivedValue_->setText(QString::number(receivedCount_));
    waitingValue_->setText(QString::number(waitingCount_));
    rejectedValue_->setText(QString::number(rejectedCount_));
    returnedValue_->setText(QString::number(returnedCount_));
}

void MqTab::setConnectionState(const QString &text, const char *state) {
    connectionLabel_->setText(text);
    connectionLabel_->setProperty("state", state);
    UiStyle::repolish(connectionLabel_);
}
