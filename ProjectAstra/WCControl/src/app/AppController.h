#pragma once

#include <QObject>

#include <qqmlintegration.h>

#include "../ble/BleDeviceModel.h"
#include "../ble/NusClient.h"
#include "../log/LogModel.h"

// QML singleton that owns the BLE device list, the NUS client and the byte log
// and wires them together.
class AppController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(BleDeviceModel *devices READ devices CONSTANT)
    Q_PROPERTY(NusClient *nus READ nus CONSTANT)
    Q_PROPERTY(LogModel *log READ log CONSTANT)

public:
    explicit AppController(QObject *parent = nullptr);

    BleDeviceModel *devices() const { return m_devices; }
    NusClient *nus() const { return m_nus; }
    LogModel *log() const { return m_log; }

    Q_INVOKABLE void connectToDevice(int index);
    Q_INVOKABLE void disconnect();

private:
    BleDeviceModel *m_devices = nullptr;
    NusClient *m_nus = nullptr;
    LogModel *m_log = nullptr;
};
