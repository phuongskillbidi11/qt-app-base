#include "mq_settings_dialog.h"

#include "mq_connection.h"
#include "mq_settings.h"

#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
// Long enough for a real broker on an unfamiliar network; short enough that a wrong
// host/port doesn't leave the dialog looking frozen. MqConnection reports a socket error
// well before this in the failure case -- this timeout only matters when the host simply
// never answers.
constexpr int kTestTimeoutMs = 5000;
constexpr int kTestPollIntervalMs = 200;

QString utf16(const char16_t *text) {
    return QString::fromUtf16(reinterpret_cast<const ushort *>(text));
}
}  // namespace

MqSettingsDialog::MqSettingsDialog(MqSettings *settings, QWidget *parent)
    : QDialog(parent),
      settings_(settings),
      hostEdit_(new QLineEdit(this)),
      portEdit_(new QLineEdit(this)),
      vhostEdit_(new QLineEdit(this)),
      userEdit_(new QLineEdit(this)),
      passwordEdit_(new QLineEdit(this)),
      resultLabel_(new QLabel(this)) {
    setWindowTitle(utf16(u"C\u00E0i \u0111\u1EB7t k\u1EBFt n\u1ED1i"));

    // Names for UI-automation tooling (FlaUI, UI Automation) to find these controls by.
    hostEdit_->setObjectName("hostEdit");
    portEdit_->setObjectName("portEdit");
    vhostEdit_->setObjectName("vhostEdit");
    userEdit_->setObjectName("userEdit");
    passwordEdit_->setObjectName("passwordEdit");
    resultLabel_->setObjectName("resultLabel");

    hostEdit_->setText(settings_->host());
    portEdit_->setText(QString::number(settings_->port()));
    vhostEdit_->setText(settings_->vhost());
    userEdit_->setText(settings_->user());
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setPlaceholderText(utf16(u"\u0111\u1EC3 tr\u1ED1ng = gi\u1EEF nguy\u00EAn"));

    auto *form = new QFormLayout;
    form->addRow(utf16(u"Host"), hostEdit_);
    form->addRow(utf16(u"Port"), portEdit_);
    form->addRow(utf16(u"Vhost"), vhostEdit_);
    form->addRow(utf16(u"User"), userEdit_);
    form->addRow(utf16(u"Password"), passwordEdit_);

    auto *saveButton = new QPushButton(utf16(u"L\u01B0u"), this);
    saveButton->setObjectName("saveButton");
    auto *testButton = new QPushButton(utf16(u"Th\u1EED k\u1EBFt n\u1ED1i"), this);
    testButton->setObjectName("testButton");
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addWidget(saveButton);
    buttonRow->addWidget(testButton);
    buttonRow->addStretch();

    resultLabel_->setWordWrap(true);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(form);
    mainLayout->addLayout(buttonRow);
    mainLayout->addWidget(resultLabel_);

    connect(saveButton, &QPushButton::clicked, this, &MqSettingsDialog::save);
    connect(testButton, &QPushButton::clicked, this, &MqSettingsDialog::startTest);
    connect(&testPoll_, &QTimer::timeout, this, &MqSettingsDialog::pollTest);
    testPoll_.setInterval(kTestPollIntervalMs);
}

MqSettingsDialog::~MqSettingsDialog() = default;

void MqSettingsDialog::save() {
    settings_->setHost(hostEdit_->text());
    settings_->setPort(static_cast<quint16>(portEdit_->text().toUInt()));
    settings_->setVhost(vhostEdit_->text());
    settings_->setUser(userEdit_->text());
    settings_->setPassword(passwordEdit_->text());  // empty leaves the stored one alone

    resultLabel_->setText(
        utf16(u"\u0110\u00E3 l\u01B0u. Kh\u1EDFi \u0111\u1ED9ng l\u1EA1i \u1EE9ng d\u1EE5ng \u0111\u1EC3 \u00E1p d\u1EE5ng."));
}

void MqSettingsDialog::startTest() {
    // A blank password field means "use what is already saved" -- testing with a literal
    // empty string would fail even when the stored credential is fine.
    const QString password =
        passwordEdit_->text().isEmpty() ? settings_->password() : passwordEdit_->text();

    testConnection_ = std::make_unique<MqConnection>();
    testElapsedMs_ = 0;
    resultLabel_->setText(utf16(u"\u0110ang th\u1EED..."));

    testConnection_->connectToHost(hostEdit_->text(),
                                   static_cast<quint16>(portEdit_->text().toUInt()),
                                   vhostEdit_->text(),
                                   userEdit_->text(),
                                   password);
    testPoll_.start();
}

void MqSettingsDialog::pollTest() {
    if (!testConnection_) {
        testPoll_.stop();
        return;
    }
    if (!testConnection_->errorString().isEmpty()) {
        finishTest(false, testConnection_->errorString());
        return;
    }
    if (testConnection_->isReady()) {
        finishTest(true, utf16(u"K\u1EBFt n\u1ED1i v\u00E0 \u0111\u0103ng nh\u1EADp th\u00E0nh c\u00F4ng."));
        return;
    }
    testElapsedMs_ += kTestPollIntervalMs;
    if (testElapsedMs_ >= kTestTimeoutMs) {
        finishTest(false, utf16(u"H\u1EBFt th\u1EDDi gian ch\u1EDD ph\u1EA3n h\u1ED3i t\u1EEB broker."));
    }
}

void MqSettingsDialog::finishTest(bool ok, const QString &message) {
    testPoll_.stop();
    testConnection_.reset();  // never leaves a test connection behind
    resultLabel_->setText((ok ? utf16(u"\u2713 ") : utf16(u"\u2717 ")) + message);
}