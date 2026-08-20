#include "demowindow.h"

#include "app_log.h"
#include "app_settings.h"
#include "thememanager.h"

#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

DemoWindow::DemoWindow() {
    setWindowTitle("qt-app-base demo");
    resize(560, 320);

    m_workerLabel = new QLabel("Worker: idle");
    auto *btnBlock = new QPushButton("Run a 3s blocking call");

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);

    layout->addWidget(new QLabel("Protocol:"));
    m_protocolCombo = new QComboBox;
    m_protocolCombo->addItem("Modbus TCP");
    m_protocolCombo->addItem("RabbitMQ (not available in this build)");
    layout->addWidget(m_protocolCombo);

    m_protocolStack = new QStackedWidget;

    // Page 0 -- Modbus TCP. Identical to Step 4c's group, just moved into its own page's
    // layout instead of the window's top-level layout -- see spec.md R1.
    auto *modbusPage = new QWidget;
    auto *modbusPageLayout = new QVBoxLayout(modbusPage);
    modbusPageLayout->setContentsMargins(0, 0, 0, 0);
    modbusPageLayout->addWidget(new QLabel(
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
    modbusPageLayout->addWidget(modbusFieldsRow);

    auto *modbusPollFieldsRow = new QWidget;
    auto *modbusPollFieldsLayout = new QHBoxLayout(modbusPollFieldsRow);
    modbusPollFieldsLayout->setContentsMargins(0, 0, 0, 0);
    modbusPollFieldsLayout->addWidget(new QLabel("Slave Id:"));
    m_modbusSlaveId = new QSpinBox;
    m_modbusSlaveId->setRange(0, 255);
    m_modbusSlaveId->setValue(1);
    modbusPollFieldsLayout->addWidget(m_modbusSlaveId);
    modbusPollFieldsLayout->addWidget(new QLabel("Register Start:"));
    m_modbusRegisterStart = new QSpinBox;
    m_modbusRegisterStart->setRange(0, 65535);
    m_modbusRegisterStart->setValue(0);
    modbusPollFieldsLayout->addWidget(m_modbusRegisterStart);
    modbusPollFieldsLayout->addWidget(new QLabel("Count:"));
    m_modbusCount = new QSpinBox;
    m_modbusCount->setRange(1, 125);
    m_modbusCount->setValue(5);
    modbusPollFieldsLayout->addWidget(m_modbusCount);
    modbusPollFieldsLayout->addWidget(new QLabel("Poll Rate (ms):"));
    m_modbusPollRate = new QSpinBox;
    m_modbusPollRate->setRange(100, 60000);
    m_modbusPollRate->setValue(2000);
    modbusPollFieldsLayout->addWidget(m_modbusPollRate);
    modbusPageLayout->addWidget(modbusPollFieldsRow);

    m_btnModbusConnect = new QPushButton("Connect");
    m_btnModbusDisconnect = new QPushButton("Disconnect");
    modbusPageLayout->addWidget(m_btnModbusConnect);
    modbusPageLayout->addWidget(m_btnModbusDisconnect);

    m_modbusStateLabel = new QLabel("State: Disconnected");
    modbusPageLayout->addWidget(m_modbusStateLabel);

    m_modbusTable = new QTableWidget;
    m_modbusTable->setColumnCount(7);
    m_modbusTable->setHorizontalHeaderLabels(
        {"Address", "Register Value", "Big Endian", "Little Endian",
         "BE Swapped", "LE Swapped", "Name"});
    m_modbusTable->verticalHeader()->setVisible(false);
    m_modbusTable->setEditTriggers(QAbstractItemView::DoubleClicked
                                   | QAbstractItemView::EditKeyPressed);
    modbusPageLayout->addWidget(m_modbusTable);

    auto *modbusWriteRow = new QWidget;
    auto *modbusWriteLayout = new QHBoxLayout(modbusWriteRow);
    modbusWriteLayout->setContentsMargins(0, 0, 0, 0);
    modbusWriteLayout->addWidget(new QLabel("Write Single Register -- Address:"));
    m_modbusWriteAddress = new QSpinBox;
    m_modbusWriteAddress->setRange(0, 65535);
    modbusWriteLayout->addWidget(m_modbusWriteAddress);
    modbusWriteLayout->addWidget(new QLabel("Value:"));
    m_modbusWriteValue = new QSpinBox;
    m_modbusWriteValue->setRange(0, 65535);
    modbusWriteLayout->addWidget(m_modbusWriteValue);
    m_btnModbusWrite = new QPushButton("Write");
    modbusWriteLayout->addWidget(m_btnModbusWrite);
    modbusPageLayout->addWidget(modbusWriteRow);

    m_modbusWriteResultLabel = new QLabel("Last write: -");
    modbusPageLayout->addWidget(m_modbusWriteResultLabel);
    modbusPageLayout->addStretch(1);
    m_protocolStack->addWidget(modbusPage);

    // Page 1 -- RabbitMQ, placeholder only. Not wired -- see spec.md's build-dependency
    // finding (AMQP-CPP is not available to qt-app-base's own standalone build).
    auto *rabbitPage = new QWidget;
    auto *rabbitPageLayout = new QVBoxLayout(rabbitPage);
    rabbitPageLayout->setContentsMargins(0, 0, 0, 0);
    rabbitPageLayout->addWidget(new QLabel(
        "RabbitMQ is not available in this build yet -- qt-app-base's own standalone build\n"
        "has no AMQP-CPP vendored (only qt-mq-lab does). See\n"
        ".plans/2026-08-18-protocol-dropdown/spec.md."));
    rabbitPageLayout->addStretch(1);
    m_protocolStack->addWidget(rabbitPage);

    layout->addWidget(m_protocolStack);

    connect(m_protocolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            m_protocolStack, &QStackedWidget::setCurrentIndex);

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
    m_modbusPollTimer->setInterval(m_modbusPollRate->value());
    connect(m_modbusPollTimer, &QTimer::timeout, this, &DemoWindow::pollModbusRegisters);
    m_modbusPollTimer->start();
    connect(m_modbusPollRate, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int value) { m_modbusPollTimer->setInterval(value); });
    connect(m_modbusRegisterStart, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { rebuildModbusTableRows(); });
    connect(m_modbusCount, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { rebuildModbusTableRows(); });

    connect(&m_modbus, &ProtocolDriver::connectionStateChanged, this, [this](bool) {
        updateModbusLabels();
    });
    connect(&m_modbus, &ModbusConnection::holdingRegistersRead, this,
            [this](QVector<uint16_t> registers) {
        updateModbusTableValues(registers);
    });
    connect(&m_modbus, &ModbusConnection::readFailed, this, [this](const QString &) {
        clearModbusTableValues();
    });
    connect(&m_modbus, &ModbusConnection::singleRegisterWritten, this,
            [this](quint16 address, quint16 value) {
        m_modbusWriteResultLabel->setText(
            QString("Last write: address %1 = %2 (confirmed)").arg(address).arg(value));
    });
    connect(&m_modbus, &ModbusConnection::writeFailed, this, [this](const QString &error) {
        m_modbusWriteResultLabel->setText("Last write failed -- " + error);
    });
    connect(m_btnModbusWrite, &QPushButton::clicked, this, [this]() {
        m_modbus.writeSingleRegister(
            static_cast<quint8>(m_modbusSlaveId->value()),
            static_cast<quint16>(m_modbusWriteAddress->value()),
            static_cast<quint16>(m_modbusWriteValue->value()));
    });

    connect(m_btnModbusConnect, &QPushButton::clicked, this, [this]() {
        m_modbus.connectToHost(m_modbusHost->text(), static_cast<quint16>(m_modbusPort->value()));
        updateModbusLabels();
    });
    connect(m_btnModbusDisconnect, &QPushButton::clicked, this, [this]() {
        m_modbus.disconnectNow();
        updateModbusLabels();
    });

    connect(&m_updateChecker, &UpdateChecker::updateAvailable,
            &m_updateInstaller, &UpdateInstaller::download);
    connect(&m_updateInstaller, &UpdateInstaller::readyToInstall, this,
            [this](const QString &version) {
        QMessageBox question(QMessageBox::Question, "Update ready",
            QString("Version %1 is ready to install. Install now?").arg(version),
            QMessageBox::NoButton, this);
        QPushButton *installButton = question.addButton("Install now", QMessageBox::AcceptRole);
        question.addButton("Later", QMessageBox::RejectRole);
        question.exec();
        if (question.clickedButton() == installButton) {
            m_updateInstaller.applyAndRestart();
        }
    });
    connect(&m_updateInstaller, &UpdateInstaller::selfUpdateUnsupported, this,
            [this](const QString &installerPath) {
        QMessageBox::information(this, "Update downloaded",
            QString("A verified update was downloaded to:\n%1\n\n"
                    "This platform does not support automatic installation -- "
                    "install it manually.").arg(installerPath));
    });
    m_updateChecker.start();

    rebuildModbusTableRows();
    updateModbusLabels();
}

void DemoWindow::raiseToFront() {
    if (isMinimized()) {
        showNormal();
    }
    raise();
    activateWindow();
}

void DemoWindow::pollModbusRegisters() {
    if (!m_modbus.isReady()) {
        return;
    }
    m_modbus.readHoldingRegisters(
        static_cast<quint8>(m_modbusSlaveId->value()),
        static_cast<quint16>(m_modbusRegisterStart->value()),
        static_cast<quint16>(m_modbusCount->value()));
}

void DemoWindow::rebuildModbusTableRows() {
    const int start = m_modbusRegisterStart->value();
    const int count = m_modbusCount->value();
    const QString placeholder = "-";

    m_modbusTable->setRowCount(count);
    for (int row = 0; row < count; ++row) {
        auto *addressItem = new QTableWidgetItem(QString::number(start + row));
        addressItem->setFlags(addressItem->flags() & ~Qt::ItemIsEditable);
        m_modbusTable->setItem(row, 0, addressItem);

        for (int col = 1; col <= 5; ++col) {
            auto *item = new QTableWidgetItem(placeholder);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            m_modbusTable->setItem(row, col, item);
        }

        // Column 6 (Name) is intentionally left editable and not recreated on every poll --
        // see spec.md D5. It IS recreated here because a Register Start/Count change means
        // the row now refers to a genuinely different register; keeping an old typed name
        // pinned to a now-different address would be actively misleading.
        m_modbusTable->setItem(row, 6, new QTableWidgetItem);
    }
}

void DemoWindow::updateModbusTableValues(const QVector<uint16_t> &registers) {
    const QString placeholder = "-";
    const int rowCount = m_modbusTable->rowCount();

    for (int row = 0; row < registers.size() && row < rowCount; ++row) {
        const uint16_t value = registers.at(row);
        m_modbusTable->item(row, 1)->setText(QString::number(value));

        if (row + 1 < registers.size()) {
            const uint16_t next = registers.at(row + 1);
            const uint16_t valueSwapped = static_cast<uint16_t>((value << 8) | (value >> 8));
            const uint16_t nextSwapped = static_cast<uint16_t>((next << 8) | (next >> 8));

            const quint32 bigEndian = (static_cast<quint32>(value) << 16) | next;
            const quint32 littleEndian =
                (static_cast<quint32>(nextSwapped) << 16) | valueSwapped;
            const quint32 beSwapped = (static_cast<quint32>(next) << 16) | value;
            const quint32 leSwapped =
                (static_cast<quint32>(valueSwapped) << 16) | nextSwapped;

            m_modbusTable->item(row, 2)->setText(QString::number(bigEndian));
            m_modbusTable->item(row, 3)->setText(QString::number(littleEndian));
            m_modbusTable->item(row, 4)->setText(QString::number(beSwapped));
            m_modbusTable->item(row, 5)->setText(QString::number(leSwapped));
        } else {
            // Last row of an odd Count -- no register N+1 exists in this read to pair with.
            // See spec.md D3: show the placeholder, do not guess.
            for (int col = 2; col <= 5; ++col) {
                m_modbusTable->item(row, col)->setText(placeholder);
            }
        }
    }
}

void DemoWindow::clearModbusTableValues() {
    const QString placeholder = "-";
    for (int row = 0; row < m_modbusTable->rowCount(); ++row) {
        for (int col = 1; col <= 5; ++col) {
            m_modbusTable->item(row, col)->setText(placeholder);
        }
    }
}

void DemoWindow::updateModbusLabels() {
    const bool ready = m_modbus.isReady();
    m_modbusStateLabel->setText(QString("State: ") + (ready ? "Connected" : "Disconnected"));
    m_btnModbusConnect->setEnabled(!ready);
    m_btnModbusDisconnect->setEnabled(ready);
    if (!ready) {
        clearModbusTableValues();
    }
}
