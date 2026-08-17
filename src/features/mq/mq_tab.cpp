#include "mq_tab.h"

#include "app_log.h"
#include "mq_codec.h"
#include "mq_connection.h"
#include "mq_service.h"
#include "mq_settings.h"
#include "mq_settings_dialog.h"
#include "mq_store.h"
#include "table_style.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTime>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>

#include <utility>

namespace {
constexpr int kMaximumRows = 5000;
constexpr int kRecentRows = 200;

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

QDateTime startOfDay(const QDate &date) {
    return QDateTime(date, QTime(0, 0, 0, 0));
}

QDateTime endOfDay(const QDate &date) {
    return QDateTime(date, QTime(23, 59, 59, 999));
}
}  // namespace

MqTab::MqTab(MqConnection *connection,
             MqService *service,
             MqSettings *settings,
             MqStore *store,
             MessagePresenter presenter,
             const QString &host,
             quint16 port,
             const QString &vhost,
             QWidget *parent)
    : QWidget(parent),
      connection_(connection),
      service_(service),
      settings_(settings),
      store_(store),
      presenter_(std::move(presenter)),
      worker_(),
      connectionLabel_(new QLabel(this)),
      bodyEdit_(new QLineEdit(this)),
      sendButton_(new QPushButton(utf16(u"G\u1EEDi"), this)),
      sendTypeLabNote_(new QRadioButton(QStringLiteral("lab.note"), this)),
      sendTypeCardIssued_(new QRadioButton(QStringLiteral("card.issued"), this)),
      sendTypeCardCancelled_(new QRadioButton(QStringLiteral("card.cancelled"), this)),
      sendTypeCardReplaced_(new QRadioButton(QStringLiteral("card.replaced"), this)),
      filterTypeCombo_(new QComboBox(this)),
      filterFromDate_(new QDateEdit(this)),
      filterToDate_(new QDateEdit(this)),
      filterSearchEdit_(new QLineEdit(this)),
      filterReceivedOnlyCheck_(new QCheckBox(utf16(u"ch\u1EC9 tin nh\u1EADn"), this)),
      table_(new QTableWidget(this)),
      dbSizeLabel_(new QLabel(this)),
      purgeButton_(new QPushButton(utf16(u"D\u1ECDn"), this)),
      pauseCheck_(new QCheckBox(utf16(u"T\u1EA1m d\u1EEBng nh\u1EADn"), this)),
      sentValue_(makeCounterValue(this)),
      receivedValue_(makeCounterValue(this)),
      waitingValue_(makeCounterValue(this)),
      rejectedValue_(makeCounterValue(this)),
      returnedValue_(makeCounterValue(this)),
      duplicateValue_(makeCounterValue(this)) {
    setWindowTitle("Qt MQ Lab");
    resize(1180, 720);

    connectionLabel_->setObjectName("connectionLabel");
    bodyEdit_->setObjectName("bodyEdit");
    sendButton_->setObjectName("sendButton");
    sendTypeLabNote_->setObjectName("sendTypeLabNote");
    sendTypeCardIssued_->setObjectName("sendTypeCardIssued");
    sendTypeCardCancelled_->setObjectName("sendTypeCardCancelled");
    sendTypeCardReplaced_->setObjectName("sendTypeCardReplaced");
    filterTypeCombo_->setObjectName("filterTypeCombo");
    filterFromDate_->setObjectName("filterFromDate");
    filterToDate_->setObjectName("filterToDate");
    filterSearchEdit_->setObjectName("filterSearchEdit");
    filterReceivedOnlyCheck_->setObjectName("filterReceivedOnlyCheck");
    table_->setObjectName("messageTable");
    dbSizeLabel_->setObjectName("dbSizeLabel");
    purgeButton_->setObjectName("purgeButton");
    pauseCheck_->setObjectName("pauseCheck");
    sentValue_->setObjectName("sentValue");
    receivedValue_->setObjectName("receivedValue");
    waitingValue_->setObjectName("waitingValue");
    rejectedValue_->setObjectName("rejectedValue");
    returnedValue_->setObjectName("returnedValue");
    duplicateValue_->setObjectName("duplicateValue");

    sendTypeLabNote_->setChecked(true);
    filterFromDate_->setCalendarPopup(true);
    filterToDate_->setCalendarPopup(true);
    filterFromDate_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    filterToDate_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    filterFromDate_->setDate(QDate::currentDate().addMonths(-1));
    filterToDate_->setDate(QDate::currentDate());
    filterSearchEdit_->setPlaceholderText(utf16(u"t\u00EAn ho\u1EB7c m\u00E3 th\u1EBB"));

    filterTypeCombo_->addItem(utf16(u"t\u1EA5t c\u1EA3"), QString());
    filterTypeCombo_->addItem(QStringLiteral("lab.note"), QStringLiteral("lab.note"));
    filterTypeCombo_->addItem(QStringLiteral("card.issued"), QStringLiteral("card.issued"));
    filterTypeCombo_->addItem(QStringLiteral("card.cancelled"),
                              QStringLiteral("card.cancelled"));
    filterTypeCombo_->addItem(QStringLiteral("card.replaced"),
                              QStringLiteral("card.replaced"));

    auto *mainLayout = new QVBoxLayout(this);

    auto *connectionRow = new QHBoxLayout;
    connectionRow->addWidget(connectionLabel_);
    connectionRow->addSpacing(12);
    connectionRow->addWidget(new QLabel(QString("%1:%2  vhost %3").arg(host).arg(port).arg(vhost),
                                         this));
    connectionRow->addStretch();
    auto *settingsButton = new QPushButton(utf16(u"C\u00E0i \u0111\u1EB7t"), this);
    settingsButton->setObjectName("settingsButton");
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

    auto *sendTypeRow = new QHBoxLayout;
    sendTypeRow->addWidget(new QLabel(utf16(u"Lo\u1EA1i:"), this));
    sendTypeRow->addWidget(sendTypeLabNote_);
    sendTypeRow->addWidget(sendTypeCardIssued_);
    sendTypeRow->addWidget(sendTypeCardCancelled_);
    sendTypeRow->addWidget(sendTypeCardReplaced_);
    sendTypeRow->addStretch();
    mainLayout->addLayout(sendTypeRow);

    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(utf16(u"L\u1ECDc:"), this));
    filterRow->addWidget(new QLabel(utf16(u"Lo\u1EA1i"), this));
    filterRow->addWidget(filterTypeCombo_);
    filterRow->addWidget(new QLabel(utf16(u"T\u1EEB"), this));
    filterRow->addWidget(filterFromDate_);
    filterRow->addWidget(new QLabel(utf16(u"\u0111\u1EBFn"), this));
    filterRow->addWidget(filterToDate_);
    filterRow->addWidget(new QLabel(utf16(u"T\u00ECm"), this));
    filterRow->addWidget(filterSearchEdit_, 1);
    filterRow->addWidget(filterReceivedOnlyCheck_);
    auto *filterApplyButton = new QPushButton(utf16(u"L\u1ECDc"), this);
    auto *filterClearButton = new QPushButton(utf16(u"Xo\u00E1 l\u1ECDc"), this);
    filterApplyButton->setObjectName("filterApplyButton");
    filterClearButton->setObjectName("filterClearButton");
    UiStyle::makeBarButton(filterApplyButton, true);
    UiStyle::makeBarButton(filterClearButton);
    filterRow->addWidget(filterApplyButton);
    filterRow->addWidget(filterClearButton);
    mainLayout->addLayout(filterRow);

    table_->setColumnCount(8);
    table_->setHorizontalHeaderLabels({"#",
                                       utf16(u"L\u00FAc"),
                                       utf16(u"H\u01B0\u1EDBng"),
                                       utf16(u"Lo\u1EA1i"),
                                       utf16(u"T\u00EAn hi\u1EC3n th\u1ECB"),
                                       utf16(u"Chi ti\u1EBFt"),
                                       "Redelivered",
                                       "Ack"});
    UiStyle::tuneTable(table_);
    UiStyle::attachEmptyHint(table_, utf16(u"Ch\u01B0a c\u00F3 tin nh\u1EAFn"));
    for (int column = 0; column < table_->columnCount(); ++column) {
        table_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    mainLayout->addWidget(table_, 1);

    auto *databaseRow = new QHBoxLayout;
    databaseRow->addStretch();
    databaseRow->addWidget(dbSizeLabel_);
    UiStyle::makeBarButton(purgeButton_);
    purgeButton_->setVisible(false);
    databaseRow->addWidget(purgeButton_);
    mainLayout->addLayout(databaseRow);

    auto *footer = new QHBoxLayout;
    footer->addWidget(pauseCheck_);
    footer->addStretch();
    footer->addWidget(new QLabel(utf16(u"\u0110\u00E3 g\u1EEDi:"), this));
    footer->addWidget(sentValue_);
    footer->addSpacing(12);
    footer->addWidget(new QLabel(utf16(u"\u0110\u00E3 nh\u1EADn:"), this));
    footer->addWidget(receivedValue_);
    footer->addSpacing(12);
    footer->addWidget(new QLabel(utf16(u"Ch\u1EDD:"), this));
    footer->addWidget(waitingValue_);
    footer->addSpacing(12);
    footer->addWidget(new QLabel(utf16(u"B\u1ECF qua:"), this));
    footer->addWidget(rejectedValue_);
    footer->addSpacing(12);
    footer->addWidget(new QLabel(utf16(u"Tr\u1EA3 v\u1EC1:"), this));
    footer->addWidget(returnedValue_);
    footer->addSpacing(12);
    footer->addWidget(new QLabel(utf16(u"Tr\u00F9ng:"), this));
    footer->addWidget(duplicateValue_);
    mainLayout->addLayout(footer);

    connect(settingsButton, &QPushButton::clicked, this, [this]() {
        MqSettingsDialog dialog(settings_, this);
        dialog.exec();
    });
    connect(sendButton_, &QPushButton::clicked, this, &MqTab::sendCurrentBody);
    connect(bodyEdit_, &QLineEdit::returnPressed, this, &MqTab::sendCurrentBody);
    connect(filterApplyButton, &QPushButton::clicked, this, &MqTab::applyFilter);
    connect(filterClearButton, &QPushButton::clicked, this, [this]() {
        filterTypeCombo_->setCurrentIndex(0);
        filterFromDate_->setDate(QDate::currentDate().addMonths(-1));
        filterToDate_->setDate(QDate::currentDate());
        filterSearchEdit_->clear();
        filterReceivedOnlyCheck_->setChecked(false);
        loadRecent();
    });
    connect(purgeButton_, &QPushButton::clicked, this, &MqTab::showPurgeDialog);
    connect(pauseCheck_, &QCheckBox::toggled, service_, &MqService::pauseConsuming);
    connect(service_, &MqService::deliveryAcked, this, &MqTab::onDeliveryAcked);
    connect(service_, &MqService::published, this,
            [this](const QString &id, const QDateTime &timestamp, const QString &type,
                   const QJsonObject &payload) {
                ++sentCount_;
                ++waitingCount_;
                updateCounters();

                const PresentedMessage presented = presentOrFallback(type, payload);
                const MqCodec::Envelope envelope{id, timestamp, 1, QString(), type, payload};
                worker_.call(
                    [store = store_, envelope, presented]() {
                        return QVariant(store->insert(envelope,
                                                      QStringLiteral("sent"),
                                                      presented.displayTitle,
                                                      presented.searchText));
                    },
                    this,
                    [this, type, presented](const QVariant &) {
                        insertRow(QStringLiteral("sent"), type, presented, false, false);
                        requestDbSize();
                    });
            });
    connect(service_, &MqService::messageReceived, this,
            [this](const QString &id, const QDateTime &timestamp, const QString &type,
                   const QJsonObject &payload,
                   bool redelivered, quint64 deliveryTag) {
                const PresentedMessage presented = presentOrFallback(type, payload);
                const MqCodec::Envelope envelope{id, timestamp, 1, QString(), type, payload};
                worker_.call(
                    [store = store_, envelope, presented]() {
                        return QVariant(store->insert(envelope,
                                                      QStringLiteral("received"),
                                                      presented.displayTitle,
                                                      presented.searchText));
                    },
                    this,
                    [this, deliveryTag, type, presented, redelivered,
                     envelopeId = envelope.id](const QVariant &freshlyInserted) {
                        pendingAckEnvelopeIds_.insert(deliveryTag, envelopeId);
                        service_->confirmInserted(deliveryTag);
                        const bool ackedNow = !pendingAckEnvelopeIds_.contains(deliveryTag);
                        if (freshlyInserted.toBool()) {
                            insertRow(QStringLiteral("received"),
                                      type,
                                      presented,
                                      redelivered,
                                      ackedNow);
                        } else {
                            ++duplicateCount_;
                            updateCounters();
                        }
                        requestDbSize();
                    });
            });
    connect(service_, &MqService::errorOccurred, this, &MqTab::setConnectionError);
    // A discarded message is not a connection fault.
    connect(service_, &MqService::messageRejected, this, &MqTab::noteRejectedMessage);
    connect(service_, &MqService::messageReturned, this, &MqTab::noteReturnedMessage);
    connect(connection_, &MqConnection::connectionStateChanged,
            this, &MqTab::refreshConnectionState);

    refreshConnectionState(connection_->isReady());
    loadRecent();
}

void MqTab::raiseToFront() {
    showNormal();
    raise();
    activateWindow();
}

void MqTab::setConnectionError(const QString &message) {
    serviceError_ = message;
    setConnectionState(utf16(u"\u25CF L\u1ED7i AMQP: ") + serviceError_, "warn");
    sendButton_->setEnabled(false);
}

void MqTab::refreshConnectionState(bool connected) {
    if (!connected) {
        if (!connection_->errorString().isEmpty()) {
            setConnectionState(utf16(u"\u25CF L\u1ED7i k\u1EBFt n\u1ED1i: ")
                                   + connection_->errorString(),
                               "warn");
        } else {
            setConnectionState(utf16(u"\u25CF \u0110ang k\u1EBFt n\u1ED1i..."), "off");
        }
        sendButton_->setEnabled(false);
        return;
    }

    serviceError_.clear();
    setConnectionState(utf16(u"\u25CF \u0110\u00E3 k\u1EBFt n\u1ED1i"), "on");
    sendButton_->setEnabled(true);
    service_->startConsuming();
}

void MqTab::sendCurrentBody() {
    const QString body = bodyEdit_->text();
    if (body.trimmed().isEmpty() || !connection_->isReady()) {
        return;
    }

    QString type = QStringLiteral("lab.note");
    QJsonObject payload{{QStringLiteral("text"), body}};
    if (!sendTypeLabNote_->isChecked()) {
        if (sendTypeCardIssued_->isChecked()) {
            type = QStringLiteral("card.issued");
        } else if (sendTypeCardCancelled_->isChecked()) {
            type = QStringLiteral("card.cancelled");
        } else {
            type = QStringLiteral("card.replaced");
        }
        payload = QJsonObject{{QStringLiteral("username"), body},
                              {QStringLiteral("card_uid"), QStringLiteral("DEMO-CARD")},
                              {QStringLiteral("gender"), QString()},
                              {QStringLiteral("role"), QStringLiteral("demo")},
                              {QStringLiteral("expiry_date"),
                               QDate::currentDate().addYears(1).toString(Qt::ISODate)}};
    }

    if (service_->publish(type, payload)) {
        bodyEdit_->clear();
    }
}

PresentedMessage MqTab::presentOrFallback(const QString &type,
                                          const QJsonObject &payload) const {
    PresentedMessage presented = presenter_ ? presenter_(type, payload) : PresentedMessage{};
    if (presented.displayTitle.isEmpty()) {
        presented.displayTitle = QString::fromUtf8(
            QJsonDocument(payload).toJson(QJsonDocument::Compact));
    }
    return presented;
}

void MqTab::insertRow(const QString &direction,
                      const QString &type,
                      const PresentedMessage &presented,
                      bool redelivered,
                      bool acked) {
    if (table_->rowCount() >= kMaximumRows) {
        table_->removeRow(0);
    }

    const int row = table_->rowCount();
    table_->insertRow(row);

    auto *sequenceItem = new QTableWidgetItem(QString::number(nextSequence_++));
    sequenceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(row, 0, sequenceItem);

    auto *timeItem = new QTableWidgetItem(QDateTime::currentDateTime().toString("HH:mm:ss"));
    timeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(row, 1, timeItem);
    table_->setItem(row, 2,
                    new QTableWidgetItem(direction == QStringLiteral("received")
                                             ? utf16(u"nh\u1EADn")
                                             : utf16(u"g\u1EEDi")));
    table_->setItem(row, 3, new QTableWidgetItem(type));
    table_->setItem(row, 4, new QTableWidgetItem(presented.displayTitle));
    table_->setItem(row, 5, new QTableWidgetItem(
                                presented.detail.isEmpty() ? QStringLiteral("-")
                                                           : presented.detail));

    auto *redeliveredItem = new QTableWidgetItem(redelivered ? utf16(u"c\u00F3") : QString());
    redeliveredItem->setTextAlignment(Qt::AlignCenter);
    table_->setItem(row, 6, redeliveredItem);

    auto *ackItem = new QTableWidgetItem(acked ? utf16(u"\u2713") : QStringLiteral("-"));
    ackItem->setTextAlignment(Qt::AlignCenter);
    table_->setItem(row, 7, ackItem);

    if (direction == QStringLiteral("received")) {
        ++receivedCount_;
        if (waitingCount_ > 0) {
            --waitingCount_;
        }
        updateCounters();
    }
}

void MqTab::loadRecent() {
    worker_.call(
        [store = store_]() {
            return QVariant::fromValue(store->recent(kRecentRows));
        },
        this,
        [this](const QVariant &rows) {
            populateRows(rows);
            requestDbSize();
        });
}

void MqTab::applyFilter() {
    const QString type = filterTypeCombo_->currentData().toString();
    const QDateTime from = startOfDay(filterFromDate_->date());
    const QDateTime to = endOfDay(filterToDate_->date());
    const QString searchText = filterSearchEdit_->text().trimmed();
    const bool receivedOnly = filterReceivedOnlyCheck_->isChecked();

    worker_.call(
        [store = store_, type, from, to, searchText, receivedOnly]() {
            return QVariant::fromValue(
                store->search(type, from, to, searchText, receivedOnly));
        },
        this,
        [this](const QVariant &rows) {
            populateRows(rows);
            requestDbSize();
        });
}

void MqTab::populateRows(const QVariant &rows) {
    table_->setRowCount(0);
    nextSequence_ = 1;
    const QVector<StoredMessage> messages = rows.value<QVector<StoredMessage>>();
    for (const StoredMessage &message : messages) {
        PresentedMessage presented = presentOrFallback(message.envelopeType, message.payload);
        if (!message.displayTitle.isEmpty()) {
            presented.displayTitle = message.displayTitle;
        }
        insertRow(message.direction,
                  message.envelopeType,
                  presented,
                  message.redelivered,
                  message.acked);
    }
}

void MqTab::requestDbSize() {
    worker_.call(
        [store = store_]() { return QVariant(store->fileSizeBytes()); },
        this,
        [this](const QVariant &bytes) { updateDbSize(bytes.toLongLong()); });
}

void MqTab::updateDbSize(qint64 bytes) {
    const double mebibytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
    dbSizeLabel_->setText(QString("%1 tin  \xC2\xB7  CSDL %2 MB")
                              .arg(table_->rowCount())
                              .arg(mebibytes, 0, 'f', 1));

    const bool warning = bytes > settings_->dbSizeWarningThresholdBytes();
    dbSizeLabel_->setProperty("state", warning ? QStringLiteral("warn") : QString());
    UiStyle::repolish(dbSizeLabel_);
    purgeButton_->setVisible(warning);
    if (warning && !dbWarningLogged_) {
        AppLog::warn(QString("MQ database size %1 bytes exceeds warning threshold %2 bytes")
                         .arg(bytes)
                         .arg(settings_->dbSizeWarningThresholdBytes()));
    }
    dbWarningLogged_ = warning;
}

void MqTab::showPurgeDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(utf16(u"D\u1ECDn d\u1EEF li\u1EC7u"));
    auto *layout = new QFormLayout(&dialog);
    auto *fromEdit = new QDateEdit(QDate::currentDate().addMonths(-1), &dialog);
    auto *toEdit = new QDateEdit(QDate::currentDate(), &dialog);
    fromEdit->setCalendarPopup(true);
    toEdit->setCalendarPopup(true);
    fromEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    toEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    layout->addRow(utf16(u"T\u1EEB:"), fromEdit);
    layout->addRow(utf16(u"\u0110\u1EBFn:"), toEdit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QDateTime from = startOfDay(fromEdit->date());
    const QDateTime to = endOfDay(toEdit->date());
    worker_.call(
        [store = store_, from, to]() { return QVariant(store->purge(from, to)); },
        this,
        [this](const QVariant &) { loadRecent(); });
}

void MqTab::onDeliveryAcked(quint64 deliveryTag) {
    const QString envelopeId = pendingAckEnvelopeIds_.take(deliveryTag);
    if (envelopeId.isEmpty()) {
        return;
    }
    worker_.call(
        [store = store_, envelopeId]() {
            store->markAcked(envelopeId);
            return QVariant();
        },
        this,
        [](const QVariant &) {});
}

void MqTab::noteRejectedMessage(const QString &reason) {
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
    duplicateValue_->setText(QString::number(duplicateCount_));
}

void MqTab::setConnectionState(const QString &text, const char *state) {
    connectionLabel_->setText(text);
    connectionLabel_->setProperty("state", state);
    UiStyle::repolish(connectionLabel_);
}
