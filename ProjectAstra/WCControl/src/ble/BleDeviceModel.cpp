#include "BleDeviceModel.h"

#include <QBluetoothUuid>

#include <algorithm>

namespace {
const QBluetoothUuid kNusService(QStringLiteral("6e400001-b5a3-f393-e0a9-e50e24dcca9e"));
const QString kNusName = QStringLiteral("Nordic_UART_Service");
} // namespace

BleDeviceModel::BleDeviceModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int BleDeviceModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_visible.size();
}

QVariant BleDeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size())
        return {};

    const QBluetoothDeviceInfo &info = m_visible.at(index.row());
    switch (role) {
    case NameRole:
        return info.name();
    case AddressRole:
        return displayAddress(info);
    case RssiRole:
        return info.rssi();
    case IsNusRole:
        return isNus(info);
    default:
        return {};
    }
}

QHash<int, QByteArray> BleDeviceModel::roleNames() const
{
    return {
        {NameRole, QByteArrayLiteral("name")},
        {AddressRole, QByteArrayLiteral("address")},
        {RssiRole, QByteArrayLiteral("rssi")},
        {IsNusRole, QByteArrayLiteral("isNus")},
    };
}

void BleDeviceModel::setNusOnly(bool value)
{
    if (m_nusOnly == value)
        return;
    m_nusOnly = value;
    emit nusOnlyChanged();
    rebuild();
}

void BleDeviceModel::startScan()
{
    if (!m_agent) {
		m_agent = new QBluetoothDeviceDiscoveryAgent(this);
								m_agent->setLowEnergyDiscoveryTimeout(20000); // keep scanning until stop()
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
                this, &BleDeviceModel::onDeviceDiscovered);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceUpdated,
                this, &BleDeviceModel::onDeviceUpdated);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished,
                this, &BleDeviceModel::onScanFinished);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::canceled,
                this, &BleDeviceModel::onCanceled);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
                this, &BleDeviceModel::onScanError);
    }

    if (m_agent->isActive())
        return;

    m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);

    if (m_agent->error() != QBluetoothDeviceDiscoveryAgent::NoError) {
        onScanError(m_agent->error());
        return;
    }

    if (!m_scanning) {
        m_scanning = true;
        emit scanningChanged();
    }
}

void BleDeviceModel::stopScan()
{
    if (m_agent && m_agent->isActive())
        m_agent->stop();
    if (m_scanning) {
        m_scanning = false;
        emit scanningChanged();
    }
}

void BleDeviceModel::clear()
{
    if (m_all.isEmpty() && m_visible.isEmpty())
        return;
    beginResetModel();
    m_all.clear();
    m_visible.clear();
    endResetModel();
    emit countChanged();
}

QBluetoothDeviceInfo BleDeviceModel::deviceAt(int row) const
{
    if (row < 0 || row >= m_visible.size())
        return {};
    return m_visible.at(row);
}

void BleDeviceModel::onDeviceDiscovered(const QBluetoothDeviceInfo &info)
{
    processDevice(info);
}

void BleDeviceModel::onDeviceUpdated(const QBluetoothDeviceInfo &info, QBluetoothDeviceInfo::Fields)
{
    processDevice(info);
}

void BleDeviceModel::onScanFinished()
{
    if (m_scanning) {
        m_scanning = false;
        emit scanningChanged();
    }
}

void BleDeviceModel::onScanError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    Q_UNUSED(error);
    if (m_agent)
        emit errorOccurred(m_agent->errorString());
    if (m_scanning) {
        m_scanning = false;
        emit scanningChanged();
    }
}

void BleDeviceModel::onCanceled()
{
    if (m_scanning) {
        m_scanning = false;
        emit scanningChanged();
    }
}

void BleDeviceModel::processDevice(const QBluetoothDeviceInfo &info)
{
	qDebug() << "Знайдено в ефірі -> MAC:" << info.address().toString()
		<< "Ім'я:" << (info.name().isEmpty() ? "[ПУСТЕ]" : info.name())
		<< "Кількість UUIDs:" << info.serviceUuids().size();
    const QString key = keyFor(info);

    // Source of truth: everything we have ever seen.
    const int allIdx = indexByKey(m_all, key);
    if (allIdx >= 0)
        m_all[allIdx] = info;
    else
        m_all.append(info);

    const bool visible = passesFilter(info);
    const int row = visible ? indexByKey(m_visible, key) : -1;

    if (visible) {
        if (row >= 0) {
            m_visible[row] = info;
            emit dataChanged(index(row), index(row));
        } else {
            const int pos = insertPositionFor(info);
            beginInsertRows(QModelIndex(), pos, pos);
            m_visible.insert(pos, info);
            endInsertRows();
            emit countChanged();
        }
    } else if (row >= 0) {
        beginRemoveRows(QModelIndex(), row, row);
        m_visible.removeAt(row);
        endRemoveRows();
        emit countChanged();
    }
}

void BleDeviceModel::rebuild()
{
    beginResetModel();
    m_visible.clear();
    m_visible.reserve(m_all.size());
    for (const QBluetoothDeviceInfo &info : std::as_const(m_all)) {
        if (passesFilter(info))
            m_visible.append(info);
    }
    std::sort(m_visible.begin(), m_visible.end(), deviceLess);
    endResetModel();
    emit countChanged();
}

QString BleDeviceModel::keyFor(const QBluetoothDeviceInfo &info)
{
    if (!info.deviceUuid().isNull())
        return info.deviceUuid().toString();
    return info.address().toString();
}

QString BleDeviceModel::displayAddress(const QBluetoothDeviceInfo &info)
{
    const QString addr = info.address().toString();
    if (!addr.isEmpty() && addr != QLatin1String("00:00:00:00:00:00"))
        return addr;
    if (!info.deviceUuid().isNull())
        return info.deviceUuid().toString(QUuid::WithoutBraces);
    return QStringLiteral("(no address)");
}

bool BleDeviceModel::isNus(const QBluetoothDeviceInfo &info)
{
    const QList<QBluetoothUuid> uuids = info.serviceUuids();
    for (const QBluetoothUuid &uuid : uuids) {
        if (uuid == kNusService)
            return true;
    }
    return info.name().contains(kNusName, Qt::CaseInsensitive);
}

int BleDeviceModel::rankOf(const QBluetoothDeviceInfo &info)
{
    if (isNus(info))
        return 0;
    if (!info.name().isEmpty())
        return 1;
    return 2;
}

bool BleDeviceModel::deviceLess(const QBluetoothDeviceInfo &a, const QBluetoothDeviceInfo &b)
{
    const int ra = rankOf(a);
    const int rb = rankOf(b);
    if (ra != rb)
        return ra < rb;
    if (a.rssi() != b.rssi())
        return a.rssi() > b.rssi();
    return QString::compare(a.name(), b.name(), Qt::CaseInsensitive) < 0;
}

int BleDeviceModel::indexByKey(const QList<QBluetoothDeviceInfo> &list, const QString &key)
{
    for (int i = 0; i < list.size(); ++i) {
        if (keyFor(list.at(i)) == key)
            return i;
    }
    return -1;
}

int BleDeviceModel::insertPositionFor(const QBluetoothDeviceInfo &info) const
{
    for (int i = 0; i < m_visible.size(); ++i) {
        if (deviceLess(info, m_visible.at(i)))
            return i;
    }
    return m_visible.size();
}
