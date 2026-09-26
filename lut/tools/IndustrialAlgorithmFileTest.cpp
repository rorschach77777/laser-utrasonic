#include "algorithm/ThicknessProcessor.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QElapsedTimer>

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
    double processingMs = std::numeric_limits<double>::quiet_NaN();
};

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
        std::cout << "Usage: IndustrialAlgorithmFileTest <csv-or-folder> [file-count]\n";
        return 2;
    }

    const QFileInfo input(arguments[1]);
    const int requestedCount = arguments.size() >= 3
        ? std::max(1, arguments[2].toInt())
        : 10;

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
    constexpr double thicknessStdMm = 5.6;
    constexpr int filterOrder = 50;

    std::cout << std::setprecision(17);
    std::cout << "File,Valid,Thickness(mm),PeakIndex,PeakTime(us),PeakVoltage,SNR(dB),ReferenceThickness(mm),Backfilled,Processing(ms)\n";

    int failures = 0;
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
            3.0,
            30.0,
            delayCount,
            thicknessStdMm,
            1.0,
            9,
            1,
            ThicknessDetectionMode::Auto,
            filterOrder,
            ThicknessAlgorithmParameters{});
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

        outputRows.push_back({fileInfo.fileName(), result, csv.referenceThickness, false, processingMs});
    }

    for (const OutputRow &row : outputRows) {
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
                  << row.processingMs << '\n';
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

    return failures == 0 ? 0 : 1;
}
