#ifndef HISTORYMANAGER_H
#define HISTORYMANAGER_H

#include <QObject>
#include <QVector>
#include <QStringList>
#include <QMutex>
#include "HistoryRecord.h"

class HistoryManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int recordCount READ recordCount NOTIFY recordListChanged)
    Q_PROPERTY(QStringList recordTitles READ recordTitles NOTIFY recordListChanged)
    Q_PROPERTY(bool historyEnabled READ historyEnabled WRITE setHistoryEnabled NOTIFY historyEnabledChanged)

public:
    explicit HistoryManager(QObject *parent = nullptr);
    ~HistoryManager();

    // Properties
    int recordCount() const { return m_records.size(); }
    QStringList recordTitles() const;
    bool historyEnabled() const { return m_historyEnabled; }

    Q_INVOKABLE void setHistoryEnabled(bool enabled);

    // Session management
    Q_INVOKABLE void startSession();
    Q_INVOKABLE void appendThickness(double thickness);
    Q_INVOKABLE void finishSession();
    bool isSessionActive() const { return m_sessionActive; }
    int sessionDataCount() const { return m_sessionData.size(); }

    // Record management
    Q_INVOKABLE QVariantMap getRecord(int index) const;
    Q_INVOKABLE QVector<double> getRecordData(int index) const;
    Q_INVOKABLE void setRecordTitle(int index, const QString &title);
    Q_INVOKABLE void deleteRecord(int index);
    Q_INVOKABLE void deleteRecords(const QList<int> &indices);
    Q_INVOKABLE void clearAll();

    // Export / Import
    Q_INVOKABLE void exportToCsv(const QList<int> &indices, const QString &filePath, bool mergeToSingleFile);
    Q_INVOKABLE void importFromCsv(const QString &filePath);

    // Load data for a record (from CSV to memory)
    Q_INVOKABLE void loadRecordData(int index);

    // Linear interpolation for NaN values (returns interpolated data)
    Q_INVOKABLE QVector<double> interpolateData(const QVector<double> &rawData) const;

    // Persistence
    void loadFromDisk(const QString &dataDir);
    void saveMetadata();

signals:
    void recordListChanged();
    void historyEnabledChanged();
    void sessionFinished(const QString &recordId);
    void errorOccurred(const QString &message);
    void importProgress(int current, int total);
    void exportProgress(int current, int total);

private:
    void saveRecordToDisk(const HistoryRecord &record);
    void removeOldestIfNeeded();
    void ensureDataDir();
    QString generateId() const;

    static const int MAX_RECORDS = 100;
    QString m_dataDir;
    QVector<HistoryRecord> m_records;
    bool m_historyEnabled;
    bool m_sessionActive;
    QVector<double> m_sessionData;
    QString m_sessionId;
    mutable QMutex m_mutex;
};

#endif // HISTORYMANAGER_H