#include "OtaController.h"

#include "AstraCrc.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothUuid>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyController>
#include <QLowEnergyDescriptor>
#include <QLowEnergyService>
#include <QTimer>

namespace {

// Wire protocol, mirrored from Shared/Protocol/Include/AstraOtaProtocol.h.
constexpr char kSof = char(0xA5);
constexpr int kFrameOverhead = 6;
constexpr int kMaxPayload = 2048 + 8;
constexpr quint32 kBlockSize = 2048;

constexpr quint32 kManifestMagic = 0x41544F41u; // "AOTA"
constexpr quint32 kMaxCodeSize = 0x18800u;      // APP_MAX_CODE_SIZE

constexpr quint8 kMsgGetInfo = 0x02;
constexpr quint8 kMsgInfo = 0x82;
constexpr quint8 kMsgBegin = 0x10;
constexpr quint8 kMsgData = 0x11;
constexpr quint8 kMsgEnd = 0x12;
constexpr quint8 kMsgAbort = 0x13;
constexpr quint8 kMsgAck = 0x90;
constexpr quint8 kMsgStatus = 0x91;
constexpr quint8 kMsgDone = 0x92;

const QBluetoothUuid &serviceUuid()
{
    static const QBluetoothUuid uuid(
        QStringLiteral("6e400001-b5a3-f393-e0a9-e50e24dcca9e"));
    return uuid;
}

const QBluetoothUuid &rxUuid()
{
    static const QBluetoothUuid uuid(
        QStringLiteral("6e400002-b5a3-f393-e0a9-e50e24dcca9e"));
    return uuid;
}

const QBluetoothUuid &txUuid()
{
    static const QBluetoothUuid uuid(
        QStringLiteral("6e400003-b5a3-f393-e0a9-e50e24dcca9e"));
    return uuid;
}

const QBluetoothUuid &ctrlUuid()
{
    static const QBluetoothUuid uuid(
        QStringLiteral("6e400004-b5a3-f393-e0a9-e50e24dcca9e"));
    return uuid;
}

quint32 leU32(const QByteArray &data, qsizetype offset)
{
    return quint32(quint8(data[offset])) |
           (quint32(quint8(data[offset + 1])) << 8) |
           (quint32(quint8(data[offset + 2])) << 16) |
           (quint32(quint8(data[offset + 3])) << 24);
}

void appendLeU32(QByteArray &data, quint32 value)
{
    data.append(char(value & 0xFFu));
    data.append(char((value >> 8) & 0xFFu));
    data.append(char((value >> 16) & 0xFFu));
    data.append(char((value >> 24) & 0xFFu));
}

QString formatVersion(quint32 value)
{
    return QStringLiteral("%1.%2.%3")
        .arg(value / 10000u)
        .arg((value / 100u) % 100u)
        .arg(value % 100u);
}

QString statusToString(quint8 code)
{
    switch (code) {
    case 0:
        return QStringLiteral("OK");
    case 1:
        return QStringLiteral("wrong state");
    case 2:
        return QStringLiteral("bad size");
    case 3:
        return QStringLiteral("CRC mismatch");
    case 4:
        return QStringLiteral("Flash error");
    case 5:
        return QStringLiteral("bad offset");
    case 6:
        return QStringLiteral("bad frame");
    default:
        return QStringLiteral("error %1").arg(code);
    }
}

} // namespace

OtaController::OtaController(QObject *parent)
    : QObject(parent)
{
}

OtaController::~OtaController()
{
    resetLink();
}

void OtaController::appendLog(const QString &line)
{
    m_statusText += line;
    m_statusText += QLatin1Char('\n');
    emit statusTextChanged();
}

void OtaController::setProgress(qreal value)
{
    if (qFuzzyCompare(m_progress, value))
        return;

    m_progress = value;
    emit progressChanged();
}

void OtaController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;

    m_busy = busy;
    emit busyChanged();
}

/* ------------------------------------------------------------------ scanning */

void OtaController::startScan()
{
    if (m_busy)
        return;

    if (!m_agent) {
        m_agent = new QBluetoothDeviceDiscoveryAgent(this);
        m_agent->setLowEnergyDiscoveryTimeout(10000);

        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this,
                &OtaController::onDeviceDiscovered);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this,
                &OtaController::onScanFinished);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::canceled, this,
                &OtaController::onScanFinished);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
                [this](QBluetoothDeviceDiscoveryAgent::Error) {
                    appendLog(QStringLiteral("Bluetooth scan error: %1")
                                  .arg(m_agent->errorString()));
                });
    }

    if (m_agent->isActive())
        m_agent->stop();

    m_knownDevices.clear();
    m_devices.clear();
    emit devicesChanged();

    m_scanning = true;
    emit scanningChanged();
    appendLog(QStringLiteral("Scanning for BLE devices..."));
    m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void OtaController::onDeviceDiscovered(const QBluetoothDeviceInfo &info)
{
    if (!(info.coreConfigurations()
          & QBluetoothDeviceInfo::LowEnergyCoreConfiguration))
        return;

    QString const address = info.address().toString();
    if (m_knownDevices.contains(address))
        return;

    m_knownDevices.insert(address, info);

    QString const name =
        info.name().isEmpty() ? QStringLiteral("(unnamed)") : info.name();
    QString const label =
        QStringLiteral("%1  [%2]").arg(name, info.deviceUuid().toString().isEmpty()
                                                ? address
                                                : info.deviceUuid().toString());

    QVariantMap entry;
    entry.insert(QStringLiteral("name"), name);
    entry.insert(QStringLiteral("address"), address);
    entry.insert(QStringLiteral("rssi"), info.rssi());
    entry.insert(QStringLiteral("label"), label);

    m_devices.append(entry);
    emit devicesChanged();
}

void OtaController::onScanFinished()
{
    m_scanning = false;
    emit scanningChanged();
    appendLog(QStringLiteral("Scan finished, %1 device(s) found.")
                  .arg(m_devices.size()));
}

/* ---------------------------------------------------------------- connection */

void OtaController::connectDevice(const QString &address)
{
    if (m_busy)
        return;

    auto const it = m_knownDevices.constFind(address);
    if (it == m_knownDevices.constEnd()) {
        appendLog(QStringLiteral("Unknown device %1").arg(address));
        return;
    }

    if (m_agent && m_agent->isActive())
        m_agent->stop();

    resetLink();

    appendLog(QStringLiteral("Connecting to %1...").arg(it.value().name().isEmpty()
                                                          ? address
                                                          : it.value().name()));

    m_controller = QLowEnergyController::createCentral(it.value(), this);

    connect(m_controller, &QLowEnergyController::connected, this,
            &OtaController::onControllerConnected);
    connect(m_controller, &QLowEnergyController::disconnected, this,
            &OtaController::onControllerDisconnected);
    connect(m_controller, &QLowEnergyController::serviceDiscovered, this,
            &OtaController::onServiceDiscovered);
    connect(m_controller, &QLowEnergyController::discoveryFinished, this,
            [this]() {
                if (!m_service) {
                    appendLog(QStringLiteral("The Astra OTA service was not "
                                             "found on this device."));
                    disconnectDevice();
                }
            });
    connect(m_controller, &QLowEnergyController::errorOccurred, this,
            [this](QLowEnergyController::Error) {
                appendLog(QStringLiteral("Bluetooth error: %1")
                              .arg(m_controller->errorString()));
            });

    m_controller->connectToDevice();
}

void OtaController::onControllerConnected()
{
    appendLog(QStringLiteral("Connected, discovering services..."));
    m_controller->discoverServices();
}

void OtaController::onControllerDisconnected()
{
    appendLog(QStringLiteral("Device disconnected."));
    resetLink();
}

void OtaController::onServiceDiscovered(const QBluetoothUuid &uuid)
{
    if (uuid != serviceUuid() || m_service)
        return;

    appendLog(QStringLiteral("Found Astra OTA service."));
    m_service = m_controller->createServiceObject(uuid, this);

    connect(m_service, &QLowEnergyService::stateChanged, this,
            &OtaController::onServiceStateChanged);
    connect(m_service, &QLowEnergyService::characteristicChanged, this,
            &OtaController::onCharacteristicChanged);
    connect(m_service, &QLowEnergyService::errorOccurred, this,
            [this](QLowEnergyService::ServiceError error) {
                appendLog(QStringLiteral("GATT error (code %1).")
                              .arg(int(error)));
            });

    m_service->discoverDetails();
}

void OtaController::onServiceStateChanged(QLowEnergyService::ServiceState state)
{
    if (state != QLowEnergyService::RemoteServiceDiscovered)
        return;

    m_rx = m_service->characteristic(rxUuid());
    m_tx = m_service->characteristic(txUuid());
    m_ctrl = m_service->characteristic(ctrlUuid());

    if (!m_rx.isValid() || !m_tx.isValid()) {
        appendLog(QStringLiteral("Astra service is missing the UART "
                                 "characteristics."));
        resetLink();
        return;
    }

    QLowEnergyDescriptor const cccd = m_tx.clientCharacteristicConfiguration();
    if (cccd.isValid()) {
        m_service->writeDescriptor(cccd, QByteArray::fromHex("0100"));
    }

    m_connected = true;
    emit connectedChanged();
    appendLog(QStringLiteral("Ready. Select an .ota file and start the update."));
}

void OtaController::disconnectDevice()
{
    if (m_controller)
        m_controller->disconnectFromDevice();

    resetLink();
}

void OtaController::resetLink()
{
    if (m_service) {
        m_service->deleteLater();
        m_service = nullptr;
    }

    if (m_controller) {
        m_controller->deleteLater();
        m_controller = nullptr;
    }

    m_rx = QLowEnergyCharacteristic();
    m_tx = QLowEnergyCharacteristic();
    m_ctrl = QLowEnergyCharacteristic();

    m_rxBuffer.clear();
    m_frames.clear();

    if (m_connected) {
        m_connected = false;
        emit connectedChanged();
    }
}

/* --------------------------------------------------------------------- image */

bool OtaController::loadImage(const QUrl &url)
{
    if (m_busy)
        return false;

    QString const path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        appendLog(QStringLiteral("Cannot open %1: %2").arg(path, file.errorString()));
        return false;
    }

    QByteArray const blob = file.readAll();
    if (blob.size() < 16) {
        appendLog(QStringLiteral("%1 is too small to be an .ota file.")
                      .arg(QFileInfo(path).fileName()));
        return false;
    }

    if (leU32(blob, 0) != kManifestMagic) {
        appendLog(QStringLiteral("%1 is not an Astra .ota file (bad magic).")
                      .arg(QFileInfo(path).fileName()));
        return false;
    }

    quint32 const version = leU32(blob, 4);
    quint32 const codeSize = leU32(blob, 8);
    quint32 const expectedCrc = leU32(blob, 12);

    if (codeSize == 0 || codeSize > kMaxCodeSize || (codeSize % 8u) != 0u) {
        appendLog(QStringLiteral("Invalid image size %1 (slot holds %2).")
                      .arg(codeSize)
                      .arg(kMaxCodeSize));
        return false;
    }

    if (quint32(blob.size() - 16) != codeSize) {
        appendLog(QStringLiteral("Image is truncated: %1 bytes of code expected, "
                                 "%2 present.")
                      .arg(codeSize)
                      .arg(blob.size() - 16));
        return false;
    }

    QByteArray const code = blob.mid(16);
    quint32 const actualCrc = Astra::crc32(code);
    if (actualCrc != expectedCrc) {
        appendLog(QStringLiteral("CRC mismatch: file says 0x%1, computed 0x%2.")
                      .arg(expectedCrc, 8, 16, QLatin1Char('0'))
                      .arg(actualCrc, 8, 16, QLatin1Char('0')));
        return false;
    }

    m_code = code;
    m_version = version;
    m_crc32 = actualCrc;
    m_progress = 0.0;
    emit progressChanged();

    m_imageInfo = QStringLiteral("Version %1, %2 bytes, CRC32 0x%3")
                      .arg(formatVersion(version))
                      .arg(codeSize)
                      .arg(actualCrc, 8, 16, QLatin1Char('0'));
    emit imageInfoChanged();
    appendLog(QStringLiteral("Loaded %1").arg(m_imageInfo));
    return true;
}

/* ------------------------------------------------------------------ protocol */

void OtaController::parseRx(const QByteArray &data)
{
    m_rxBuffer.append(data);

    for (;;) {
        qsizetype const start = m_rxBuffer.indexOf(kSof);
        if (start < 0) {
            m_rxBuffer.clear();
            return;
        }
        if (start > 0)
            m_rxBuffer.remove(0, start);

        if (m_rxBuffer.size() < kFrameOverhead)
            return;

        quint16 const length = quint16(quint8(m_rxBuffer[2]))
                               | (quint16(quint8(m_rxBuffer[3])) << 8);
        if (length > kMaxPayload) {
            m_rxBuffer.remove(0, 1);
            continue;
        }

        qsizetype const frameSize = kFrameOverhead + length;
        if (m_rxBuffer.size() < frameSize)
            return;

        quint16 const received = quint16(quint8(m_rxBuffer[4 + length]))
                                 | (quint16(quint8(m_rxBuffer[5 + length])) << 8);
        if (Astra::crc16(m_rxBuffer.mid(1, 3 + length)) != received) {
            appendLog(QStringLiteral("Dropping a corrupt frame."));
            m_rxBuffer.remove(0, 1);
            continue;
        }

        quint8 const type = quint8(m_rxBuffer[1]);
        QByteArray const payload = m_rxBuffer.mid(4, length);
        m_rxBuffer.remove(0, frameSize);

        m_frames.enqueue(qMakePair(type, payload));
        emit frameReceived();
    }
}

void OtaController::onCharacteristicChanged(const QLowEnergyCharacteristic &info,
                                            const QByteArray &value)
{
    if (info.uuid() != txUuid())
        return;

    parseRx(value);
}

bool OtaController::waitForFrame(int timeoutMs, quint8 &type, QByteArray &payload)
{
    QElapsedTimer timer;
    timer.start();

    while (m_frames.isEmpty()) {
        int const remaining = timeoutMs - int(timer.elapsed());
        if (remaining <= 0)
            return false;

        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(this, &OtaController::frameReceived, &loop,
                         &QEventLoop::quit);
        timeout.start(remaining);
        loop.exec();
    }

    auto const frame = m_frames.dequeue();
    type = frame.first;
    payload = frame.second;
    return true;
}

bool OtaController::waitForAck(quint32 expectedNext, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        quint8 type = 0;
        QByteArray payload;
        int const remaining = timeoutMs - int(timer.elapsed());
        if (remaining <= 0 || !waitForFrame(remaining, type, payload))
            break;

        if (type == kMsgAck && payload.size() >= 4) {
            quint32 const next = leU32(payload, 0);
            if (next == expectedNext)
                return true;

            appendLog(QStringLiteral("Unexpected ACK offset %1 (expected %2).")
                          .arg(next)
                          .arg(expectedNext));
            return false;
        }

        if (type == kMsgStatus && payload.size() >= 1) {
            appendLog(QStringLiteral("Bootloader rejected the frame: %1.")
                          .arg(statusToString(quint8(payload[0]))));
            return false;
        }

        // INFO or anything else that was still in flight: ignore it.
    }

    return false;
}

bool OtaController::writeValue(
    const QLowEnergyCharacteristic &characteristic, const QByteArray &value)
{
    if (!m_service || !characteristic.isValid())
        return false;

    bool ok = false;
    QEventLoop loop;

    auto const written = QObject::connect(
        m_service, &QLowEnergyService::characteristicWritten, &loop,
        [&](const QLowEnergyCharacteristic &info, const QByteArray &) {
            if (info.uuid() == characteristic.uuid()) {
                ok = true;
                loop.quit();
            }
        });
    auto const failed = QObject::connect(
        m_service, &QLowEnergyService::errorOccurred, &loop,
        [&](QLowEnergyService::ServiceError) { loop.quit(); });

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(5000);

    m_service->writeCharacteristic(characteristic, value,
                                   QLowEnergyService::WriteWithResponse);
    loop.exec();

    QObject::disconnect(written);
    QObject::disconnect(failed);

    return ok;
}

bool OtaController::sendFrame(quint8 type, const QByteArray &payload)
{
    if (payload.size() > kMaxPayload) {
        appendLog(QStringLiteral("Frame payload is too large (%1 bytes).")
                      .arg(payload.size()));
        return false;
    }

    QByteArray frame;
    frame.reserve(payload.size() + kFrameOverhead);
    frame.append(kSof);
    frame.append(char(type));
    frame.append(char(quint8(payload.size() & 0xFF)));
    frame.append(char(quint8((payload.size() >> 8) & 0xFF)));
    frame.append(payload);

    quint16 const crc = Astra::crc16(frame.mid(1));
    frame.append(char(quint8(crc & 0xFF)));
    frame.append(char(quint8(crc >> 8)));

    int chunkSize = 20;
    if (m_controller) {
        int const mtu = m_controller->mtu();
        if (mtu > 23)
            chunkSize = qMin(mtu - 3, 240);
    }

    for (qsizetype offset = 0; offset < frame.size(); offset += chunkSize) {
        QByteArray const chunk = frame.mid(offset, chunkSize);
        if (!writeValue(m_rx, chunk)) {
            appendLog(QStringLiteral("BLE write failed (device gone?)."));
            return false;
        }
    }

    return true;
}

bool OtaController::waitMs(int ms)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(ms);
    loop.exec();
    return true;
}

/* -------------------------------------------------------------------- update */

void OtaController::startUpdate()
{
    if (m_busy)
        return;

    if (!m_connected || !m_service) {
        appendLog(QStringLiteral("Connect to the device first."));
        return;
    }

    if (m_code.isEmpty()) {
        appendLog(QStringLiteral("Load an .ota file first."));
        return;
    }

    m_frames.clear();
    m_rxBuffer.clear();
    setProgress(0.0);
    setBusy(true);

    // Let the UI repaint the busy state before the blocking sequence starts.
    QTimer::singleShot(0, this, &OtaController::runUpdate);
}

void OtaController::runUpdate()
{
    auto abort = [this](const QString &reason) {
        appendLog(QStringLiteral("UPDATE FAILED: %1").arg(reason));
        sendFrame(kMsgAbort, QByteArray());
        setBusy(false);
    };

    appendLog(QStringLiteral("Pulsing the STM32 OTA request line..."));
    if (!writeValue(m_ctrl, QByteArray(1, char(0x01)))) {
        abort(QStringLiteral("could not write the control characteristic"));
        return;
    }

    // The application erases its header page and resets; the bootloader then
    // blinks its LED for four seconds before serving the UART.
    appendLog(QStringLiteral("Waiting for the bootloader..."));
    waitMs(4500);

    if (!waitForBootloader()) {
        abort(QStringLiteral("the bootloader did not answer (is the STM32 "
                             "connected?)"));
        return;
    }

    if (!downloadImage()) {
        abort(QStringLiteral("transfer aborted"));
        return;
    }

    setProgress(1.0);
    appendLog(QStringLiteral("Update complete. The STM32 is restarting."));
    setBusy(false);
}

bool OtaController::waitForBootloader()
{
    for (int attempt = 0; attempt < 25; ++attempt) {
        if (!sendFrame(kMsgGetInfo, QByteArray()))
            return false;

        quint8 type = 0;
        QByteArray payload;
        if (!waitForFrame(400, type, payload))
            continue;

        if (type == kMsgInfo && payload.size() >= 9) {
            quint32 const activeVersion = leU32(payload, 0);
            quint32 const maxCodeSize = leU32(payload, 4);
            quint8 const state = quint8(payload[8]);

            appendLog(QStringLiteral("Bootloader ready: installed version %1, "
                                     "slot %2 bytes%3.")
                          .arg(formatVersion(activeVersion))
                          .arg(maxCodeSize)
                          .arg(state ? QStringLiteral(", update in progress")
                                     : QString()));
            return true;
        }

        if (type == kMsgStatus && payload.size() >= 1) {
            appendLog(QStringLiteral("Bootloader error: %1.")
                          .arg(statusToString(quint8(payload[0]))));
            return false;
        }
    }

    return false;
}

bool OtaController::downloadImage()
{
    quint32 const total = quint32(m_code.size());

    appendLog(QStringLiteral("Starting transfer: %1 bytes, CRC32 0x%2.")
                  .arg(total)
                  .arg(m_crc32, 8, 16, QLatin1Char('0')));

    QByteArray begin;
    appendLeU32(begin, total);
    appendLeU32(begin, m_crc32);
    appendLeU32(begin, m_version);

    if (!sendFrame(kMsgBegin, begin) || !waitForAck(0, 20000)) {
        appendLog(QStringLiteral("The bootloader did not accept the image."));
        return false;
    }

    setProgress(0.02);

    for (quint32 offset = 0; offset < total; offset += kBlockSize) {
        quint32 const length = qMin<quint32>(kBlockSize, total - offset);

        QByteArray payload;
        payload.reserve(length + 4);
        appendLeU32(payload, offset);
        payload.append(m_code.constData() + offset, length);

        if (!sendFrame(kMsgData, payload)) {
            appendLog(QStringLiteral("Transfer interrupted at %1 bytes.")
                          .arg(offset));
            return false;
        }

        if (!waitForAck(offset + length, 10000)) {
            appendLog(QStringLiteral("No ACK for offset %1.")
                          .arg(offset));
            return false;
        }

        quint32 const done = offset + length;
        setProgress(0.02 + 0.96 * (qreal(done) / qreal(total)));

        if ((offset / kBlockSize) % 8u == 0u) {
            appendLog(QStringLiteral("  %1 / %2 bytes").arg(done).arg(total));
        }
    }

    appendLog(QStringLiteral("Verifying CRC on the STM32..."));
    if (!sendFrame(kMsgEnd, QByteArray())) {
        appendLog(QStringLiteral("Could not send the final frame."));
        return false;
    }

    quint8 type = 0;
    QByteArray payload;
    if (!waitForFrame(30000, type, payload)) {
        appendLog(QStringLiteral("Timed out waiting for the final verdict."));
        return false;
    }

    if (type == kMsgDone) {
        setProgress(0.99);
        return true;
    }

    if (type == kMsgStatus && payload.size() >= 1) {
        appendLog(QStringLiteral("Bootloader rejected the image: %1.")
                      .arg(statusToString(quint8(payload[0]))));
        return false;
    }

    appendLog(QStringLiteral("Unexpected final frame 0x%1.")
                  .arg(type, 2, 16, QLatin1Char('0')));
    return false;
}
