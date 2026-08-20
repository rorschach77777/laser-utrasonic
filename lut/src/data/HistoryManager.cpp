#include "HistoryManager.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QUuid>
#include <QtConcurrent>
#include <QFileInfo>
#include <cmath>

HistoryManager::HistoryManager(QObject *parent)
    : QObject(parent)
    , m_historyEnabled(false)
    , m_sessionActive(false)
{
    m_dataDir = QDir::currentPath() + "/HistoryData";
    ensureDataDir();
}

HistoryManager::~HistoryManager()
{
    // If session is active, finish it
    if (m_sessionActive) {
        finishSession();
    }
    saveMetadata();
}

QStringList HistoryManager::recordTitles() const
{
    QMutexLocker locker(&m_mutex);
    QStringList titles;
    for (const auto &rec : m_records) {
        titles.append(rec.displayTitle());
    }
    return titles;
}

void HistoryManager::setHistoryEnabled(bool enabled)
{
    if (m_historyEnabled != enabled) {
        m_historyEnabled = enabled;
        emit historyEnabledChanged();
    }
}

void HistoryManager::startSession()
{
    qDebug() << "[HistoryManager] startSession() called, sessionActive:" << m_sessionActive;
    if (m_sessionActive) {
        qDebug() << "[HistoryManager] startSession: session already active, finishing first";
        finishSession();
    }
    m_sessionActive = true;
    m_sessionData.clear();
    m_sessionId = generateId();
    qDebug() << "[HistoryManager] startSession: new session started, id:" << m_sessionId;
}

void HistoryManager::appendThickness(double thickness)
{
    if (!m_sessionActive) return;
    m_sessionData.append(thickness);
    // Log every 100th point to avoid flooding
    if (m_sessionData.size() % 100 == 1) {
        qDebug() << "[HistoryManager] appendThickness: count=" << m_sessionData.size()
                 << "lastVal=" << thickness;
    }
}

void HistoryManager::finishSession()
{
    qDebug() << "[HistoryManager] finishSession() called, sessionActive:" << m_sessionActive
             << "dataCount:" << m_sessionData.size();

    if (!m_sessionActive) return;

    m_sessionActive = false;

    if (m_sessionData.isEmpty()) {
        qDebug() << "[HistoryManager] finishSession: no data, skipping record creation";
        m_sessionData.clear();
        return;
    }

    qDebug() << "[HistoryManager] finishSession: creating record with" << m_sessionData.size() << "points";

    HistoryRecord record;
    record.id = m_sessionId;
    record.startTime = QDateTime::currentDateTime();
    record.title = record.startTime.toString("yyyy-MM-dd HH:mm:ss");
    record.totalPoints = m_sessionData.size();
    record.thicknessData = m_sessionData;

    // Save to disk
    saveRecordToDisk(record);

    // Add to memory list (top)
    {
        QMutexLocker locker(&m_mutex);
        m_records.prepend(record);
    }

    removeOldestIfNeeded();
    saveMetadata();
    emit recordListChanged();
    emit sessionFinished(record.id);
    qDebug() << "[HistoryManager] finishSession: record saved, file:" << record.filePath;
}

QVariantMap HistoryManager::getRecord(int index) const
{
    QMutexLocker locker(&m_mutex);
    if (index < 0 || index >= m_records.size())
        return {};

    const auto &rec = m_records[index];
    QVariantMap map;
    map["id"] = rec.id;
    map["title"] = rec.displayTitle();
    map["startTime"] = rec.startTime.toString("yyyy-MM-dd HH:mm:ss");
    map["totalPoints"] = rec.totalPoints;
    map["filePath"] = rec.filePath;
    return map;
}

QVector<double> HistoryManager::getRecordData(int index) const
{
    QMutexLocker locker(&m_mutex);
    if (index < 0 || index >= m_records.size())
        return {};
    return m_records[index].thicknessData;
}

void HistoryManager::setRecordTitle(int index, const QString &title)
{
    {
        QMutexLocker locker(&m_mutex);
        if (index < 0 || index >= m_records.size()) return;
        m_records[index].title = title;
    }
    saveMetadata();
    emit recordListChanged();
}

void HistoryManager::deleteRecord(int index)
{
    {
        QMutexLocker locker(&m_mutex);
        if (index < 0 || index >= m_records.size()) return;

        // Delete file
        const auto &rec = m_records[index];
        if (!rec.filePath.isEmpty()) {
            QFile::remove(rec.filePath);
        }
        m_records.removeAt(index);
    }
    saveMetadata();
    emit recordListChanged();
}

void HistoryManager::deleteRecords(const QList<int> &indices)
{
    {
        QMutexLocker locker(&m_mutex);
        QList<int> sorted = indices;
        std::sort(sorted.begin(), sorted.end(), std::greater<int>());
        for (int idx : sorted) {
            if (idx < 0 || idx >= m_records.size()) continue;
            const auto &rec = m_records[idx];
            if (!rec.filePath.isEmpty()) {
                QFile::remove(rec.filePath);
            }
            m_records.removeAt(idx);
        }
    }
    saveMetadata();
    emit recordListChanged();
}

void HistoryManager::clearAll()
{
    {
        QMutexLocker locker(&m_mutex);
        for (const auto &rec : m_records) {
            if (!rec.filePath.isEmpty()) {
                QFile::remove(rec.filePath);
            }
        }
        m_records.clear();
    }
    saveMetadata();
    emit recordListChanged();
}

void HistoryManager::exportToCsv(const QList<int> &indices, const QString &filePath, bool mergeToSingleFile)
{
    // Run in background
    QtConcurrent::run([this, indices, filePath, mergeToSingleFile]() {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMetaObject::invokeMethod(this, [this]() {
                emit errorOccurred(tr("Cannot create export file."));
            }, Qt::QueuedConnection);
            return;
        }

        QTextStream out(&file);
        int total = indices.size();
        int current = 0;

        if (mergeToSingleFile) {
            struct ExportRecord {
                QString title;
                QString sourcePath;
                QVector<double> data;
            };

            QVector<ExportRecord> records;
            {
                QMutexLocker locker(&m_mutex);
                records.reserve(indices.size());
                for (int idx : indices) {
                    if (idx < 0 || idx >= m_records.size()) continue;
                    const auto &rec = m_records[idx];
                    records.append({rec.displayTitle(), rec.filePath, rec.thicknessData});
                }
            }

            // Records loaded from metadata after application startup keep thicknessData empty
            // until they are opened in the history view. Load missing data directly from
            // each record file so all selected records are included in a merged export.
            auto loadRecordFile = [](const QString &sourcePath) {
                QVector<double> data;
                QFile sourceFile(sourcePath);
                if (!sourceFile.open(QIODevice::ReadOnly | QIODevice::Text))
                    return data;

                QTextStream in(&sourceFile);
                bool firstLine = true;
                while (!in.atEnd()) {
                    QString line = in.readLine().trimmed();
                    if (line.isEmpty()) continue;

                    if (firstLine) {
                        firstLine = false;
                        if (line.startsWith("Index", Qt::CaseInsensitive))
                            continue;
                    }

                    QString valueText = line;
                    if (line.contains(',')) {
                        const QStringList parts = line.split(',');
                        if (parts.size() >= 2)
                            valueText = parts[1].trimmed();
                    }

                    if (valueText.compare("NaN", Qt::CaseInsensitive) == 0) {
                        data.append(std::numeric_limits<double>::quiet_NaN());
                    } else {
                        bool ok = false;
                        const double value = valueText.toDouble(&ok);
                        if (ok)
                            data.append(value);
                    }
                }
                return data;
            };

            qsizetype maxPointCount = 0;
            for (auto &rec : records) {
                if (rec.data.isEmpty() && !rec.sourcePath.isEmpty())
                    rec.data = loadRecordFile(rec.sourcePath);

                maxPointCount = qMax(maxPointCount, rec.data.size());
                ++current;
                QMetaObject::invokeMethod(this, [this, current, total]() {
                    emit exportProgress(current, total);
                }, Qt::QueuedConnection);
            }

            auto csvCell = [](QString text) {
                if (text.contains('"'))
                    text.replace("\"", "\"\"");
                if (text.contains(',') || text.contains('"') || text.contains('\n') || text.contains('\r'))
                    return QString("\"%1\"").arg(text);
                return text;
            };

            // Row 1: one title for each two-column record group.
            QStringList titleRow;
            for (const auto &rec : records) {
                titleRow << csvCell(rec.title) << QString();
            }
            out << titleRow.join(',') << "\n";

            // Row 2: column names for every selected record.
            QStringList headerRow;
            for (qsizetype i = 0; i < records.size(); ++i) {
                headerRow << QStringLiteral("数据点") << QStringLiteral("厚度点");
            }
            out << headerRow.join(',') << "\n";

            // Remaining rows: records are arranged horizontally, two columns per record.
            for (qsizetype point = 0; point < maxPointCount; ++point) {
                QStringList row;
                for (const auto &rec : records) {
                    if (point < rec.data.size()) {
                        row << QString::number(point + 1);
                        const double value = rec.data[point];
                        row << (std::isnan(value) ? QStringLiteral("NaN")
                                                  : QString::number(value, 'f', 6));
                    } else {
                        row << QString() << QString();
                    }
                }
                out << row.join(',') << "\n";
            }

            file.close();
            return;
        }

        // Keep the existing single-record export layout unchanged.
        for (int idx : indices) {
            QMutexLocker locker(&m_mutex);
            if (idx < 0 || idx >= m_records.size()) continue;

            const auto &rec = m_records[idx];
            out << "Index,Thickness(mm)\n";

            const auto &data = rec.thicknessData;
            for (int i = 0; i < data.size(); ++i) {
                double val = data[i];
                if (std::isnan(val)) {
                    out << (i + 1) << ",NaN\n";
                } else {
                    out << (i + 1) << "," << QString::number(val, 'f', 6) << "\n";
                }
            }

            locker.unlock();

            current++;
            QMetaObject::invokeMethod(this, [this, current, total]() {
                emit exportProgress(current, total);
            }, Qt::QueuedConnection);
        }

        file.close();
    });
}

void HistoryManager::importFromCsv(const QString &filePath)
{
    QtConcurrent::run([this, filePath]() {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMetaObject::invokeMethod(this, [this]() {
                emit errorOccurred(tr("Cannot open file for import."));
            }, Qt::QueuedConnection);
            return;
        }

        QTextStream in(&file);
        QVector<double> data;
        bool firstLine = true;

        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty()) continue;

            // Skip header line
            if (firstLine) {
                firstLine = false;
                if (line.startsWith("Index", Qt::CaseInsensitive) ||
                    line.startsWith("===")) {
                    continue;
                }
            }

            // Parse value: could be "Index,Value" or just "Value"
            double val = 0.0;
            bool isNan = false;
            if (line.contains(',')) {
                QStringList parts = line.split(',');
                if (parts.size() >= 2) {
                    QString valStr = parts[1].trimmed();
                    if (valStr.compare("NaN", Qt::CaseInsensitive) == 0) {
                        isNan = true;
                    } else {
                        val = valStr.toDouble();
                    }
                }
            } else {
                if (line.compare("NaN", Qt::CaseInsensitive) == 0) {
                    isNan = true;
                } else {
                    val = line.toDouble();
                }
            }

            data.append(isNan ? std::numeric_limits<double>::quiet_NaN() : val);
        }

        file.close();

        if (data.isEmpty()) {
            QMetaObject::invokeMethod(this, [this]() {
                emit errorOccurred(tr("No valid data found in file."));
            }, Qt::QueuedConnection);
            return;
        }

        // Create record
        HistoryRecord record;
        record.id = generateId();
        record.startTime = QDateTime::currentDateTime();
        record.title = QFileInfo(filePath).baseName();
        record.totalPoints = data.size();
        record.thicknessData = data;

        saveRecordToDisk(record);

        {
            QMutexLocker locker(&m_mutex);
            m_records.prepend(record);
        }

        removeOldestIfNeeded();
        saveMetadata();

        QMetaObject::invokeMethod(this, [this]() {
            emit recordListChanged();
        }, Qt::QueuedConnection);
    });
}

void HistoryManager::loadRecordData(int index)
{
    QMutexLocker locker(&m_mutex);
    if (index < 0 || index >= m_records.size()) return;

    auto &rec = m_records[index];
    if (!rec.thicknessData.isEmpty()) return; // Already loaded

    // Load from CSV
    QFile file(rec.filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    QVector<double> data;
    bool firstLine = true;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        if (firstLine) {
            firstLine = false;
            if (line.startsWith("Index", Qt::CaseInsensitive)) continue;
        }

        double val = 0.0;
        if (line.compare("NaN", Qt::CaseInsensitive) == 0) {
            val = std::numeric_limits<double>::quiet_NaN();
        } else if (line.contains(',')) {
            QStringList parts = line.split(',');
            QString valStr = parts.size() >= 2 ? parts[1].trimmed() : parts[0].trimmed();
            if (valStr.compare("NaN", Qt::CaseInsensitive) == 0) {
                val = std::numeric_limits<double>::quiet_NaN();
            } else {
                val = valStr.toDouble();
            }
        } else {
            val = line.toDouble();
        }
        data.append(val);
    }

    rec.thicknessData = data;
}

QVector<double> HistoryManager::interpolateData(const QVector<double> &rawData) const
{
    if (rawData.isEmpty()) return {};

    QVector<double> result = rawData;

    // Find NaN segments and interpolate linearly
    int n = result.size();
    int i = 0;
    while (i < n) {
        if (std::isnan(result[i])) {
            // Find start of NaN segment
            int nanStart = i;
            while (i < n && std::isnan(result[i])) ++i;
            int nanEnd = i - 1;

            // Find previous valid value
            double prevVal = 0.0;
            bool hasPrev = false;
            if (nanStart > 0) {
                prevVal = result[nanStart - 1];
                hasPrev = true;
            }

            // Find next valid value
            double nextVal = 0.0;
            bool hasNext = false;
            if (nanEnd < n - 1) {
                nextVal = result[nanEnd + 1];
                hasNext = true;
            }

            if (hasPrev && hasNext) {
                double startVal = prevVal;
                double endVal = nextVal;
                int segLen = nanEnd - nanStart + 1;
                for (int j = 0; j < segLen; ++j) {
                    double t = (j + 1.0) / (segLen + 1.0);
                    result[nanStart + j] = startVal + (endVal - startVal) * t;
                }
            } else if (hasPrev) {
                // Extend last value
                for (int j = nanStart; j <= nanEnd; ++j) {
                    result[j] = prevVal;
                }
            } else if (hasNext) {
                // Extend first value
                for (int j = nanStart; j <= nanEnd; ++j) {
                    result[j] = nextVal;
                }
            }
        } else {
            ++i;
        }
    }

    return result;
}

void HistoryManager::loadFromDisk(const QString &dataDir)
{
    m_dataDir = dataDir;
    ensureDataDir();

    QFile metaFile(m_dataDir + "/metadata.json");
    if (!metaFile.open(QIODevice::ReadOnly)) return;

    QByteArray data = metaFile.readAll();
    metaFile.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;

    QJsonObject root = doc.object();
    QJsonArray records = root["records"].toArray();

    QMutexLocker locker(&m_mutex);
    m_records.clear();

    for (const auto &recVal : records) {
        QJsonObject obj = recVal.toObject();
        HistoryRecord rec;
        rec.id = obj["id"].toString();
        rec.title = obj["title"].toString();
        rec.startTime = QDateTime::fromString(obj["startTime"].toString(), Qt::ISODate);
        rec.filePath = obj["filePath"].toString();
        rec.totalPoints = obj["totalPoints"].toInt();
        // thicknessData is loaded on demand via loadRecordData()
        m_records.append(rec);
    }

    locker.unlock();
    emit recordListChanged();
}

void HistoryManager::saveMetadata()
{
    QMutexLocker locker(&m_mutex);

    QJsonArray records;
    for (const auto &rec : m_records) {
        QJsonObject obj;
        obj["id"] = rec.id;
        obj["title"] = rec.title;
        obj["startTime"] = rec.startTime.toString(Qt::ISODate);
        obj["filePath"] = rec.filePath;
        obj["totalPoints"] = rec.totalPoints;
        records.append(obj);
    }

    QJsonObject root;
    root["records"] = records;

    QJsonDocument doc(root);
    QFile file(m_dataDir + "/metadata.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }
}

void HistoryManager::saveRecordToDisk(const HistoryRecord &record)
{
    QString fileName = record.id + ".csv";
    QString filePath = m_dataDir + "/" + fileName;

    qDebug() << "[HistoryManager] saveRecordToDisk: saving to" << filePath
             << "points:" << record.thicknessData.size();

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "[HistoryManager] saveRecordToDisk: ERROR cannot create file" << filePath;
        emit errorOccurred(tr("Cannot create data file."));
        return;
    }

    QTextStream out(&file);
    out << "Index,Thickness(mm)\n";

    const auto &data = record.thicknessData;
    for (int i = 0; i < data.size(); ++i) {
        double val = data[i];
        if (std::isnan(val)) {
            out << (i + 1) << ",NaN\n";
        } else {
            out << (i + 1) << "," << QString::number(val, 'f', 6) << "\n";
        }
    }

    file.close();

    // Update record file path
    auto &rec = const_cast<HistoryRecord&>(record);
    rec.filePath = filePath;
}

void HistoryManager::removeOldestIfNeeded()
{
    QMutexLocker locker(&m_mutex);
    while (m_records.size() > MAX_RECORDS) {
        const auto &oldest = m_records.last();
        if (!oldest.filePath.isEmpty()) {
            QFile::remove(oldest.filePath);
        }
        m_records.removeLast();
    }
}

void HistoryManager::ensureDataDir()
{
    QDir dir(m_dataDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
}

QString HistoryManager::generateId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}