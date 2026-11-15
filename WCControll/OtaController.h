#pragma once

#include <QBluetoothDeviceInfo>
#include <QByteArray>
#include <QHash>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyService>
#include <QObject>
#include <QQueue>
#include <QUrl>
#include <QVariantList>

#include <QtQmlIntegration/qqmlintegration.h>

class QBluetoothDeviceDiscoveryAgent;
class QLowEnergyController;

/*
 * Talks to the nRF52840 BLE/UART bridge and drives the STM32 bootloader OTA
 * protocol (see Shared/Protocol/Include/AstraOtaProtocol.h).
 *
 * The bridge is a transparent byte pipe, so every frame is emitted as a
 * sequence of GATT writes and the replies arrive as notifications.
 */
class OtaController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString imageInfo READ imageInfo NOTIFY imageInfoChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)

public:
    explicit OtaController(QObject *parent = nullptr);
    ~OtaController() override;

    bool busy() const { return m_busy; }
    bool scanning() const { return m_scanning; }
    bool connected() const { return m_connected; }
    QVariantList devices() const { return m_devices; }
    QString statusText() const { return m_statusText; }
    QString imageInfo() const { return m_imageInfo; }
    qreal progress() const { return m_progress; }

    Q_INVOKABLE void startScan();
    Q_INVOKABLE void connectDevice(const QString &address);
    Q_INVOKABLE void disconnectDevice();
    Q_INVOKABLE bool loadImage(const QUrl &url);
    Q_INVOKABLE void startUpdate();

signals:
    void busyChanged();
    void scanningChanged();
    void connectedChanged();
    void devicesChanged();
    void statusTextChanged();
    void imageInfoChanged();
    void progressChanged();

    // Wakes waitForFrame() from the notification handler.
    void frameReceived();

private slots:
    void onDeviceDiscovered(const QBluetoothDeviceInfo &info);
    void onScanFinished();
    void onControllerConnected();
    void onControllerDisconnected();
    void onServiceDiscovered(const QBluetoothUuid &uuid);
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicChanged(const QLowEnergyCharacteristic &info,
                                 const QByteArray &value);

private:
    void appendLog(const QString &line);
    void setProgress(qreal value);
    void setBusy(bool busy);

    void resetLink();

    void parseRx(const QByteArray &data);
    bool waitForFrame(int timeoutMs, quint8 &type, QByteArray &payload);
    bool waitForAck(quint32 expectedNext, int timeoutMs);
    bool sendFrame(quint8 type, const QByteArray &payload);
    bool writeValue(const QLowEnergyCharacteristic &characteristic,
                    const QByteArray &value);
    bool waitMs(int ms);

    void runUpdate();
    bool waitForBootloader();
    bool downloadImage();

    QBluetoothDeviceDiscoveryAgent *m_agent = nullptr;
    QLowEnergyController *m_controller = nullptr;
    QLowEnergyService *m_service = nullptr;
    QLowEnergyCharacteristic m_rx;
    QLowEnergyCharacteristic m_tx;
    QLowEnergyCharacteristic m_ctrl;

    QHash<QString, QBluetoothDeviceInfo> m_knownDevices;
    QVariantList m_devices;

    QByteArray m_rxBuffer;
    QQueue<QPair<quint8, QByteArray>> m_frames;

    QByteArray m_code;
    quint32 m_version = 0;
    quint32 m_crc32 = 0;

    QString m_statusText;
    QString m_imageInfo;
    qreal m_progress = 0.0;

    bool m_scanning = false;
    bool m_connected = false;
    bool m_busy = false;
    bool m_writeOk = false;
};
