#ifndef PROCESSANALYZER_H
#define PROCESSANALYZER_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class HistoryManager;

class ProcessAnalyzer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap result READ result NOTIFY analysisChanged)
    Q_PROPERTY(QVariantList spectrum READ spectrum NOTIFY analysisChanged)
    Q_PROPERTY(QVariantList thicknessProfile READ thicknessProfile NOTIFY analysisChanged)
    Q_PROPERTY(bool hasAnalysis READ hasAnalysis NOTIFY analysisChanged)
    Q_PROPERTY(QVariantMap batchResult READ batchResult NOTIFY batchAnalysisChanged)
    Q_PROPERTY(QVariantList batchTrend READ batchTrend NOTIFY batchAnalysisChanged)
    Q_PROPERTY(bool hasBatchAnalysis READ hasBatchAnalysis NOTIFY batchAnalysisChanged)

public:
    explicit ProcessAnalyzer(QObject *parent = nullptr);

    void setHistoryManager(HistoryManager *manager);

    QVariantMap result() const { return m_result; }
    QVariantList spectrum() const { return m_spectrum; }
    QVariantList thicknessProfile() const { return m_thicknessProfile; }
    bool hasAnalysis() const { return m_hasAnalysis; }
    QVariantMap batchResult() const { return m_batchResult; }
    QVariantList batchTrend() const { return m_batchTrend; }
    bool hasBatchAnalysis() const { return m_hasBatchAnalysis; }

    Q_INVOKABLE bool analyzeRecord(int index);
    Q_INVOKABLE bool analyzeRecord(int index,
                                   double cutoffWavelengthM,
                                   double minVariationMm,
                                   double dominanceThreshold);
    Q_INVOKABLE bool analyzeBatch(int maxRecords = 30,
                                  double rangeRateThresholdPercent = 5.0,
                                  double minValidRatePercent = 80.0,
                                  int plugCount = 0,
                                  int cycleTolerancePipes = 0);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void clearBatch();

signals:
    void analysisChanged();
    void batchAnalysisChanged();
    void errorOccurred(const QString &message);

private:
    HistoryManager *m_historyManager;
    QVariantMap m_result;
    QVariantList m_spectrum;
    QVariantList m_thicknessProfile;
    bool m_hasAnalysis;
    QVariantMap m_batchResult;
    QVariantList m_batchTrend;
    bool m_hasBatchAnalysis;
};

#endif // PROCESSANALYZER_H
