#pragma once

#include <QDialog>
#include <QTimer>

#include <memory>

class QLabel;
class QLineEdit;
class MqSettings;
class MqConnection;

// The "Cài đặt" dialog: edit broker connection settings and try them before saving.
//
// Save writes through MqSettings and takes effect on the next app start -- it does not
// attempt to reconfigure the live MqConnection. Nothing in this codebase has proven it safe
// to call MqConnection::connectToHost() again on an already-connected socket, and the
// reconnect machinery that exists (ConnectionState) is not wired to MqConnection yet.
// Test Connection opens a separate, temporary MqConnection instead, so neither path touches
// the app's live connection.
class MqSettingsDialog : public QDialog {
public:
    explicit MqSettingsDialog(MqSettings *settings, QWidget *parent = nullptr);
    ~MqSettingsDialog() override;

private:
    void save();
    void startTest();
    void pollTest();
    void finishTest(bool ok, const QString &message);

    MqSettings *settings_;
    QLineEdit *hostEdit_;
    QLineEdit *portEdit_;
    QLineEdit *vhostEdit_;
    QLineEdit *userEdit_;
    QLineEdit *passwordEdit_;
    QLabel *resultLabel_;
    std::unique_ptr<MqConnection> testConnection_;
    QTimer testPoll_;
    int testElapsedMs_ = 0;
};
