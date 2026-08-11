#pragma once

#include <QMainWindow>

#include "connection_state.h"
#include "worker.h"

class QLabel;
class QPushButton;

// The base's proof of life. It has no device and no network: a fake "connection" that
// succeeds or fails on a toggle, driven through the real ConnectionState, and a fake
// blocking call driven through the real Worker.
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

    ConnectionState m_connection;
    Worker m_worker;
    bool m_deviceReachable = false;

    QLabel *m_stateLabel = nullptr;
    QLabel *m_workerLabel = nullptr;
    QPushButton *m_btnConnect = nullptr;
    QPushButton *m_btnDisconnect = nullptr;
    QPushButton *m_btnReachable = nullptr;
};
