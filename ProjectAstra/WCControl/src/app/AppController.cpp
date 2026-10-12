#include "AppController.h"

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_devices(new BleDeviceModel(this))
    , m_nus(new NusClient(this))
    , m_log(new LogModel(this))
{
    connect(m_devices, &BleDeviceModel::errorOccurred, this, [this](const QString &message) {
        m_log->appendInfo(tr("Scan error: %1").arg(message), LogModel::Error);
    });

    connect(m_nus, &NusClient::bytesSent, this, [this](const QByteArray &data) {
        m_log->appendData(LogModel::Tx, data);
    });
    connect(m_nus, &NusClient::dataReceived, this, [this](const QByteArray &data) {
        m_log->appendData(LogModel::Rx, data);
    });
    connect(m_nus, &NusClient::errorOccurred, this, [this](const QString &message) {
        m_log->appendInfo(message, LogModel::Error);
    });
    connect(m_nus, &NusClient::stateChanged, this, [this] {
        m_log->appendInfo(tr("BLE state: %1").arg(m_nus->stateText()));
    });
}

void AppController::connectToDevice(int index)
{
    const QBluetoothDeviceInfo info = m_devices->deviceAt(index);
    if (!info.isValid()) {
        m_log->appendInfo(tr("No device selected."), LogModel::Warning);
        return;
    }

    const QString label = info.name().isEmpty() ? info.address().toString() : info.name();
    m_log->appendInfo(tr("Connecting to %1\u2026").arg(label));
    m_nus->connectToDevice(info);
}

void AppController::disconnect()
{
    m_nus->disconnectFromDevice();
}
