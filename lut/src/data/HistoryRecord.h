#ifndef HISTORYRECORD_H
#define HISTORYRECORD_H

#include <QString>
#include <QDateTime>
#include <QVector>
#include <QtGlobal>

struct HistoryRecord {
    static constexpr int CURRENT_SCHEMA_VERSION = 2;

    // Record identity / compatibility
    int schemaVersion = CURRENT_SCHEMA_VERSION;
    QString id;                 // unique ID (UUID)
    QString title;              // user-customizable title, default = start time

    // Pipe metadata
    QString pipeId;             // production / pipe identifier
    QString steelGrade;         // steel grade, e.g. P110
    double nominalThicknessMm = 0.0;
    double lineSpeedMps = 0.0;
    double sampleRateHz = 0.0;  // thickness sampling rate, not DAQ sample rate
    double pipeLengthM = 0.0;

    // Pipe-level measurement statistics
    double meanThicknessMm = 0.0;
    double maxThicknessMm = 0.0;
    double minThicknessMm = 0.0;
    double thicknessRangeMm = 0.0;
    double stdDevThicknessMm = 0.0;
    double validRatePercent = 0.0;

    // Measurement time / storage
    QDateTime startTime;
    QDateTime endTime;
    QString filePath;           // path to local measurement data file
    int totalPoints = 0;        // total points (including invalid / NaN points)

    // Point data. thicknessData keeps its existing name for compatibility with
    // current history graph/export code. Other vectors will be populated by the
    // pipe-session upgrade in the next step.
    QVector<double> positionDataM;
    QVector<double> thicknessData;
    QVector<double> snrDataDb;
    QVector<quint8> validData;  // 1 = valid thickness point, 0 = invalid

    // Display-friendly title
    QString displayTitle() const {
        if (!title.isEmpty())
            return title;
        return startTime.toString("yyyy-MM-dd HH:mm:ss");
    }
};

#endif // HISTORYRECORD_H