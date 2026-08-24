#include "demowindow.h"

#include "app_log.h"
#include "app_settings.h"
#include "platform.h"
#include "thememanager.h"

#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
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
#include <QListWidget>
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
constexpr int kC3ViewDeviceInfo = 0;
constexpr int kC3ViewUsers = 1;
constexpr int kC3ViewTemplates = 2;
constexpr int kC3ViewTransaction = 3;
constexpr int kC3ViewRealtimeLog = 4;
constexpr int kC3ViewControl = 5;
constexpr int kC3ViewRecordCount = 6;
}  // namespace

DemoWindow::DemoWindow() {
    setWindowTitle("qt-app-base demo");
    resize(900, 480);

    m_workerLabel = new QLabel("Worker: idle");
    auto *btnBlock = new QPushButton("Run a 3s blocking call");

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);

    auto *navRow = new QWidget;
    auto *navRowLayout = new QHBoxLayout(navRow);
    navRowLayout->setContentsMargins(0, 0, 0, 0);

    // Fixed row indices, never dynamically added/removed -- only setRowHidden() changes.
    // 0=Modbus TCP, 1=C3 Protocol header/entry-point, 2..8=the 7 C3 sub-views (2 + kC3View*).
    // See spec.md D1/D2 of .plans/2026-08-22-demo-ui-sidebar.
    m_sidebar = new QListWidget;
    m_sidebar->setFixedWidth(190);
    m_sidebar->addItem("Modbus TCP");
    m_sidebar->addItem("C3 Protocol (ZKTeco)");
    m_sidebar->addItem("  Device Info");
    m_sidebar->addItem("  Users");
    m_sidebar->addItem("  Templates");
    m_sidebar->addItem("  Transaction");
    m_sidebar->addItem("  Real-time Log");
    m_sidebar->addItem("  Control");
    m_sidebar->addItem("  Record Count");
#ifdef BUILD_DEMO_MQ
    m_sidebar->addItem("RabbitMQ");
#endif
    for (int row = 2; row <= 8; ++row) {
        m_sidebar->setRowHidden(row, true);
    }
    navRowLayout->addWidget(m_sidebar);

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
    // Without this, the table's own content-driven minimum width (7 fixed columns) silently
    // overrides resize()'s own requested window size -- confirmed live on the real HMI
    // (F2 of .plans/2026-08-22-demo-ui-sidebar): the window grew back to ~1077px wide
    // regardless of what resize() asked for. This lets the table shrink and rely on its own
    // horizontal scrollbar for the rest, instead of forcing the window wider.
    m_modbusTable->setMinimumWidth(0);
    modbusPageLayout->addWidget(m_modbusTable);
    m_protocolStack->addWidget(modbusPage);

    // Page 2 -- C3 Protocol (ZKTeco). Mirrors the Modbus page's own shape: one connect row,
    // one mode dropdown, a shared table for the three fetch-once views, a dedicated page each
    // for the continuously-polled Real-time Log and for Control (never a table) -- see
    // spec.md D1 of .plans/2026-08-21-c3-demo-ui.
    auto *c3Page = new QWidget;
    auto *c3PageLayout = new QVBoxLayout(c3Page);
    c3PageLayout->setContentsMargins(0, 0, 0, 0);

    auto *c3ConnectRow = new QWidget;
    auto *c3ConnectLayout = new QHBoxLayout(c3ConnectRow);
    c3ConnectLayout->setContentsMargins(0, 0, 0, 0);
    c3ConnectLayout->addWidget(new QLabel("Host:"));
    m_c3Host = new QLineEdit("192.168.2.163");
    c3ConnectLayout->addWidget(m_c3Host);
    c3ConnectLayout->addWidget(new QLabel("Port:"));
    m_c3Port = new QSpinBox;
    m_c3Port->setRange(1, 65535);
    m_c3Port->setValue(4370);
    c3ConnectLayout->addWidget(m_c3Port);
    m_btnC3Connect = new QPushButton("Connect");
    c3ConnectLayout->addWidget(m_btnC3Connect);
    c3PageLayout->addWidget(c3ConnectRow);

    m_c3StateLabel = new QLabel("State: Disconnected");
    c3PageLayout->addWidget(m_c3StateLabel);

    m_c3ViewStack = new QStackedWidget;

    // Stack page 0 -- Device Info / Users / Templates share one Refresh button + table.
    auto *c3TablePage = new QWidget;
    auto *c3TablePageLayout = new QVBoxLayout(c3TablePage);
    c3TablePageLayout->setContentsMargins(0, 0, 0, 0);
    m_btnC3Refresh = new QPushButton("Refresh");
    c3TablePageLayout->addWidget(m_btnC3Refresh);
    m_c3Table = new QTableWidget;
    m_c3Table->verticalHeader()->setVisible(false);
    m_c3Table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_c3Table->setMinimumWidth(0);   // same reasoning as m_modbusTable above
    c3TablePageLayout->addWidget(m_c3Table);
    m_c3ViewStack->addWidget(c3TablePage);

    // Stack page 1 -- Real-time Log. Continuously polled, append-only -- see spec.md D1.
    auto *c3LogPage = new QWidget;
    auto *c3LogPageLayout = new QVBoxLayout(c3LogPage);
    c3LogPageLayout->setContentsMargins(0, 0, 0, 0);
    m_c3LogTable = new QTableWidget;
    m_c3LogTable->setColumnCount(7);
    m_c3LogTable->setHorizontalHeaderLabels(
        {"Time", "CardNo", "Pin", "DoorID", "EventType", "InOutState", "VerifyMode"});
    m_c3LogTable->verticalHeader()->setVisible(false);
    m_c3LogTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_c3LogTable->setMinimumWidth(0);   // same reasoning as m_modbusTable above
    c3LogPageLayout->addWidget(m_c3LogTable);
    m_c3ViewStack->addWidget(c3LogPage);

    // Stack page 2 -- Control. Deliberately never a table -- door/aux OUTPUT and
    // CANCEL_ALARM only; no Restart Device or Normal-Open-State control anywhere in this UI
    // (spec.md D1/D4 -- matches the backend CONTROL phase's own user-confirmed safety
    // boundary).
    auto *c3ControlPage = new QWidget;
    auto *c3ControlPageLayout = new QVBoxLayout(c3ControlPage);
    c3ControlPageLayout->setContentsMargins(0, 0, 0, 0);
    auto *c3ControlRow = new QWidget;
    auto *c3ControlRowLayout = new QHBoxLayout(c3ControlRow);
    c3ControlRowLayout->setContentsMargins(0, 0, 0, 0);
    c3ControlRowLayout->addWidget(new QLabel("Door Number:"));
    m_c3ControlDoor = new QSpinBox;
    m_c3ControlDoor->setRange(1, 4);
    m_c3ControlDoor->setValue(1);
    c3ControlRowLayout->addWidget(m_c3ControlDoor);
    c3ControlRowLayout->addWidget(new QLabel("Duration (s):"));
    m_c3ControlDuration = new QSpinBox;
    // 1-254 only -- 255 (stay open indefinitely) and 0 (close) are deliberately unreachable
    // from this UI; see spec.md D4. This button only ever performs a timed, self-closing
    // unlock.
    m_c3ControlDuration->setRange(1, 254);
    m_c3ControlDuration->setValue(3);
    c3ControlRowLayout->addWidget(m_c3ControlDuration);
    m_btnC3DoorOpen = new QPushButton("Open Door");
    c3ControlRowLayout->addWidget(m_btnC3DoorOpen);
    m_btnC3CancelAlarm = new QPushButton("Cancel Alarm");
    c3ControlRowLayout->addWidget(m_btnC3CancelAlarm);
    c3ControlPageLayout->addWidget(c3ControlRow);
    m_c3ControlResultLabel = new QLabel("Last result: -");
    c3ControlPageLayout->addWidget(m_c3ControlResultLabel);
    c3ControlPageLayout->addStretch(1);
    m_c3ViewStack->addWidget(c3ControlPage);

    // Stack page 3 -- Record Count. A single number, not a table -- gets its own page the
    // same way Control did (spec.md D1 of .plans/2026-08-22-c3-getdatacount-ui).
    auto *c3CountPage = new QWidget;
    auto *c3CountPageLayout = new QVBoxLayout(c3CountPage);
    c3CountPageLayout->setContentsMargins(0, 0, 0, 0);
    auto *c3CountRow = new QWidget;
    auto *c3CountRowLayout = new QHBoxLayout(c3CountRow);
    c3CountRowLayout->setContentsMargins(0, 0, 0, 0);
    c3CountRowLayout->addWidget(new QLabel("Table:"));
    m_c3CountTable = new QComboBox;
    // Exactly the 8 table names this session's own live GETDATACOUNT testing cross-validated
    // against a real panel -- a dropdown of known-good names, not free text (spec.md D2).
    m_c3CountTable->addItem("user");
    m_c3CountTable->addItem("userauthorize");
    m_c3CountTable->addItem("holiday");
    m_c3CountTable->addItem("timezone");
    m_c3CountTable->addItem("transaction");
    m_c3CountTable->addItem("firstcard");
    m_c3CountTable->addItem("multicard");
    m_c3CountTable->addItem("inoutfun");
    c3CountRowLayout->addWidget(m_c3CountTable);
    m_btnC3CountRefresh = new QPushButton("Refresh");
    c3CountRowLayout->addWidget(m_btnC3CountRefresh);
    c3CountPageLayout->addWidget(c3CountRow);
    m_c3CountResultLabel = new QLabel("Count: -");
    c3CountPageLayout->addWidget(m_c3CountResultLabel);
    c3CountPageLayout->addStretch(1);
    m_c3ViewStack->addWidget(c3CountPage);

    c3PageLayout->addWidget(m_c3ViewStack);
    m_protocolStack->addWidget(c3Page);

#ifdef BUILD_DEMO_MQ
    // Page 2 -- RabbitMQ. MqTab is the base's own shipped widget, reused whole (spec.md D1
    // of .plans/2026-08-23-demo-rabbitmq-tab) -- no new UI is designed here. The presenter
    // is deliberately generic (D4): the demo has no card-domain schema of its own to
    // interpret a payload with, so it shows the envelope's raw type and a compact rendering
    // of the JSON body, which is enough to prove routing/ack/reject/mandatory+recall() all
    // work end to end.
    const MessagePresenter mqPresenter = [](const QString &type, const QJsonObject &payload) {
        const QString compact = QString::fromUtf8(
            QJsonDocument(payload).toJson(QJsonDocument::Compact));
        return PresentedMessage{type, compact, compact};
    };
    m_mqTab = new MqTab(&m_mqConnection, &m_mqService, &m_mqSettings, &m_mqStore, mqPresenter,
                        m_mqSettings.host(), m_mqSettings.port(), m_mqSettings.vhost());
    m_protocolStack->addWidget(m_mqTab);
#endif

    navRowLayout->addWidget(m_protocolStack);
    layout->addWidget(navRow);

    connect(m_sidebar, &QListWidget::currentRowChanged,
            this, &DemoWindow::onSidebarRowChanged);

#ifdef BUILD_DEMO_MQ
    connect(&m_mqService, &MqService::errorOccurred, this, [this](const QString &message) {
        if (m_mqTab) {
            m_mqTab->setConnectionError(message);
        }
    });
    m_mqConnection.connectToHost(m_mqSettings.host(), m_mqSettings.port(),
                                 m_mqSettings.vhost(), m_mqSettings.user(),
                                 m_mqSettings.password());
#endif

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
    auto *systemInfoAction = viewMenu->addAction("&System Info");
    connect(systemInfoAction, &QAction::triggered, this, &DemoWindow::showSystemInfo);

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

    connect(m_btnC3Refresh, &QPushButton::clicked, this, [this]() { refreshC3View(); });
    connect(m_btnC3Connect, &QPushButton::clicked, this, &DemoWindow::onC3ConnectClicked);
    connect(m_btnC3DoorOpen, &QPushButton::clicked, this, [this]() {
        if (!m_c3.isReady()) return;
        m_c3.controlDoorOutput(static_cast<uint8_t>(m_c3ControlDoor->value()),
                               static_cast<uint8_t>(m_c3ControlDuration->value()));
        m_c3ControlResultLabel->setText("Last result: sent, waiting...");
    });
    connect(m_btnC3CancelAlarm, &QPushButton::clicked, this, [this]() {
        if (!m_c3.isReady()) return;
        m_c3.cancelAlarm();
        m_c3ControlResultLabel->setText("Last result: sent, waiting...");
    });
    connect(m_btnC3CountRefresh, &QPushButton::clicked, this, [this]() { refreshC3RecordCount(); });
    connect(m_c3CountTable, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshC3RecordCount(); });

    connect(&m_c3, &ProtocolDriver::connectionStateChanged, this, [this](bool) {
        updateC3Labels();
    });
    connect(&m_c3, &C3Connection::deviceParamsReceived, this, [this](QVariantMap values) {
        rebuildC3TableForKeyValueMap(values);
    });
    connect(&m_c3, &C3Connection::deviceParamsFailed, this, [this](QString error) {
        m_c3Table->setColumnCount(1);
        m_c3Table->setHorizontalHeaderLabels({"Status"});
        m_c3Table->setRowCount(1);
        m_c3Table->setItem(0, 0, new QTableWidgetItem("Failed: " + error));
    });
    connect(&m_c3, &C3Connection::tableDataReceived, this,
            [this](QString /*tableName*/, QVector<QVariantMap> records) {
        rebuildC3TableForRecords(records);
    });
    connect(&m_c3, &C3Connection::tableDataFailed, this,
            [this](QString /*tableName*/, QString error) {
        m_c3Table->setColumnCount(1);
        m_c3Table->setHorizontalHeaderLabels({"Status"});
        m_c3Table->setRowCount(1);
        m_c3Table->setItem(0, 0, new QTableWidgetItem("Failed: " + error));
    });
    connect(&m_c3, &C3Connection::realtimeLogReceived, this,
            [this](QVector<C3Codec::RtLogRecord> records) {
        // Card-swipe events only -- see spec.md D5. DoorAlarmStatus records and any Event
        // record with no card number (e.g. a PIN-only entry) are dropped here, in the demo's
        // own display layer; C3Connection keeps emitting every decoded record unchanged.
        for (const C3Codec::RtLogRecord &record : records) {
            if (record.kind != C3Codec::RtLogRecordKind::Event || record.cardNo == 0) {
                continue;
            }
            appendC3LogRow({C3Codec::decodeC3DateTime(record.timeSecond),
                            QString::number(record.cardNo), QString::number(record.pin),
                            QString::number(record.doorId), QString::number(record.eventType),
                            QString::number(record.inOutState),
                            QString::number(record.verified)});
        }
    });
    connect(&m_c3, &C3Connection::realtimeLogKeyValueReceived, this,
            [this](QVector<QVariantMap> records) {
        // Card-swipe events only -- see spec.md D5. A record with no "cardno" key, or one
        // that parses to zero, is dropped here, in the demo's own display layer;
        // C3Connection keeps emitting every decoded record unchanged.
        for (const QVariantMap &record : records) {
            bool ok = false;
            const qulonglong cardNo = record.value("cardno").toULongLong(&ok);
            if (!ok || cardNo == 0) {
                continue;
            }
            const QString time = record.value("time").toString();
            // Only Time/CardNo are reliably present in key/value mode (an "un-interpreted"
            // map -- see c3_codec.h's own RtLogKeyValueResponse note); the other 5 columns
            // stay "-" rather than guessing a key name that may not exist.
            appendC3LogRow({time.isEmpty() ? "-" : time, QString::number(cardNo),
                            "-", "-", "-", "-", "-"});
        }
    });
    connect(&m_c3, &C3Connection::controlAcknowledged, this, [this]() {
        m_c3ControlResultLabel->setText("Last result: acknowledged");
    });
    connect(&m_c3, &C3Connection::controlFailed, this, [this](QString error) {
        m_c3ControlResultLabel->setText("Last result: failed -- " + error);
    });
    connect(&m_c3, &C3Connection::tableRecordCountReceived, this,
            [this](QString /*tableName*/, quint32 count) {
        m_c3CountResultLabel->setText(QString("Count: %1").arg(count));
    });
    connect(&m_c3, &C3Connection::tableRecordCountFailed, this,
            [this](QString tableName, QString error) {
        m_c3CountResultLabel->setText(QString("Count: failed -- %1 (%2)").arg(error, tableName));
    });

    m_c3LogPollTimer = new QTimer(this);
    m_c3LogPollTimer->setInterval(2000);
    connect(m_c3LogPollTimer, &QTimer::timeout, this, &DemoWindow::pollC3RealtimeLog);
    m_c3LogPollTimer->start();

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
    m_sidebar->setCurrentRow(0);
    updateC3Labels();

    // Overrides whatever minimum size the layout would otherwise compute from its
    // widest row of controls -- confirmed live on the real HMI (F2 of
    // .plans/2026-08-22-demo-ui-sidebar) that without this, the window's own X11 size
    // hints reported a ~1077px-wide floor no matter what resize() requested, even after
    // T19 already removed the three tables' own contribution to it. This is the
    // authoritative, last-word override for a QMainWindow; content that still doesn't
    // fit within the resulting size relies on individual widgets' own scrollbars.
    setMinimumSize(1, 1);
}

void DemoWindow::raiseToFront() {
    if (isMinimized()) {
        showNormal();
    }
    raise();
    activateWindow();
}

void DemoWindow::showSystemInfo() {
    // Queried on demand, not cached from startup -- so the numbers reflect right now, not
    // whatever they were when the app launched (spec.md D6 of
    // .plans/2026-08-22-resource-check).
    const Platform::SystemInfo info = Platform::querySystemInfo();
    const Platform::SystemResources resources =
        Platform::checkSystemResources(QFileInfo(AppLog::filePath()).absolutePath());

    QDialog dialog(this);
    dialog.setWindowTitle("System Info");
    auto *form = new QFormLayout(&dialog);
    form->addRow("Chip:", new QLabel(info.chipName));
    form->addRow("Architecture:", new QLabel(info.architecture));
    form->addRow("OS:", new QLabel(info.osVersion));
    form->addRow("RAM:", new QLabel(QString("%1 / %2 MB available")
        .arg(resources.availableRamBytes / (1024 * 1024))
        .arg(resources.totalRamBytes / (1024 * 1024))));
    form->addRow("Disk:", new QLabel(QString("%1 / %2 MB available")
        .arg(resources.availableDiskBytes / (1024 * 1024))
        .arg(resources.totalDiskBytes / (1024 * 1024))));
    auto *closeButton = new QPushButton("Close");
    form->addRow(closeButton);
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
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

void DemoWindow::updateC3Labels() {
    const bool ready = m_c3.isReady();
    m_c3StateLabel->setText(QString("State: ") + (ready ? "Connected" : "Disconnected"));
    m_btnC3Connect->setText(ready ? "Disconnect" : "Connect");
    if (!ready) {
        m_c3Table->setRowCount(0);
        m_c3LogTable->setRowCount(0);
        m_c3ControlResultLabel->setText("Last result: -");
    } else {
        refreshC3View();
    }
}

void DemoWindow::onC3ConnectClicked() {
    if (m_c3.isReady()) {
        m_c3.disconnectNow();
    } else {
        m_c3.connectToHost(m_c3Host->text(), static_cast<quint16>(m_c3Port->value()));
    }
    updateC3Labels();
}

void DemoWindow::onSidebarRowChanged(int row) {
    // row==1's own branch defers its setCurrentRow() call via QTimer::singleShot(0, ...)
    // rather than calling it directly from here -- see that branch's own comment. A
    // synchronous/reentrant call was tried first and shipped, then found live (F1) to
    // leave the sidebar's visual highlight stuck on row 1 despite the content pane
    // switching correctly -- a real Qt reentrancy hazard between nested calls to
    // setCurrentRow() from within a currentRowChanged handler, confirmed by testing, not
    // just theorized. No bool guard is needed for this deferred call either: by the time
    // the timer fires, this handler has already returned, so there is nothing to reenter.
    if (row < 0) {
        return;
    }
    if (row == 0) {
        // Modbus TCP -- collapse the C3 sub-view rows, matching spec.md D1.
        m_protocolStack->setCurrentIndex(0);
        for (int r = 2; r <= 8; ++r) {
            m_sidebar->setRowHidden(r, true);
        }
        return;
    }
    if (row == 1) {
        // C3 Protocol header -- unhide the sub-views and jump straight to whichever was
        // last active (spec.md D2/D3). setCurrentRow() is deferred to the next event-loop
        // iteration instead of called directly here -- a synchronous/reentrant call was
        // tried first and found live (F1 of .plans/2026-08-22-demo-ui-sidebar) to leave the
        // sidebar's own visual highlight stuck on row 1 even though the nested call's other
        // side effects (page switch, refresh) ran correctly. Deferring lets this call's own
        // currentRowChanged processing fully finish first, avoiding the reentrancy.
        for (int r = 2; r <= 8; ++r) {
            m_sidebar->setRowHidden(r, false);
        }
        const int target = 2 + m_lastC3ViewIndex;
        // Guarded: if the user has already navigated away from row 1 by the time this
        // fires (confirmed live, F1 of .plans/2026-08-22-demo-ui-sidebar, with rapid
        // repeated arrow-key presses), applying a now-stale jump would silently undo their
        // more recent navigation. Only apply it if row 1 is still actually current.
        QTimer::singleShot(0, this, [this, target]() {
            if (m_sidebar->currentRow() == 1) {
                m_sidebar->setCurrentRow(target);
            }
        });
        return;
    }
#ifdef BUILD_DEMO_MQ
    if (row == 9) {
        // RabbitMQ -- one page, no sub-view tree, mirrors row 0's pattern (spec.md D5 of
        // .plans/2026-08-23-demo-rabbitmq-tab).
        m_protocolStack->setCurrentIndex(2);
        for (int r = 2; r <= 8; ++r) {
            m_sidebar->setRowHidden(r, true);
        }
        return;
    }
#endif

    // One of the 7 C3 sub-view rows.
    const int index = row - 2;
    m_lastC3ViewIndex = index;
    m_protocolStack->setCurrentIndex(1);
    for (int r = 2; r <= 8; ++r) {
        m_sidebar->setRowHidden(r, false);
    }
    int stackIndex = 0;
    if (index == kC3ViewRealtimeLog) {
        stackIndex = 1;
    } else if (index == kC3ViewControl) {
        stackIndex = 2;
    } else if (index == kC3ViewRecordCount) {
        stackIndex = 3;
    }
    m_c3ViewStack->setCurrentIndex(stackIndex);
    if (stackIndex == 0) {
        refreshC3View();
    } else if (stackIndex == 3) {
        refreshC3RecordCount();
    }
}

void DemoWindow::refreshC3View() {
    if (!m_c3.isReady()) {
        return;
    }
    switch (m_lastC3ViewIndex) {
    case kC3ViewDeviceInfo:
        m_c3.requestDeviceParams({"~SerialNumber", "FirmVer", "DeviceName", "LockCount",
                                  "AuxInCount", "AuxOutCount"});
        break;
    case kC3ViewUsers:
        m_c3.requestTableData("user");
        break;
    case kC3ViewTemplates:
        m_c3.requestTableData("template");
        break;
    case kC3ViewTransaction:
        m_c3.requestTableData("transaction");
        break;
    default:
        break;   // Real-time Log and Control are not fetched this way
    }
}

void DemoWindow::refreshC3RecordCount() {
    if (!m_c3.isReady()) {
        return;
    }
    m_c3.requestTableRecordCount(m_c3CountTable->currentText());
}

void DemoWindow::pollC3RealtimeLog() {
    if (!m_c3.isReady() || m_lastC3ViewIndex != kC3ViewRealtimeLog) {
        return;
    }
    m_c3.requestRealtimeLog();
}

void DemoWindow::rebuildC3TableForKeyValueMap(const QVariantMap &values) {
    m_c3Table->setColumnCount(2);
    m_c3Table->setHorizontalHeaderLabels({"Parameter", "Value"});
    m_c3Table->setRowCount(values.size());
    int row = 0;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it, ++row) {
        m_c3Table->setItem(row, 0, new QTableWidgetItem(it.key()));
        m_c3Table->setItem(row, 1, new QTableWidgetItem(it.value().toString()));
    }
}

void DemoWindow::rebuildC3TableForRecords(const QVector<QVariantMap> &records) {
    if (records.isEmpty()) {
        m_c3Table->setColumnCount(1);
        m_c3Table->setHorizontalHeaderLabels({"Status"});
        m_c3Table->setRowCount(1);
        m_c3Table->setItem(0, 0, new QTableWidgetItem("No records returned by the panel"));
        return;
    }
    const QStringList keys = records.first().keys();
    m_c3Table->setColumnCount(keys.size());
    m_c3Table->setHorizontalHeaderLabels(keys);
    m_c3Table->setRowCount(records.size());
    for (int row = 0; row < records.size(); ++row) {
        for (int col = 0; col < keys.size(); ++col) {
            const QString &key = keys.at(col);
            const QVariant value = records.at(row).value(key);
            // Real calendar date instead of the raw encoded integer -- spec.md D9 of
            // .plans/2026-08-22-demo-ui-sidebar. Every other column stays raw.
            const QString text = (key == QLatin1String("Time_second"))
                ? C3Codec::decodeC3DateTime(value.toUInt())
                : value.toString();
            m_c3Table->setItem(row, col, new QTableWidgetItem(text));
        }
    }
}

void DemoWindow::appendC3LogRow(const QStringList &cells) {
    // Append-only, capped -- see spec.md's own Real-time Log design (D1): this view
    // behaves like an actual log, not a snapshot. The cap prevents unbounded growth on a
    // long-running HMI display. `cells` is always exactly 7 entries, matching the table's
    // own 7 columns (Time/CardNo/Pin/DoorID/EventType/InOutState/VerifyMode).
    constexpr int kMaxLogRows = 500;
    const int row = m_c3LogTable->rowCount();
    m_c3LogTable->insertRow(row);
    for (int col = 0; col < cells.size(); ++col) {
        m_c3LogTable->setItem(row, col, new QTableWidgetItem(cells.at(col)));
    }
    m_c3LogTable->scrollToBottom();
    while (m_c3LogTable->rowCount() > kMaxLogRows) {
        m_c3LogTable->removeRow(0);
    }
}
