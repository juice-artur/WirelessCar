#include "NusClient.h"

#include "../util/HexUtils.h"

#include <QBluetoothAddress>

namespace {
const QBluetoothUuid kNusService(QStringLiteral("6e400001-b5a3-f393-e0a9-e50e24dcca9e"));
const QBluetoothUuid kNusRx(QStringLiteral("6e400002-b5a3-f393-e0a9-e50e24dcca9e"));
const QBluetoothUuid kNusTx(QStringLiteral("6e400003-b5a3-f393-e0a9-e50e24dcca9e"));
} // namespace

NusClient::NusClient(QObject *parent)
    : QObject(parent)
{
}

QString NusClient::stateText() const
{
    switch (m_state) {
    case Disconnected:
        return tr("Not connected");
    case Connecting:
        return tr("Connecting\u2026");
    case Connected:
        return tr("Connected");
    case Discovering:
        return tr("Discovering services\u2026");
    case Ready:
        return tr("Ready");
    case Error:
        return tr("Error");
    }
    return QString();
}

bool NusClient::canSend() const
{
    return m_state == Ready && m_service && m_rx.isValid();
}

void NusClient::connectToDevice(const QBluetoothDeviceInfo &info)
{
    if (m_controller)
        disconnectFromDevice();

    m_deviceName = info.name().isEmpty() ? info.address().toString() : info.name();
    m_address = info.address().toString();
    if (m_address.isEmpty() || m_address == QLatin1String("00:00:00:00:00:00"))
        m_address = info.deviceUuid().toString(QUuid::WithoutBraces);
    emit connectedNameChanged();

    m_mtu = 23;
    emit mtuSizeChanged();

    m_controller = QLowEnergyController::createCentral(info, this);
    connect(m_controller, &QLowEnergyController::connected, this, [this]() {
        setState(Discovering);
        m_controller->discoverServices();
    });
    connect(m_controller, &QLowEnergyController::disconnected, this, [this]() {
        resetConnection();
        setState(Disconnected);
    });
    connect(m_controller, &QLowEnergyController::serviceDiscovered,
            this, &NusClient::onServiceDiscovered);
    connect(m_controller, &QLowEnergyController::discoveryFinished, this, [this]() {
        if (!m_service) {
            emit errorOccurred(tr("NUS service not found on this device."));
            setState(Error);
        }
    });
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &NusClient::onControllerError);
    connect(m_controller, &QLowEnergyController::mtuChanged, this, [this](int mtu) {
        m_mtu = mtu;
        emit mtuSizeChanged();
    });

    setState(Connecting);
    m_controller->connectToDevice();
}

void NusClient::disconnectFromDevice()
{
    resetConnection();
    setState(Disconnected);
    m_deviceName.clear();
    m_address.clear();
    emit connectedNameChanged();
}

bool NusClient::sendHex(const QString &hex)
{
    QByteArray data;
    QString error;
    if (!HexUtils::fromHex(hex, &data, &error)) {
        emit errorOccurred(error);
        return false;
    }
    return sendBytes(data);
}

bool NusClient::sendBytes(const QByteArray &data)
{
    if (data.isEmpty())
        return false;
    if (!canSend()) {
        emit errorOccurred(tr("Cannot send: not connected to a NUS device."));
        return false;
    }

    m_txQueue.append(data);
    emit bytesSent(data);
    pumpQueue();
    return true;
}

void NusClient::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void NusClient::resetConnection()
{
    m_txQueue.clear();
    m_waitingForWrite = false;

    if (m_service) {
        m_service->disconnect(this);
        m_service->deleteLater();
        m_service = nullptr;
    }
    m_rx = QLowEnergyCharacteristic();
    m_tx = QLowEnergyCharacteristic();

    if (m_controller) {
        m_controller->disconnect(this);
        m_controller->disconnectFromDevice();
        m_controller->deleteLater();
        m_controller = nullptr;
    }
}

void NusClient::onServiceDiscovered(const QBluetoothUuid &uuid)
{
    if (uuid != kNusService || m_service)
        return;

    m_service = m_controller->createServiceObject(uuid, this);
    if (!m_service) {
        emit errorOccurred(tr("Failed to create the NUS service object."));
        setState(Error);
        return;
    }

    connect(m_service, &QLowEnergyService::stateChanged,
            this, &NusClient::onServiceStateChanged);
    connect(m_service, &QLowEnergyService::characteristicChanged,
            this, &NusClient::onCharacteristicChanged);
    connect(m_service, &QLowEnergyService::characteristicWritten,
            this, &NusClient::onCharacteristicWritten);
    connect(m_service, &QLowEnergyService::errorOccurred,
            this, &NusClient::onServiceError);

    m_service->discoverDetails();
}

void NusClient::onServiceStateChanged(QLowEnergyService::ServiceState state)
{
    if (state != QLowEnergyService::RemoteServiceDiscovered)
        return;

    m_rx = m_service->characteristic(kNusRx);
    m_tx = m_service->characteristic(kNusTx);

    if (!m_rx.isValid() || !m_tx.isValid()) {
        emit errorOccurred(tr("NUS RX/TX characteristics are missing."));
        setState(Error);
        return;
    }

    // Enable notifications on the TX characteristic (device -> host).
    const QLowEnergyDescriptor cccd = m_tx.descriptor(
        QBluetoothUuid(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration));
    if (cccd.isValid())
        m_service->writeDescriptor(cccd, QByteArray::fromHex("0100"));

    setState(Ready);
}

void NusClient::onCharacteristicChanged(const QLowEnergyCharacteristic &info, const QByteArray &value)
{
    if (info.uuid() == kNusTx)
        emit dataReceived(value);
}

void NusClient::onCharacteristicWritten(const QLowEnergyCharacteristic &info, const QByteArray &value)
{
    Q_UNUSED(info);
    Q_UNUSED(value);
    m_waitingForWrite = false;
    pumpQueue();
}

void NusClient::onControllerError(QLowEnergyController::Error error)
{
    emit errorOccurred(controllerErrorText(error));
    if (m_state == Connecting || m_state == Discovering)
        setState(Error);
}

void NusClient::onServiceError(QLowEnergyService::ServiceError error)
{
    if (error == QLowEnergyService::NoError)
        return;
    if (error == QLowEnergyService::CharacteristicWriteError)
        m_waitingForWrite = false;
    emit errorOccurred(tr("NUS service error (%1).").arg(static_cast<int>(error)));
}

void NusClient::pumpQueue()
{
    if (m_waitingForWrite || !m_service || m_txQueue.isEmpty() || !m_rx.isValid())
        return;

    const bool withoutResponse = m_rx.properties() & QLowEnergyCharacteristic::WriteNoResponse;
    const int chunk = chunkSize();

    while (!m_txQueue.isEmpty()) {
        const QByteArray part = m_txQueue.left(chunk);
        m_txQueue.remove(0, part.size());
        m_service->writeCharacteristic(m_rx, part,
                                       withoutResponse ? QLowEnergyService::WriteWithoutResponse
                                                       : QLowEnergyService::WriteWithResponse);
        if (!withoutResponse) {
            m_waitingForWrite = true;
            return;
        }
    }
}

int NusClient::chunkSize() const
{
    if (m_mtu <= 23)
        return 20;
    return qBound(20, m_mtu - 3, 512);
}

QString NusClient::controllerErrorText(QLowEnergyController::Error error)
{
    switch (error) {
    case QLowEnergyController::NoError:
        return QString();
    case QLowEnergyController::UnknownError:
        return tr("Unknown Bluetooth error.");
    case QLowEnergyController::UnknownRemoteDeviceError:
        return tr("The remote device could not be found.");
    case QLowEnergyController::NetworkError:
        return tr("A network error occurred.");
    case QLowEnergyController::InvalidBluetoothAdapterError:
        return tr("Invalid Bluetooth adapter.");
    case QLowEnergyController::ConnectionError:
        return tr("Could not connect to the device.");
    case QLowEnergyController::AdvertisingError:
        return tr("Advertising error.");
    case QLowEnergyController::RemoteHostClosedError:
        return tr("The remote device closed the connection.");
    case QLowEnergyController::AuthorizationError:
        return tr("Bluetooth authorization failed.");
    case QLowEnergyController::MissingPermissionsError:
        return tr("Bluetooth permissions are missing.");
    case QLowEnergyController::RssiReadError:
        return tr("Could not read the RSSI.");
    }
    return tr("Bluetooth error.");
}
