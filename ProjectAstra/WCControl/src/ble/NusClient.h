#pragma once

#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QObject>
#include <QString>

#include <qqmlintegration.h>

// BLE central speaking the Nordic UART Service (NUS). Handles connect ->
// service discovery -> enabling notifications and exposes a simple byte pipe
// (sendBytes / dataReceived) that the OTA logic will later build upon.
class NusClient : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Access the client via AppController.nus")

public:
    enum State {
        Disconnected,
        Connecting,
        Connected,
        Discovering,
        Ready,
        Error,
    };
    Q_ENUM(State)

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(QString connectedName READ connectedName NOTIFY connectedNameChanged)
    Q_PROPERTY(QString remoteAddress READ remoteAddress NOTIFY connectedNameChanged)
    Q_PROPERTY(int mtuSize READ mtuSize NOTIFY mtuSizeChanged)
    Q_PROPERTY(bool canSend READ canSend NOTIFY stateChanged)

    explicit NusClient(QObject *parent = nullptr);

    State state() const { return m_state; }
    QString stateText() const;
    QString connectedName() const { return m_deviceName; }
    QString remoteAddress() const { return m_address; }
    int mtuSize() const { return m_mtu; }
    bool canSend() const;

    void connectToDevice(const QBluetoothDeviceInfo &info);
    Q_INVOKABLE void disconnectFromDevice();
    Q_INVOKABLE bool sendHex(const QString &hex);
    Q_INVOKABLE bool sendBytes(const QByteArray &data);

signals:
    void stateChanged();
    void connectedNameChanged();
    void mtuSizeChanged();
    void dataReceived(const QByteArray &data);
    void bytesSent(const QByteArray &data);
    void errorOccurred(const QString &message);

private:
    void setState(State state);
    void resetConnection();

    void onServiceDiscovered(const QBluetoothUuid &uuid);
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicChanged(const QLowEnergyCharacteristic &info, const QByteArray &value);
    void onCharacteristicWritten(const QLowEnergyCharacteristic &info, const QByteArray &value);
    void onControllerError(QLowEnergyController::Error error);
    void onServiceError(QLowEnergyService::ServiceError error);

    void pumpQueue();
    int chunkSize() const;

    static QString controllerErrorText(QLowEnergyController::Error error);

    QLowEnergyController *m_controller = nullptr;
    QLowEnergyService *m_service = nullptr;
    QLowEnergyCharacteristic m_rx;
    QLowEnergyCharacteristic m_tx;

    QByteArray m_txQueue;
    bool m_waitingForWrite = false;

    State m_state = Disconnected;
    QString m_deviceName;
    QString m_address;
    int m_mtu = 23;
};
