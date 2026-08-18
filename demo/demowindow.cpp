#include "demowindow.h"

#include "app_log.h"
#include "app_settings.h"
#include "thememanager.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
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
    layout->addWidget(new QLabel(
        "Modbus TCP (real network -- needs a running device or the pymodbus dev simulator)"));

    auto *modbusFieldsRow = new QWidget;
    auto *modbusFieldsLayout = new QHBoxLayout(modbusFieldsRow);
    modbusFieldsLayout->setContentsMargins(0, 0, 0, 0);
    modbusFieldsLayout->addWidget(new QLabel("Host:"));
    m_modbusHost = new QLineEdit("127.0.0.1");
    modbusFieldsLayout->addWidget(m_modbusHost);
    modbusFieldsLayout->addWidget(new QLabel("Port:"));
    m_modbusPort = new QSpinBox;
    m_modbusPort->setRange(1, 65535);
    m_modbusPort->setValue(5020);
    modbusFieldsLayout->addWidget(m_modbusPort);
    layout->addWidget(modbusFieldsRow);

    m_btnModbusConnect = new QPushButton("Connect");
    m_btnModbusDisconnect = new QPushButton("Disconnect");
    layout->addWidget(m_btnModbusConnect);
    layout->addWidget(m_btnModbusDisconnect);

    m_modbusStateLabel = new QLabel("State: Disconnected");
    m_modbusRegistersLabel = new QLabel(QString::fromUtf8("Registers (unit 1, addr 0-4): \u2014"));
    layout->addWidget(m_modbusStateLabel);
    layout->addWidget(m_modbusRegistersLabel);
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

    m_modbusPollTimer = new QTimer(this);
    m_modbusPollTimer->setInterval(2000);
    connect(m_modbusPollTimer, &QTimer::timeout, this, &DemoWindow::pollModbusRegisters);
    m_modbusPollTimer->start();

    connect(&m_modbus, &ProtocolDriver::connectionStateChanged, this, [this](bool) {
        updateModbusLabels();
    });
    connect(&m_modbus, &ModbusConnection::holdingRegistersRead, this,
            [this](QVector<uint16_t> registers) {
        QStringList parts;
        for (uint16_t value : registers) {
            parts << QString::number(value);
        }
        m_modbusRegistersLabel->setText("Registers (unit 1, addr 0-4): " + parts.join(", "));
    });
    connect(&m_modbus, &ModbusConnection::readFailed, this, [this](const QString &error) {
        m_modbusRegistersLabel->setText("Registers (unit 1, addr 0-4): read failed -- " + error);
    });

    connect(m_btnModbusConnect, &QPushButton::clicked, this, [this]() {
        m_modbus.connectToHost(m_modbusHost->text(), static_cast<quint16>(m_modbusPort->value()));
        updateModbusLabels();
    });
    connect(m_btnModbusDisconnect, &QPushButton::clicked, this, [this]() {
        m_modbus.disconnectNow();
        updateModbusLabels();
    });

    updateModbusLabels();
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

void DemoWindow::pollModbusRegisters() {
    if (!m_modbus.isReady()) {
        return;
    }
    m_modbus.readHoldingRegisters(1, 0, 5);
}

void DemoWindow::updateModbusLabels() {
    const bool ready = m_modbus.isReady();
    m_modbusStateLabel->setText(QString("State: ") + (ready ? "Connected" : "Disconnected"));
    m_btnModbusConnect->setEnabled(!ready);
    m_btnModbusDisconnect->setEnabled(ready);
    if (!ready) {
        m_modbusRegistersLabel->setText(QString::fromUtf8("Registers (unit 1, addr 0-4): \u2014"));
    }
}
