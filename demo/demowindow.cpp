#include "demowindow.h"

#include "app_log.h"
#include "app_settings.h"
#include "thememanager.h"

#include <QAction>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QStatusBar>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

DemoWindow::DemoWindow() {
    setWindowTitle("qt-app-base demo");
    resize(560, 320);

    m_stateLabel = new QLabel;
    m_workerLabel = new QLabel("Worker: idle");
    m_btnConnect = new QPushButton("Connect");
    m_btnDisconnect = new QPushButton("Disconnect");
    m_btnReachable = new QPushButton("Device: unreachable (click to toggle)");
    auto *btnBlock = new QPushButton("Run a 3s blocking call");

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(new QLabel(
        "No device, no network. The fake connection below runs through the real\n"
        "ConnectionState, and the blocking call through the real Worker."));
    layout->addWidget(m_stateLabel);
    layout->addWidget(m_btnConnect);
    layout->addWidget(m_btnDisconnect);
    layout->addWidget(m_btnReachable);
    layout->addSpacing(12);
    layout->addWidget(btnBlock);
    layout->addWidget(m_workerLabel);
    layout->addStretch(1);
    setCentralWidget(central);

    auto *viewMenu = menuBar()->addMenu("&View");
    auto *darkAction = viewMenu->addAction("&Dark mode");
    darkAction->setCheckable(true);
    darkAction->setChecked(ThemeManager::current() == ThemeManager::Dark);
    connect(darkAction, &QAction::toggled, this, [](bool dark) {
        const ThemeManager::Theme theme = dark ? ThemeManager::Dark : ThemeManager::Light;
        ThemeManager::apply(theme);
        ThemeManager::save(theme);
    });

    statusBar()->showMessage("Log: " + AppLog::filePath());

    // The two operations that would touch hardware in a real app. Here they are a bool.
    m_connection.setHandlers(
        [this]() { return m_deviceReachable; },
        [this]() { return m_deviceReachable; },
        []() {});
    m_connection.setHeartbeatIntervalMs(2000);

    connect(&m_connection, &ConnectionState::connectionStateChanged, this,
            [this](bool connected) {
        AppLog::info(QString("demo connection state: %1")
            .arg(connected ? "connected" : "disconnected"));
        updateLabels();
    });
    connect(&m_connection, &ConnectionState::reconnectingChanged, this,
            [this](bool) { updateLabels(); });

    connect(m_btnConnect, &QPushButton::clicked, this, [this]() {
        m_connection.beginAutoConnect();
        updateLabels();
    });
    connect(m_btnDisconnect, &QPushButton::clicked, this, [this]() {
        m_connection.disconnectNow();
        updateLabels();
    });
    connect(m_btnReachable, &QPushButton::clicked, this, [this]() {
        m_deviceReachable = !m_deviceReachable;
        m_btnReachable->setText(m_deviceReachable
            ? "Device: reachable (click to toggle)"
            : "Device: unreachable (click to toggle)");
    });

    connect(btnBlock, &QPushButton::clicked, this, [this, btnBlock]() {
        btnBlock->setEnabled(false);
        m_workerLabel->setText("Worker: running — this window must stay responsive");
        m_worker.call([]() -> QVariant {
            QThread::msleep(3000);
            return QVariant("done");
        }, this, [this, btnBlock](const QVariant &result) {
            m_workerLabel->setText("Worker: " + result.toString()
                                   + " — the window never froze");
            btnBlock->setEnabled(true);
        });
    });

    updateLabels();
}

void DemoWindow::raiseToFront() {
    if (isMinimized()) {
        showNormal();
    }
    raise();
    activateWindow();
}

void DemoWindow::updateLabels() {
    QString state = "Disconnected";
    if (m_connection.isConnected()) {
        state = "Connected";
    } else if (m_connection.isReconnecting()) {
        state = "Reconnecting… (backoff: 5s, 5s, 5s, 15s, 15s, 30s, 60s)";
    }
    m_stateLabel->setText("State: " + state);
    // Connect stays reachable whenever not connected, including mid-reconnect — the
    // original app disabled it there and left the user with no way to intervene.
    m_btnConnect->setEnabled(!m_connection.isConnected());
    m_btnDisconnect->setEnabled(m_connection.isConnected() || m_connection.isReconnecting());
}
