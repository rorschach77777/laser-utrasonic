#include "algorithm/ThicknessProcessor.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QElapsedTimer>
#include <QHash>
#include <QMap>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

struct CsvData
{
    std::vector<double> raw;
    std::vector<double> filtered;
    double referenceThickness = std::numeric_limits<double>::quiet_NaN();
};

struct OutputRow
{
    QString fileName;
    ThicknessResult result;
    double referenceThickness = std::numeric_limits<double>::quiet_NaN();
    bool replacedByBackfill = false;
    bool shadowReplacedByBackfill = false;
    double processingMs = std::numeric_limits<double>::quiet_NaN();
};

struct ExpectedShadowRow
{
    double tof = std::numeric_limits<double>::quiet_NaN();
    double thicknessMm = std::numeric_limits<double>::quiet_NaN();
    QString confidenceClass;
    QString reason;
};

static QStringList splitCsvQuoted(const QString &line)
{
    QStringList fields;
    QString current;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line[i];
        if (ch == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                current += '"';
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (ch == ',' && !quoted) {
            fields.push_back(current);
            current.clear();
        } else {
            current += ch;
        }
    }
    fields.push_back(current);
    return fields;
}

static QHash<QString, ExpectedShadowRow> readExpectedShadow(const QString &path)
{
    QHash<QString, ExpectedShadowRow> rows;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return rows;
    QTextStream in(&file);
    if (in.atEnd())
        return rows;
    const QStringList header = splitCsvQuoted(in.readLine());
    const int fileCol = header.indexOf(QStringLiteral("File"));
    const int tofCol = header.indexOf(QStringLiteral("OutputTOF"));
    const int thicknessCol = header.indexOf(QStringLiteral("OutputThicknessMm"));
    const int confidenceCol = header.indexOf(QStringLiteral("ConfidenceClass"));
    const int reasonCol = header.indexOf(QStringLiteral("Reason"));
    if (fileCol < 0 || tofCol < 0 || thicknessCol < 0 || confidenceCol < 0 || reasonCol < 0)
        return rows;
    while (!in.atEnd()) {
        const QStringList fields = splitCsvQuoted(in.readLine());
        const int required = std::max({fileCol, tofCol, thicknessCol, confidenceCol, reasonCol});
        if (fields.size() <= required)
            continue;
        bool tofOk = false;
        bool thicknessOk = false;
        ExpectedShadowRow row;
        row.tof = fields[tofCol].toDouble(&tofOk);
        row.thicknessMm = fields[thicknessCol].toDouble(&thicknessOk);
        if (!tofOk || !thicknessOk)
            continue;
        row.confidenceClass = fields[confidenceCol];
        row.reason = fields[reasonCol];
        rows.insert(fields[fileCol], row);
    }
    return rows;
}

struct T2ExpectedRow
{
    bool available = false;
    double thicknessMm = std::numeric_limits<double>::quiet_NaN();
    double tof = std::numeric_limits<double>::quiet_NaN();
    QString reason;
    double formalT1 = std::numeric_limits<double>::quiet_NaN();
    double observedPeriod = std::numeric_limits<double>::quiet_NaN();
    bool pairSupport = false;
    bool t2T3Support = false;
    QString coreStatus, finalStatus, tracking, finalPrimaryReason;
    bool formalT1Available = false;
};

static QString readLogicalCsvRow(QTextStream &in)
{
    if (in.atEnd())
        return {};
    QString row = in.readLine();
    bool insideQuotes = false;
    auto consume = [&](const QString &part) {
        for (int i = 0; i < part.size(); ++i) {
            if (part[i] == '"') {
                if (insideQuotes && i + 1 < part.size() && part[i + 1] == '"')
                    ++i;
                else
                    insideQuotes = !insideQuotes;
            }
        }
    };
    consume(row);
    while (insideQuotes && !in.atEnd()) {
        const QString part = in.readLine();
        row += QLatin1Char('\n') + part;
        consume(part);
    }
    return row;
}

static QHash<QString, T2ExpectedRow> readExpectedT2(const QString &path)
{
    QHash<QString, T2ExpectedRow> out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    QTextStream in(&file);
    if (in.atEnd())
        return out;
    const QStringList head = splitCsvQuoted(readLogicalCsvRow(in));
    const int nameIx = head.indexOf(QStringLiteral("File"));
    const int validIx = head.indexOf(QStringLiteral("SecondEchoOnlyAvailable"));
    const int mmIx = head.indexOf(QStringLiteral("SecondEchoOnlyThickness"));
    const int tofIx = head.indexOf(QStringLiteral("SecondEchoOnlyTOF"));
    const int reasonIx = head.indexOf(QStringLiteral("SecondEchoOnlyReason"));
    const int t1Ix = head.indexOf(QStringLiteral("FirstEchoFormalTOF"));
    const int periodIx = head.indexOf(QStringLiteral("EchoSequencePeriodObserved"));
    const int supportIx = head.indexOf(QStringLiteral("SecondEchoOnlyDeltaTSupportAvailable"));
    const int t2t3Ix = head.indexOf(QStringLiteral("SecondEchoOnlyT2T3Support"));
    const int coreStatusIx = head.indexOf(QStringLiteral("MeasurementStatus"));
    const int finalStatusIx = head.indexOf(QStringLiteral("FinalMeasurementStatus"));
    const int trackingIx = head.indexOf(QStringLiteral("TrackingState"));
    const int fpReasonIx = head.indexOf(QStringLiteral("FinalPrimaryIdentityReason"));
    const int formalAvailableIx = head.indexOf(QStringLiteral("FirstEchoFormalAvailable"));
    if (std::min({nameIx, validIx, mmIx, tofIx, reasonIx}) < 0)
        return out;
    const int maxIx = std::max({nameIx, validIx, mmIx, tofIx, reasonIx});
    while (!in.atEnd()) {
        const auto cols = splitCsvQuoted(readLogicalCsvRow(in));
        if (cols.size() <= maxIx)
            continue;
        T2ExpectedRow frame;
        frame.available = cols[validIx].compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
        frame.thicknessMm = cols[mmIx].toDouble();
        frame.tof = cols[tofIx].toDouble();
        frame.reason = cols[reasonIx];
        if (t1Ix >= 0 && cols.size() > t1Ix)
            frame.formalT1 = cols[t1Ix].toDouble();
        if (periodIx >= 0 && cols.size() > periodIx)
            frame.observedPeriod = cols[periodIx].toDouble();
        if (supportIx >= 0 && cols.size() > supportIx)
            frame.pairSupport = cols[supportIx] == QStringLiteral("true");
        if (t2t3Ix >= 0 && cols.size() > t2t3Ix)
            frame.t2T3Support = cols[t2t3Ix] == QStringLiteral("true");
        if (coreStatusIx >= 0 && cols.size() > coreStatusIx)
            frame.coreStatus = cols[coreStatusIx];
        if (finalStatusIx >= 0 && cols.size() > finalStatusIx)
            frame.finalStatus = cols[finalStatusIx];
        if (trackingIx >= 0 && cols.size() > trackingIx)
            frame.tracking = cols[trackingIx];
        if (fpReasonIx >= 0 && cols.size() > fpReasonIx)
            frame.finalPrimaryReason = cols[fpReasonIx];
        if (formalAvailableIx >= 0 && cols.size() > formalAvailableIx)
            frame.formalT1Available = cols[formalAvailableIx] == QStringLiteral("true");
        out.insert(cols[nameIx], frame);
    }
    return out;
}

static CsvData readCsv(const QString& filePath)
{
    CsvData data;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return data;

    QTextStream in(&file);
    in.readLine(); // 跳过表头

    while (!in.atEnd())
    {
        const QStringList cols = in.readLine().split(',');
        if (cols.size() < 3)
            continue;

        bool rawOk = false;
        bool filteredOk = false;
        const double raw = cols[1].toDouble(&rawOk);
        const double filtered = cols[2].toDouble(&filteredOk);
        if (!rawOk || !filteredOk)
            continue;

        data.raw.push_back(raw);
        data.filtered.push_back(filtered);

        if (cols.size() >= 4)
        {
            bool referenceOk = false;
            const double reference = cols[3].toDouble(&referenceOk);
            if (referenceOk)
                data.referenceThickness = reference;
        }
    }
    return data;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() < 2)
    {
        std::cout << "Usage: IndustrialAlgorithmFileTest <csv-or-folder> [file-count] [expected-confidence-csv]\n";
        return 2;
    }

    const QFileInfo input(arguments[1]);
    const int requestedCount = arguments.size() >= 3
        ? std::max(1, arguments[2].toInt())
        : 10;
    const QString expectedConfidencePath = arguments.size() >= 4
        ? arguments[3] : QString();

    QStringList files;
    if (input.isFile())
    {
        files.push_back(input.absoluteFilePath());
    }
    else if (input.isDir())
    {
        QDir dir(input.absoluteFilePath());
        const QFileInfoList entries = dir.entryInfoList(
            QStringList() << "Frame_*.csv",
            QDir::Files | QDir::Readable,
            QDir::Name);
        for (const QFileInfo& entry : entries)
        {
            files.push_back(entry.absoluteFilePath());
            if (files.size() >= requestedCount)
                break;
        }
    }

    if (files.isEmpty())
    {
        std::cerr << "No CSV files found\n";
        return 2;
    }

    ThicknessProcessor processor;

    // 与当前LSTAlgorithmTest真实数据批处理参数一致，用于迁移后的逐帧一致性验证。
    constexpr double sampleRateHz = 160e6;
    constexpr int delayCount = 240;
    const double thicknessStdMm = [] {
        bool ok = false;
        const double value = qEnvironmentVariable("LST_MAIN_THICKNESS_STD_MM").toDouble(&ok);
        return ok && std::isfinite(value) && value > 0.0 ? value : 5.6;
    }();
    constexpr int filterOrder = 50;
    // Optional regression overrides: exercise on-site filter settings without
    // modifying the acquisition UI defaults or the production algorithm.
    const auto filterBound = [](const char *key, double fallback) {
        bool ok = false;
        const double v = qEnvironmentVariable(key).toDouble(&ok);
        return ok && std::isfinite(v) && v > 0.0 ? v : fallback;
    };
    const double filterLowMHz = filterBound("LST_MAIN_FILTER_LOW_MHZ", 3.0);
    const double filterHighMHz = filterBound("LST_MAIN_FILTER_HIGH_MHZ", 30.0);
    ThicknessAlgorithmParameters algorithmParameters;
    // Regression A/B only: the actual UI uses DataController's bool setting.
    if (qEnvironmentVariableIsSet("LST_MAIN_T2_HIGH_PRECISION"))
        algorithmParameters.t2HighPrecisionDetectionEnabled =
            qEnvironmentVariable("LST_MAIN_T2_HIGH_PRECISION").trimmed() != QStringLiteral("0");
    if (qEnvironmentVariableIsSet("LST_MAIN_T2_FULL_HILBERT"))
        algorithmParameters.t2RoiHilbertEnabled = false;
    bool roiMarginOk = false;
    const int roiMargin = qEnvironmentVariable("LST_MAIN_T2_ROI_MARGIN").toInt(&roiMarginOk);
    if (roiMarginOk)
        algorithmParameters.t2RoiMarginSamples = std::clamp(roiMargin, 0, 1600);
    const QString echoMode = qEnvironmentVariable("LST_MAIN_ECHO_MODE").trimmed().toLower();
    if (echoMode == QStringLiteral("t2") || echoMode == QStringLiteral("t2-only")
        || echoMode == QStringLiteral("second_echo")) {
        algorithmParameters.echoMode = ThicknessEchoMode::T2Only;
    }

    std::cout << std::setprecision(17);
    std::cout << "File,Valid,Thickness(mm),PeakIndex,PeakTime(us),PeakVoltage,SNR(dB),ReferenceThickness(mm),Backfilled,"
                 "ShadowValid,ShadowTOF,ShadowThickness(mm),ShadowConfidence,ShadowReason,ShadowBackfilled,"
                 "PhaseApplied,PhaseCorrectionSamples,PhaseReason,T2Stage,T2Reason,T2CoreStatus,T2CoreTracking,T2FormalT1,T2PeriodEvidence,T2Period,T2TOF,T2TemplateCorrelation,T2CandidateCount,T2CandidateSummary,T2PairStage,T2PairPredictedPeriod,T2PairHalfWidth,T2PairEnvelopeCorrelation,T2PairRawAbsCorrelation,T2PairResidual,T2PairPublished,Processing(ms)\n";

    int failures = 0;
    int shadowComparisonFailures = 0;
    std::vector<double> processingTimesMs;
    processingTimesMs.reserve(static_cast<size_t>(files.size()));
    std::vector<OutputRow> outputRows;
    outputRows.reserve(static_cast<size_t>(files.size()));
    for (const QString& filePath : files)
    {
        const CsvData csv = readCsv(filePath);
        const QFileInfo fileInfo(filePath);
        if (csv.raw.empty() || csv.filtered.size() != csv.raw.size())
        {
            std::cerr << "Failed to read waveform CSV: "
                      << fileInfo.fileName().toStdString() << "\n";
            ++failures;
            continue;
        }

        // 按文件顺序复用同一实例，覆盖跨帧预测、模板和噪声底状态。
        // 计时只包围主程序真实 ThicknessProcessor::process()，不包含CSV磁盘读取和打印，
        // 因而覆盖旧检测/LOCK/LOST + Hilbert/t50/LeadingEdge/Envelope NCC + 最终回退决策。
        QElapsedTimer processTimer;
        processTimer.start();
        ThicknessResult result = processor.process(
            csv.raw,
            nullptr,
            sampleRateHz,
            filterLowMHz,
            filterHighMHz,
            delayCount,
            thicknessStdMm,
            1.0,
            9,
            1,
            ThicknessDetectionMode::Auto,
            filterOrder,
            algorithmParameters);
        const double processingMs = processTimer.nsecsElapsed() / 1e6;
        processingTimesMs.push_back(processingMs);

        // 与测试程序批处理一致：当前帧确认重锁时，先修正此前的PENDING输出。
        if (result.hasBackfill && result.backfillFramesAgo > 0
            && result.backfillFramesAgo <= static_cast<int>(outputRows.size())) {
            ThicknessResult &previous =
                outputRows[outputRows.size() - result.backfillFramesAgo].result;
            previous.valid = true;
            previous.thicknessMm = result.backfillThicknessMm;
            previous.peakIndex = result.backfillPeakIndex;
            previous.peakTimeSec = result.backfillPeakTimeSec;
            previous.peakVoltage = result.backfillPeakVoltage;
            previous.snrDb = result.backfillSnrDb;
            outputRows[outputRows.size() - result.backfillFramesAgo].replacedByBackfill = true;
        }
        if (result.hasFinalPrimaryShadowBackfill
            && result.finalPrimaryShadowBackfillFramesAgo > 0
            && result.finalPrimaryShadowBackfillFramesAgo <= static_cast<int>(outputRows.size())) {
            ThicknessResult &previous = outputRows[
                outputRows.size() - result.finalPrimaryShadowBackfillFramesAgo].result;
            previous.finalPrimaryShadowValid = true;
            previous.finalPrimaryShadowTofIndex = result.finalPrimaryShadowBackfillTofIndex;
            previous.finalPrimaryShadowThicknessMm = result.finalPrimaryShadowBackfillThicknessMm;
            previous.finalPrimaryShadowConfidenceClass = result.finalPrimaryShadowBackfillConfidenceClass;
            previous.finalPrimaryShadowReason = result.finalPrimaryShadowBackfillReason;
            outputRows[outputRows.size() - result.finalPrimaryShadowBackfillFramesAgo]
                .shadowReplacedByBackfill = true;
        }

        outputRows.push_back({fileInfo.fileName(), result, csv.referenceThickness, false, false, processingMs});
    }

    if (!qEnvironmentVariableIsSet("LST_MAIN_T2_SUMMARY_ONLY")) for (const OutputRow &row : outputRows) {
        const ThicknessResult &result = row.result;
        std::cout << row.fileName.toStdString() << ','
                  << (result.valid ? "true" : "false") << ','
                  << result.thicknessMm << ','
                  << result.peakIndex << ','
                  << result.peakTimeSec * 1e6 << ','
                  << result.peakVoltage << ','
                  << result.snrDb << ','
                  << row.referenceThickness << ','
                  << (row.replacedByBackfill ? "true" : "false") << ','
                  << (result.finalPrimaryShadowValid ? "true" : "false") << ','
                  << result.finalPrimaryShadowTofIndex << ','
                  << result.finalPrimaryShadowThicknessMm << ','
                  << result.finalPrimaryShadowConfidenceClass.toStdString() << ','
                  << result.finalPrimaryShadowReason.toStdString() << ','
                  << (row.shadowReplacedByBackfill ? "true" : "false") << ','
                  << (result.firstEchoPhaseRefinementApplied ? "true" : "false") << ','
                  << result.firstEchoPhaseRefinementCorrectionSamples << ','
                  << result.firstEchoPhaseRefinementReason.toStdString() << ','
                  << result.t2OnlyStage.toStdString() << ','
                  << result.t2OnlyReason.toStdString() << ','
                  << result.t2OnlyFormalT1CoreStatus.toStdString() << ','
                  << result.t2OnlyFormalT1CoreTrackingState.toStdString() << ','
                  << result.t2OnlyFormalT1TofSamples << ','
                  << (result.t2OnlyPeriodEvidenceAvailable ? "true" : "false") << ','
                  << result.t2OnlyPeriodSamples << ','
                  << result.t2OnlyTofSamples << ','
                  << result.t2OnlyTemplateCorrelation << ','
                  << result.t2OnlyCandidateCount << ','
                  << result.t2OnlyCandidateSummary.toStdString() << ','
                  << result.t2OnlyPairStage.toStdString() << ','
                  << result.t2OnlyPairPredictedPeriod << ','
                  << result.t2OnlyPairSearchHalfWidth << ','
                  << result.t2OnlyPairEnvelopeCorrelation << ','
                  << result.t2OnlyPairRawAbsCorrelation << ','
                  << result.t2OnlyPairResidualSamples << ','
                  << (result.t2OnlyPairPublished ? "true" : "false") << ','
                  << row.processingMs << '\n';
    }

    const QString t2ExpectedPath = qEnvironmentVariable("LST_MAIN_T2_EXPECTED_CSV");
    if (!t2ExpectedPath.isEmpty()) {
        const auto expected = readExpectedT2(t2ExpectedPath);
        int missing = 0, validMismatches = 0, tofMismatches = 0, thicknessMismatches = 0;
        double maxTofDelta = 0.0, maxThicknessDelta = 0.0;
        QStringList mismatchExamples;
        for (const auto &row : outputRows) {
            auto it = expected.constFind(row.fileName);
            if (it == expected.cend()) {
                ++missing;
                continue;
            }
            if (row.result.valid != it->available) {
                ++validMismatches;
                if (mismatchExamples.size() < 12)
                    mismatchExamples << QStringLiteral("%1 VALID main=%2 expected=%3 reason=%4 expectedReason=%5")
                        .arg(row.fileName).arg(row.result.valid).arg(it->available)
                        .arg(row.result.t2OnlyReason, it->reason)
                        + QStringLiteral(" t1=%1/%2 period=%3/%4 periodValid=%5/%6")
                            .arg(row.result.t2OnlyFormalT1TofSamples,0,'f',3)
                            .arg(it->formalT1,0,'f',3)
                            .arg(row.result.t2OnlyPeriodSamples,0,'f',3)
                            .arg(it->observedPeriod,0,'f',3)
                            .arg(row.result.t2OnlyPeriodEvidenceAvailable)
                            .arg(it->pairSupport)
                        + QStringLiteral(" core=%1/%2 track=%3/%4 formal=%5 fp=%6")
                            .arg(row.result.t2OnlyFormalT1CoreStatus, it->coreStatus)
                            .arg(row.result.t2OnlyFormalT1CoreTrackingState, it->tracking)
                            .arg(it->formalT1Available)
                            .arg(it->finalPrimaryReason);
                continue;
            }
            if (!it->available)
                continue;
            const double deltaTof = std::abs(row.result.t2OnlyTofSamples - it->tof);
            const double deltaMm = std::abs(row.result.thicknessMm - it->thicknessMm);
            maxTofDelta = std::max(maxTofDelta, deltaTof);
            maxThicknessDelta = std::max(maxThicknessDelta, deltaMm);
            if (deltaTof > 1e-6 || !std::isfinite(deltaTof)) {
                ++tofMismatches;
                if (mismatchExamples.size() < 12)
                    mismatchExamples << QStringLiteral("%1 TOF main=%2 expected=%3 reason=%4/%5")
                        .arg(row.fileName).arg(row.result.t2OnlyTofSamples, 0, 'f', 7)
                        .arg(it->tof, 0, 'f', 7).arg(row.result.t2OnlyReason, it->reason);
            }
            if (deltaMm > 1e-6 || !std::isfinite(deltaMm))
                ++thicknessMismatches;
        }
        std::cerr << "T2_PARITY expected_rows=" << expected.size()
                  << " measured=" << outputRows.size()
                  << " missing=" << missing << " valid_mismatches=" << validMismatches
                  << " tof_mismatches=" << tofMismatches
                  << " thickness_mismatches=" << thicknessMismatches
                  << " max_tof_delta=" << maxTofDelta
                  << " max_thickness_delta_mm=" << maxThicknessDelta << '\n';
        for (const auto &example : mismatchExamples)
            std::cerr << "T2_PARITY_DETAIL " << example.toStdString() << '\n';
        if (expected.size() != outputRows.size() || missing || validMismatches ||
            tofMismatches || thicknessMismatches)
            failures++;
    }

    if (!expectedConfidencePath.isEmpty()) {
        const QHash<QString, ExpectedShadowRow> expected =
            readExpectedShadow(expectedConfidencePath);
        int expectedMatched = 0;
        int validMismatch = 0;
        int tofMismatch = 0;
        int thicknessMismatch = 0;
        int officialValidMismatch = 0;
        int officialTofMismatch = 0;
        int officialThicknessMismatch = 0;
        int confidenceMismatch = 0;
        int reasonMismatch = 0;
        int missingFile = 0;
        double maxTofDiff = 0.0;
        double maxThicknessDiff = 0.0;
        double maxOfficialTofDiff = 0.0;
        double maxOfficialThicknessDiff = 0.0;
        QStringList firstMismatches;
        QHash<QString, bool> seen;

        for (const OutputRow &row : outputRows) {
            const auto expectedIt = expected.constFind(row.fileName);
            const bool shouldBeValid = expectedIt != expected.constEnd();
            const bool shadowValid = row.result.finalPrimaryShadowValid;
            seen.insert(row.fileName, true);
            if (shadowValid != shouldBeValid) {
                ++validMismatch;
                if (firstMismatches.size() < 20) {
                    firstMismatches << QStringLiteral("%1 SHADOW_VALID main=%2 expected=%3")
                        .arg(row.fileName)
                        .arg(shadowValid ? 1 : 0)
                        .arg(shouldBeValid ? 1 : 0);
                }
            }
            if (row.result.valid != shouldBeValid) {
                ++officialValidMismatch;
                if (firstMismatches.size() < 20) {
                    firstMismatches << QStringLiteral("%1 OFFICIAL_VALID main=%2 expected=%3")
                        .arg(row.fileName)
                        .arg(row.result.valid ? 1 : 0)
                        .arg(shouldBeValid ? 1 : 0);
                }
            }
            if (!shouldBeValid || !shadowValid || !row.result.valid)
                continue;
            ++expectedMatched;
            const ExpectedShadowRow &exp = expectedIt.value();
            const double tofDiff = std::abs(row.result.finalPrimaryShadowTofIndex - exp.tof);
            const double thicknessDiff = std::abs(
                row.result.finalPrimaryShadowThicknessMm - exp.thicknessMm);
            maxTofDiff = std::max(maxTofDiff, tofDiff);
            maxThicknessDiff = std::max(maxThicknessDiff, thicknessDiff);
            if (!std::isfinite(tofDiff) || tofDiff > 1e-12) {
                ++tofMismatch;
                if (firstMismatches.size() < 20) {
                    firstMismatches << QStringLiteral("%1 TOF main=%2 expected=%3 diff=%4")
                        .arg(row.fileName)
                        .arg(row.result.finalPrimaryShadowTofIndex, 0, 'f', 9)
                        .arg(exp.tof, 0, 'f', 9)
                        .arg(tofDiff, 0, 'g', 12);
                }
            }
            if (!std::isfinite(thicknessDiff) || thicknessDiff > 1e-12) {
                ++thicknessMismatch;
                if (firstMismatches.size() < 20) {
                    firstMismatches << QStringLiteral("%1 THICK main=%2 expected=%3 diff=%4")
                        .arg(row.fileName)
                        .arg(row.result.finalPrimaryShadowThicknessMm, 0, 'f', 9)
                        .arg(exp.thicknessMm, 0, 'f', 9)
                        .arg(thicknessDiff, 0, 'g', 12);
                }
            }
            const double officialTof = row.result.peakTimeSec * sampleRateHz;
            const double officialTofDiff = std::abs(officialTof - exp.tof);
            const double officialThicknessDiff = std::abs(row.result.thicknessMm - exp.thicknessMm);
            maxOfficialTofDiff = std::max(maxOfficialTofDiff, officialTofDiff);
            maxOfficialThicknessDiff = std::max(maxOfficialThicknessDiff, officialThicknessDiff);
            if (!std::isfinite(officialTofDiff) || officialTofDiff > 1e-12) {
                ++officialTofMismatch;
                if (firstMismatches.size() < 20) {
                    firstMismatches << QStringLiteral("%1 OFFICIAL_TOF main=%2 expected=%3 diff=%4")
                        .arg(row.fileName)
                        .arg(officialTof, 0, 'f', 9)
                        .arg(exp.tof, 0, 'f', 9)
                        .arg(officialTofDiff, 0, 'g', 12);
                }
            }
            if (!std::isfinite(officialThicknessDiff) || officialThicknessDiff > 1e-12) {
                ++officialThicknessMismatch;
                if (firstMismatches.size() < 20) {
                    firstMismatches << QStringLiteral("%1 OFFICIAL_THICK main=%2 expected=%3 diff=%4")
                        .arg(row.fileName)
                        .arg(row.result.thicknessMm, 0, 'f', 9)
                        .arg(exp.thicknessMm, 0, 'f', 9)
                        .arg(officialThicknessDiff, 0, 'g', 12);
                }
            }
            if (row.result.finalPrimaryShadowConfidenceClass != exp.confidenceClass) {
                ++confidenceMismatch;
                if (firstMismatches.size() < 20) {
                    firstMismatches << QStringLiteral("%1 CONF main=%2 expected=%3")
                        .arg(row.fileName, row.result.finalPrimaryShadowConfidenceClass,
                             exp.confidenceClass);
                }
            }
            if (row.result.finalPrimaryShadowReason != exp.reason) {
                ++reasonMismatch;
                if (firstMismatches.size() < 20) {
                    firstMismatches << QStringLiteral("%1 REASON main=%2 expected=%3")
                        .arg(row.fileName, row.result.finalPrimaryShadowReason, exp.reason);
                }
            }
        }
        for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) {
            if (!seen.contains(it.key())) {
                ++missingFile;
                if (firstMismatches.size() < 20)
                    firstMismatches << QStringLiteral("%1 MISSING_MAIN_FRAME").arg(it.key());
            }
        }

        shadowComparisonFailures = validMismatch + tofMismatch + thicknessMismatch
            + officialValidMismatch + officialTofMismatch + officialThicknessMismatch
            + confidenceMismatch + reasonMismatch + missingFile;
        std::cout << "#SHADOW_COMPARE,expected_rows=" << expected.size()
                  << ",matched_valid=" << expectedMatched
                  << ",valid_mismatch=" << validMismatch
                  << ",tof_mismatch=" << tofMismatch
                  << ",thickness_mismatch=" << thicknessMismatch
                  << ",official_valid_mismatch=" << officialValidMismatch
                  << ",official_tof_mismatch=" << officialTofMismatch
                  << ",official_thickness_mismatch=" << officialThicknessMismatch
                  << ",confidence_mismatch=" << confidenceMismatch
                  << ",reason_mismatch=" << reasonMismatch
                  << ",missing_file=" << missingFile
                  << ",max_tof_diff=" << maxTofDiff
                  << ",max_thickness_diff=" << maxThicknessDiff
                  << ",max_official_tof_diff=" << maxOfficialTofDiff
                  << ",max_official_thickness_diff=" << maxOfficialThicknessDiff << '\n';
        for (const QString &message : firstMismatches)
            std::cout << "#SHADOW_MISMATCH," << message.toStdString() << '\n';
    }

    if (!processingTimesMs.empty()) {
        std::sort(processingTimesMs.begin(), processingTimesMs.end());
        double sum = 0.0;
        int gt5 = 0;
        int gt8 = 0;
        int gt10 = 0;
        for (double value : processingTimesMs) {
            sum += value;
            if (value > 5.0) ++gt5;
            if (value > 8.0) ++gt8;
            if (value > 10.0) ++gt10;
        }
        const auto percentile = [&](double fraction) {
            const double position = std::clamp(fraction, 0.0, 1.0)
                * static_cast<double>(processingTimesMs.size() - 1);
            const size_t lower = static_cast<size_t>(std::floor(position));
            const size_t upper = static_cast<size_t>(std::ceil(position));
            if (lower == upper)
                return processingTimesMs[lower];
            const double weight = position - static_cast<double>(lower);
            return processingTimesMs[lower] * (1.0 - weight)
                + processingTimesMs[upper] * weight;
        };
        std::cout << "#PERF,count=" << processingTimesMs.size()
                  << ",mean_ms=" << sum / processingTimesMs.size()
                  << ",p50_ms=" << percentile(0.50)
                  << ",p95_ms=" << percentile(0.95)
                  << ",p99_ms=" << percentile(0.99)
                  << ",max_ms=" << processingTimesMs.back()
                  << ",gt5ms=" << gt5
                  << ",gt8ms=" << gt8
                  << ",gt10ms=" << gt10 << '\n';
    }

    return (failures == 0 && shadowComparisonFailures == 0) ? 0 : 1;
}
