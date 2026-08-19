#ifndef HISTORYRECORD_H
#define HISTORYRECORD_H

#include <QString>
#include <QDateTime>
#include <QVector>

struct HistoryRecord {
    QString id;             // unique ID (UUID)
    QString title;          // user-customizable title, default = start time
    QDateTime startTime;    // acquisition start time
    QString filePath;       // path to local CSV file
    int totalPoints;        // total points (including NaN)
    QVector<double> thicknessData; // thickness values (loaded on demand)

    // Display-friendly title
    QString displayTitle() const {
        if (!title.isEmpty())
            return title;
        return startTime.toString("yyyy-MM-dd HH:mm:ss");
    }
};

#endif // HISTORYRECORD_H