#pragma once

#include <QMainWindow>

#include "modbus_connection.h"
#include "update_checker.h"
#include "update_installer.h"
#include "worker.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
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
    void updateModbusLabels();
    void rebuildModbusTableRows();
    void updateModbusTableValues(const QVector<uint16_t> &registers);
    void clearModbusTableValues();
    void pollModbusRegisters();

    Worker m_worker;
    UpdateChecker m_updateChecker;
    UpdateInstaller m_updateInstaller;

    ModbusConnection m_modbus;
    QTimer *m_modbusPollTimer = nullptr;
    QComboBox *m_protocolCombo = nullptr;
    QStackedWidget *m_protocolStack = nullptr;
    QLineEdit *m_modbusHost = nullptr;
    QSpinBox *m_modbusPort = nullptr;
    QSpinBox *m_modbusSlaveId = nullptr;
    QSpinBox *m_modbusRegisterStart = nullptr;
    QSpinBox *m_modbusCount = nullptr;
    QSpinBox *m_modbusPollRate = nullptr;
    QSpinBox *m_modbusWriteAddress = nullptr;
    QSpinBox *m_modbusWriteValue = nullptr;
    QPushButton *m_btnModbusWrite = nullptr;
    QLabel *m_modbusWriteResultLabel = nullptr;
    QPushButton *m_btnModbusConnect = nullptr;
    QPushButton *m_btnModbusDisconnect = nullptr;
    QLabel *m_modbusStateLabel = nullptr;
    QTableWidget *m_modbusTable = nullptr;

    QLabel *m_workerLabel = nullptr;
};
