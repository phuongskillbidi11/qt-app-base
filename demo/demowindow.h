#pragma once

#include <QMainWindow>

#include "connection_state.h"
#include "modbus_connection.h"
#include "worker.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class QSpinBox;
class QStackedWidget;
class QTimer;

// The base's proof of life. Its fake-connection group has no device and no network: a fake
// "connection" that succeeds or fails on a toggle, driven through the real ConnectionState,
// and a fake blocking call driven through the real Worker. The Modbus TCP group is the
// base's first real network integration.
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
    void updateLabels();
    void updateModbusLabels();
    void pollModbusRegisters();

    ConnectionState m_connection;
    Worker m_worker;
    bool m_deviceReachable = false;

    ModbusConnection m_modbus;
    QTimer *m_modbusPollTimer = nullptr;
    QComboBox *m_protocolCombo = nullptr;
    QStackedWidget *m_protocolStack = nullptr;
    QLineEdit *m_modbusHost = nullptr;
    QSpinBox *m_modbusPort = nullptr;
    QPushButton *m_btnModbusConnect = nullptr;
    QPushButton *m_btnModbusDisconnect = nullptr;
    QLabel *m_modbusStateLabel = nullptr;
    QLabel *m_modbusRegistersLabel = nullptr;

    QLabel *m_stateLabel = nullptr;
    QLabel *m_workerLabel = nullptr;
    QPushButton *m_btnConnect = nullptr;
    QPushButton *m_btnDisconnect = nullptr;
    QPushButton *m_btnReachable = nullptr;
};
