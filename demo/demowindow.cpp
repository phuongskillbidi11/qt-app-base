#include "demowindow.h"

#include "app_log.h"
#include "app_settings.h"
#include "thememanager.h"

#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QDialog>
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

namespace {
// Index into m_modbusFunction -- see the addItem() calls below for the exact order.
constexpr int kFunctionReadCoils = 0;
constexpr int kFunctionReadDiscreteInputs = 1;
constexpr int kFunctionReadHoldingRegisters = 2;
constexpr int kFunctionReadInputRegisters = 3;
constexpr int kFunctionWriteSingleCoil = 4;
constexpr int kFunctionWriteSingleRegister = 5;
constexpr int kFunctionWriteMultipleCoils = 6;
constexpr int kFunctionWriteMultipleRegisters = 7;
}  // namespace

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
    // layout instead of the window's top-level layout -- see spec.md R1 of
    // .plans/2026-08-18-protocol-dropdown.
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
    modbusFieldsLayout->addWidget(new QLabel("Timeout (ms):"));
    m_modbusTimeout = new QSpinBox;
    m_modbusTimeout->setRange(100, 30000);
    m_modbusTimeout->setValue(1000);
    modbusFieldsLayout->addWidget(m_modbusTimeout);
    m_btnModbusConnect = new QPushButton("Connect");
    modbusFieldsLayout->addWidget(m_btnModbusConnect);
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
    modbusPollFieldsLayout->addWidget(new QLabel("Function:"));
    m_modbusFunction = new QComboBox;
    m_modbusFunction->addItem("01-Read Coils");
    m_modbusFunction->addItem("02-Read Discrete Inputs");
    m_modbusFunction->addItem("03-Read Holding Registers");
    m_modbusFunction->addItem("04-Read Input Registers");
    m_modbusFunction->addItem("05-Write Single Coil");
    m_modbusFunction->addItem("06-Write Single Register");
    m_modbusFunction->addItem("15-Write Multiple Coils");
    m_modbusFunction->addItem("16-Write Multiple Registers");
    m_modbusFunction->setCurrentIndex(kFunctionReadHoldingRegisters);
    modbusPollFieldsLayout->addWidget(m_modbusFunction);
    modbusPageLayout->addWidget(modbusPollFieldsRow);

    m_modbusStateLabel = new QLabel("State: Disconnected");
    modbusPageLayout->addWidget(m_modbusStateLabel);

    m_modbusWriteResultLabel = new QLabel("Last write: -");
    modbusPageLayout->addWidget(m_modbusWriteResultLabel);

    // Read functions (01/02/03/04) poll into this table; write functions (05/06/15/16)
    // repurpose it as a plain address picker -- double-click a row to open the write
    // dialog for that address (spec.md D3), no dedicated write page taking up space.
    m_modbusTable = new QTableWidget;
    m_modbusTable->setColumnCount(7);
    m_modbusTable->setHorizontalHeaderLabels(
        {"Address", "Register Value", "Big Endian", "Little Endian",
         "BE Swapped", "LE Swapped", "Name"});
    m_modbusTable->verticalHeader()->setVisible(false);
    m_modbusTable->setEditTriggers(QAbstractItemView::DoubleClicked
                                   | QAbstractItemView::EditKeyPressed);
    modbusPageLayout->addWidget(m_modbusTable);
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
    m_modbus.setRequestTimeoutMs(m_modbusTimeout->value());
    connect(m_modbusTimeout, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int value) { m_modbus.setRequestTimeoutMs(value); });
    connect(m_modbusRegisterStart, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { rebuildModbusTableRows(); });
    connect(m_modbusCount, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { rebuildModbusTableRows(); });
    connect(m_modbusFunction, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { onModbusFunctionChanged(); });
    connect(m_modbusTable, &QTableWidget::cellDoubleClicked,
            this, &DemoWindow::onModbusTableDoubleClicked);

    connect(&m_modbus, &ProtocolDriver::connectionStateChanged, this, [this](bool) {
        updateModbusLabels();
    });
    connect(&m_modbus, &ModbusConnection::holdingRegistersRead, this,
            [this](QVector<uint16_t> registers) {
        updateModbusRegisterTableValues(registers);
    });
    connect(&m_modbus, &ModbusConnection::inputRegistersRead, this,
            [this](QVector<uint16_t> registers) {
        updateModbusRegisterTableValues(registers);
    });
    connect(&m_modbus, &ModbusConnection::coilsRead, this,
            [this](QVector<bool> bits) { updateModbusCoilTableValues(bits); });
    connect(&m_modbus, &ModbusConnection::discreteInputsRead, this,
            [this](QVector<bool> bits) { updateModbusCoilTableValues(bits); });
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
    connect(&m_modbus, &ModbusConnection::multipleRegistersWritten, this,
            [this](quint16 startAddress, quint16 quantity) {
        m_modbusWriteResultLabel->setText(
            QString("Last write: %1 register(s) from address %2 (confirmed)")
                .arg(quantity).arg(startAddress));
    });
    connect(&m_modbus, &ModbusConnection::multipleWriteFailed, this,
            [this](const QString &error) {
        m_modbusWriteResultLabel->setText("Last write failed -- " + error);
    });
    connect(&m_modbus, &ModbusConnection::singleCoilWritten, this,
            [this](quint16 address, bool value) {
        m_modbusWriteResultLabel->setText(
            QString("Last write: coil %1 = %2 (confirmed)")
                .arg(address).arg(value ? "ON" : "OFF"));
    });
    connect(&m_modbus, &ModbusConnection::singleCoilWriteFailed, this,
            [this](const QString &error) {
        m_modbusWriteResultLabel->setText("Last write failed -- " + error);
    });
    connect(&m_modbus, &ModbusConnection::multipleCoilsWritten, this,
            [this](quint16 startAddress, quint16 quantity) {
        m_modbusWriteResultLabel->setText(
            QString("Last write: %1 coil(s) from address %2 (confirmed)")
                .arg(quantity).arg(startAddress));
    });
    connect(&m_modbus, &ModbusConnection::multipleCoilWriteFailed, this,
            [this](const QString &error) {
        m_modbusWriteResultLabel->setText("Last write failed -- " + error);
    });

    connect(m_btnModbusConnect, &QPushButton::clicked, this,
            &DemoWindow::onModbusConnectClicked);

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

    onModbusFunctionChanged();
    updateModbusLabels();
}

void DemoWindow::raiseToFront() {
    if (isMinimized()) {
        showNormal();
    }
    raise();
    activateWindow();
}

bool DemoWindow::isModbusCoilFunction() const {
    const int index = m_modbusFunction->currentIndex();
    return index == kFunctionReadCoils || index == kFunctionReadDiscreteInputs;
}

bool DemoWindow::isModbusWriteFunction() const {
    return m_modbusFunction->currentIndex() >= kFunctionWriteSingleCoil;
}

void DemoWindow::onModbusFunctionChanged() {
    if (isModbusWriteFunction()) {
        // No value column at all -- nothing is polled while a write function is selected
        // (spec.md D5 of .plans/2026-08-21-modbus-unified-function-ui), so the table is
        // just an address picker: double-click a row to open the write dialog for it.
        m_modbusTable->setColumnCount(2);
        m_modbusTable->setHorizontalHeaderLabels({"Address", "Name"});
    } else if (isModbusCoilFunction()) {
        m_modbusTable->setColumnCount(3);
        m_modbusTable->setHorizontalHeaderLabels({"Address", "Value", "Name"});
    } else {
        m_modbusTable->setColumnCount(7);
        m_modbusTable->setHorizontalHeaderLabels(
            {"Address", "Register Value", "Big Endian", "Little Endian",
             "BE Swapped", "LE Swapped", "Name"});
    }
    rebuildModbusTableRows();
}

void DemoWindow::onModbusConnectClicked() {
    if (m_modbus.isReady()) {
        m_modbus.disconnectNow();
    } else {
        m_modbus.connectToHost(m_modbusHost->text(), static_cast<quint16>(m_modbusPort->value()));
    }
    updateModbusLabels();
}

void DemoWindow::pollModbusRegisters() {
    if (!m_modbus.isReady()) {
        return;
    }
    const quint8 slaveId = static_cast<quint8>(m_modbusSlaveId->value());
    const quint16 start = static_cast<quint16>(m_modbusRegisterStart->value());
    const quint16 count = static_cast<quint16>(m_modbusCount->value());
    switch (m_modbusFunction->currentIndex()) {
    case kFunctionReadCoils:
        m_modbus.readCoils(slaveId, start, count);
        break;
    case kFunctionReadDiscreteInputs:
        m_modbus.readDiscreteInputs(slaveId, start, count);
        break;
    case kFunctionReadHoldingRegisters:
        m_modbus.readHoldingRegisters(slaveId, start, count);
        break;
    case kFunctionReadInputRegisters:
        m_modbus.readInputRegisters(slaveId, start, count);
        break;
    default:
        break;   // a write function is selected -- writes are one-shot, not polled
    }
}

void DemoWindow::onModbusTableDoubleClicked(int row, int /*column*/) {
    if (!isModbusWriteFunction() || !m_modbus.isReady()) {
        return;
    }
    const quint8 slaveId = static_cast<quint8>(m_modbusSlaveId->value());
    const quint16 address = static_cast<quint16>(m_modbusRegisterStart->value() + row);
    const int functionIndex = m_modbusFunction->currentIndex();
    const bool isMultiple = functionIndex == kFunctionWriteMultipleCoils
        || functionIndex == kFunctionWriteMultipleRegisters;

    QDialog dialog(this);
    dialog.setWindowTitle(m_modbusFunction->currentText());
    auto *dialogLayout = new QVBoxLayout(&dialog);
    dialogLayout->addWidget(new QLabel(
        QString(isMultiple ? "Start Address: %1" : "Address: %1").arg(address)));

    QComboBox *coilStateField = nullptr;
    QLineEdit *valueField = nullptr;
    if (functionIndex == kFunctionWriteSingleCoil) {
        dialogLayout->addWidget(new QLabel("State:"));
        coilStateField = new QComboBox;
        coilStateField->addItem("OFF");
        coilStateField->addItem("ON");
        dialogLayout->addWidget(coilStateField);
    } else if (functionIndex == kFunctionWriteSingleRegister) {
        dialogLayout->addWidget(new QLabel("Enter value (0-65535):"));
        valueField = new QLineEdit;
        dialogLayout->addWidget(valueField);
    } else if (functionIndex == kFunctionWriteMultipleCoils) {
        dialogLayout->addWidget(new QLabel("Values (comma-separated 0/1):"));
        valueField = new QLineEdit;
        dialogLayout->addWidget(valueField);
    } else if (functionIndex == kFunctionWriteMultipleRegisters) {
        dialogLayout->addWidget(new QLabel("Values (comma-separated 0-65535):"));
        valueField = new QLineEdit;
        dialogLayout->addWidget(valueField);
    } else {
        return;   // not a write function -- unreachable given the guard above
    }

    auto *errorLabel = new QLabel;
    errorLabel->setVisible(false);
    dialogLayout->addWidget(errorLabel);

    auto *buttonRow = new QWidget;
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    auto *cancelButton = new QPushButton("Cancel");
    auto *sendButton = new QPushButton("Send");
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(sendButton);
    dialogLayout->addWidget(buttonRow);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    // Validation stays inside the dialog (an inline error label, not a closed popup) --
    // the exact same per-function rules already proven in Steps 4m/4o, just triggered from
    // a modal Send button instead of a permanent one.
    connect(sendButton, &QPushButton::clicked, &dialog, [&]() {
        if (functionIndex == kFunctionWriteSingleCoil) {
            m_modbus.writeSingleCoil(slaveId, address, coilStateField->currentIndex() == 1);
            dialog.accept();
            return;
        }
        if (functionIndex == kFunctionWriteSingleRegister) {
            bool ok = false;
            const int value = valueField->text().trimmed().toInt(&ok);
            if (!ok || value < 0 || value > 65535) {
                errorLabel->setText("Value must be an integer 0-65535");
                errorLabel->setVisible(true);
                return;
            }
            m_modbus.writeSingleRegister(slaveId, address, static_cast<quint16>(value));
            dialog.accept();
            return;
        }
        if (functionIndex == kFunctionWriteMultipleCoils) {
            const QStringList parts = valueField->text().split(',', Qt::SkipEmptyParts);
            QVector<bool> coils;
            bool allValid = !parts.isEmpty() && parts.size() <= 1968;
            for (const QString &part : parts) {
                const QString trimmed = part.trimmed();
                if (trimmed == "1") {
                    coils.append(true);
                } else if (trimmed == "0") {
                    coils.append(false);
                } else {
                    allValid = false;
                    break;
                }
            }
            if (!allValid) {
                errorLabel->setText("Values must be 1-1968 comma-separated 0/1");
                errorLabel->setVisible(true);
                return;
            }
            m_modbus.writeMultipleCoils(slaveId, address, coils);
            dialog.accept();
            return;
        }
        // kFunctionWriteMultipleRegisters
        const QStringList parts = valueField->text().split(',', Qt::SkipEmptyParts);
        QVector<quint16> values;
        bool allValid = !parts.isEmpty() && parts.size() <= 123;
        for (const QString &part : parts) {
            bool ok = false;
            const int value = part.trimmed().toInt(&ok);
            if (!ok || value < 0 || value > 65535) {
                allValid = false;
                break;
            }
            values.append(static_cast<quint16>(value));
        }
        if (!allValid) {
            errorLabel->setText("Values must be 1-123 comma-separated integers 0-65535");
            errorLabel->setVisible(true);
            return;
        }
        m_modbus.writeMultipleRegisters(slaveId, address, values);
        dialog.accept();
    });

    dialog.exec();
}

void DemoWindow::rebuildModbusTableRows() {
    const int start = m_modbusRegisterStart->value();
    const int count = m_modbusCount->value();
    const QString placeholder = "-";
    const int nameCol = m_modbusTable->columnCount() - 1;

    m_modbusTable->setRowCount(count);
    for (int row = 0; row < count; ++row) {
        auto *addressItem = new QTableWidgetItem(QString::number(start + row));
        addressItem->setFlags(addressItem->flags() & ~Qt::ItemIsEditable);
        m_modbusTable->setItem(row, 0, addressItem);

        for (int col = 1; col < nameCol; ++col) {
            auto *item = new QTableWidgetItem(placeholder);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            m_modbusTable->setItem(row, col, item);
        }

        // The Name column is intentionally left editable and not recreated on every poll --
        // see spec.md D5 of .plans/2026-08-19-modbus-register-table. It IS recreated here
        // because a Register Start/Count/Function change means the row now refers to a
        // genuinely different address, keeping an old typed name pinned to a now-different
        // address would be actively misleading.
        m_modbusTable->setItem(row, nameCol, new QTableWidgetItem);
    }
}

void DemoWindow::updateModbusRegisterTableValues(const QVector<uint16_t> &registers) {
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
            // See spec.md D3 of .plans/2026-08-19-modbus-register-table: show the
            // placeholder, do not guess.
            for (int col = 2; col <= 5; ++col) {
                m_modbusTable->item(row, col)->setText(placeholder);
            }
        }
    }
}

void DemoWindow::updateModbusCoilTableValues(const QVector<bool> &bits) {
    const int rowCount = m_modbusTable->rowCount();
    for (int row = 0; row < bits.size() && row < rowCount; ++row) {
        m_modbusTable->item(row, 1)->setText(bits.at(row) ? "ON" : "OFF");
    }
}

void DemoWindow::clearModbusTableValues() {
    const QString placeholder = "-";
    const int nameCol = m_modbusTable->columnCount() - 1;
    for (int row = 0; row < m_modbusTable->rowCount(); ++row) {
        for (int col = 1; col < nameCol; ++col) {
            m_modbusTable->item(row, col)->setText(placeholder);
        }
    }
}

void DemoWindow::updateModbusLabels() {
    const bool ready = m_modbus.isReady();
    m_modbusStateLabel->setText(QString("State: ") + (ready ? "Connected" : "Disconnected"));
    m_btnModbusConnect->setText(ready ? "Disconnect" : "Connect");
    if (!ready) {
        clearModbusTableValues();
    }
}
