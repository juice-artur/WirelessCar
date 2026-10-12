#pragma once

#include <QAbstractListModel>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QHash>
#include <QList>
#include <QString>

#include <qqmlintegration.h>

// List model of every BLE device seen while scanning. Devices are de-duplicated
// by address/UUID, the RSSI is refreshed on re-discovery and the result is
// sorted so that devices advertising the Nordic UART Service (and named
// devices) appear first.
class BleDeviceModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Access the device list via AppController.devices")

    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool nusOnly READ nusOnly WRITE setNusOnly NOTIFY nusOnlyChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        AddressRole,
        RssiRole,
        IsNusRole,
    };

    explicit BleDeviceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_visible.size(); }

    bool scanning() const { return m_scanning; }
    bool nusOnly() const { return m_nusOnly; }
    void setNusOnly(bool value);

    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    Q_INVOKABLE void clear();

    QBluetoothDeviceInfo deviceAt(int row) const;

signals:
    void scanningChanged();
    void nusOnlyChanged();
    void countChanged();
    void errorOccurred(const QString &message);

private slots:
    void onDeviceDiscovered(const QBluetoothDeviceInfo &info);
    void onDeviceUpdated(const QBluetoothDeviceInfo &info, QBluetoothDeviceInfo::Fields fields);
    void onScanFinished();
    void onScanError(QBluetoothDeviceDiscoveryAgent::Error error);
    void onCanceled();

private:
    void processDevice(const QBluetoothDeviceInfo &info);
    void rebuild();

    bool passesFilter(const QBluetoothDeviceInfo &info) const { return !m_nusOnly || isNus(info); }

    static QString keyFor(const QBluetoothDeviceInfo &info);
    static QString displayAddress(const QBluetoothDeviceInfo &info);
    static bool isNus(const QBluetoothDeviceInfo &info);
    static int rankOf(const QBluetoothDeviceInfo &info);
    static bool deviceLess(const QBluetoothDeviceInfo &a, const QBluetoothDeviceInfo &b);

    static int indexByKey(const QList<QBluetoothDeviceInfo> &list, const QString &key);
    int insertPositionFor(const QBluetoothDeviceInfo &info) const;

    QBluetoothDeviceDiscoveryAgent *m_agent = nullptr;
    QList<QBluetoothDeviceInfo> m_all;     // everything discovered
    QList<QBluetoothDeviceInfo> m_visible; // filtered + sorted view
    bool m_scanning = false;
    bool m_nusOnly = false;
};
