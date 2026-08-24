#pragma once

#include <QMainWindow>
#include <QVariant>

#include "c3_connection.h"
#include "modbus_connection.h"
#include "update_checker.h"
#include "update_installer.h"
#include "worker.h"

#ifdef BUILD_DEMO_MQ
#include "mq_connection.h"
#include "mq_service.h"
#include "mq_settings.h"
#include "mq_store.h"
#include "mq_tab.h"
#endif

class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class QListWidget;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTimer;

// The base's proof of life. The Modbus TCP group is the base's real network integration,
// live-tested against both a local pymodbus simulator and a real Siemens PLC. The Worker
// group below it is a separate proof: a fake 3-second blocking call driven through the real
// Worker, showing the UI thread never freezes.
//
// If the window opens styled, the theme toggles, a second launch raises this one instead
// of starting a rival, the log fills, and the Block 3s button leaves the UI responsive —
// then the base works, and an application can start from here.
class DemoWindow : public QMainWindow {
    Q_OBJECT
public:
    DemoWindow();

public slots:
    void raiseToFront();

private:
    void showSystemInfo();
    void updateModbusLabels();
    void rebuildModbusTableRows();
    void updateModbusRegisterTableValues(const QVector<uint16_t> &registers);
    void updateModbusCoilTableValues(const QVector<bool> &bits);
    void clearModbusTableValues();
    void pollModbusRegisters();
    void onModbusFunctionChanged();
    void onModbusConnectClicked();
    void onModbusTableDoubleClicked(int row, int column);
    bool isModbusCoilFunction() const;
    bool isModbusWriteFunction() const;
    void updateC3Labels();
    void onC3ConnectClicked();
    void onSidebarRowChanged(int row);
    void refreshC3View();
    void refreshC3RecordCount();
    void pollC3RealtimeLog();
    void rebuildC3TableForKeyValueMap(const QVariantMap &values);
    void rebuildC3TableForRecords(const QVector<QVariantMap> &records);
    void appendC3LogRow(const QStringList &cells);

    Worker m_worker;
    UpdateChecker m_updateChecker;
    UpdateInstaller m_updateInstaller;

    ModbusConnection m_modbus;
    QTimer *m_modbusPollTimer = nullptr;
    QListWidget *m_sidebar = nullptr;
    QStackedWidget *m_protocolStack = nullptr;
    QLineEdit *m_modbusHost = nullptr;
    QSpinBox *m_modbusPort = nullptr;
    QSpinBox *m_modbusTimeout = nullptr;
    QPushButton *m_btnModbusConnect = nullptr;
    QSpinBox *m_modbusSlaveId = nullptr;
    QSpinBox *m_modbusRegisterStart = nullptr;
    QSpinBox *m_modbusCount = nullptr;
    QSpinBox *m_modbusPollRate = nullptr;
    QComboBox *m_modbusFunction = nullptr;
    QLabel *m_modbusStateLabel = nullptr;
    QLabel *m_modbusWriteResultLabel = nullptr;
    QTableWidget *m_modbusTable = nullptr;

    C3Connection m_c3;
    QLineEdit *m_c3Host = nullptr;
    QSpinBox *m_c3Port = nullptr;
    QPushButton *m_btnC3Connect = nullptr;
    int m_lastC3ViewIndex = 0;
    QLabel *m_c3StateLabel = nullptr;
    QStackedWidget *m_c3ViewStack = nullptr;
    QPushButton *m_btnC3Refresh = nullptr;
    QTableWidget *m_c3Table = nullptr;
    QTableWidget *m_c3LogTable = nullptr;
    QTimer *m_c3LogPollTimer = nullptr;
    QSpinBox *m_c3ControlDoor = nullptr;
    QSpinBox *m_c3ControlDuration = nullptr;
    QPushButton *m_btnC3DoorOpen = nullptr;
    QPushButton *m_btnC3CancelAlarm = nullptr;
    QLabel *m_c3ControlResultLabel = nullptr;
    QComboBox *m_c3CountTable = nullptr;
    QPushButton *m_btnC3CountRefresh = nullptr;
    QLabel *m_c3CountResultLabel = nullptr;

    QLabel *m_workerLabel = nullptr;

#ifdef BUILD_DEMO_MQ
    // See .plans/2026-08-23-demo-rabbitmq-tab/spec.md D1/D6. m_mqConnection must be declared
    // before m_mqService -- its in-class initializer takes m_mqConnection's address, and
    // members initialize in declaration order regardless of initializer-list order.
    MqSettings m_mqSettings;
    MqConnection m_mqConnection;
    MqService m_mqService{&m_mqConnection};
    MqStore m_mqStore;
    MqTab *m_mqTab = nullptr;
#endif
};
