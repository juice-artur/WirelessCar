#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

#include <qqmlintegration.h>

// Byte-traffic log shown in the right pane. Entries are either raw data
// (received / sent) or informational messages.
class LogModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Access the log via AppController.log")

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Direction {
        Rx = 0,   // device -> host
        Tx = 1,   // host -> device
        Info = 2, // informational
    };
    Q_ENUM(Direction)

    enum Level {
        Normal = 0,
        Warning = 1,
        Error = 2,
    };
    Q_ENUM(Level)

    enum Roles {
        TimeRole = Qt::UserRole + 1,
        DirectionRole,
        HexRole,
        AsciiRole,
        MessageRole,
        LevelRole,
    };

    explicit LogModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_entries.size(); }

    Q_INVOKABLE void clear();

    void appendData(Direction direction, const QByteArray &data);
    void appendInfo(const QString &message, Level level = Normal);

signals:
    void countChanged();

private:
    struct Entry {
        QDateTime time;
        Direction direction = Info;
        QString hex;
        QString ascii;
        QString message;
        Level level = Normal;
    };

    void addEntry(Entry entry);
    void trim();

    static constexpr int kMaxEntries = 5000;

    QVector<Entry> m_entries;
};
