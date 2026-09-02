#include "HealthMonitor.h"

#include "../communication/DeviceComm.h"
#include "../communication/PollingService.h"
#include "../models/DfbModel.h"
#include "../models/MultiModePumpModel.h"
#include "../models/PumpModel.h"
#include "../models/SlaveStatusModel.h"
#include "../models/SystemStatusModel.h"
#include "../models/TemperatureModel.h"
#include "../models/WaterCoolerModel.h"

#include <QtMath>

HealthMonitor::HealthMonitor(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &HealthMonitor::evaluateNow);
}

void HealthMonitor::setSources(DeviceComm *comm, PollingService *pollingService)
{
    m_comm = comm;
    m_pollingService = pollingService;
    if (!m_timer.isActive())
        m_timer.start();
    evaluateNow();
}

QString HealthMonitor::thresholdNote() const
{
    return tr("当前阈值为软件初始工程阈值：优先依据设备在线状态、故障码、控制界面允许范围，以及实时值相对设定值/限值的偏差判断。正式现场使用前应按设备说明书和工艺要求统一标定。");
}

void HealthMonitor::appendCondition(QVariantList &target,
                                    const QString &code,
                                    bool condition,
                                    Severity severity,
                                    const QString &device,
                                    const QString &message,
                                    const QString &valueText,
                                    const QString &normalText,
                                    const QString &suggestion,
                                    int requiredChecks)
{
    if (!condition) {
        m_violationCounts.remove(code);
        return;
    }

    int count = m_violationCounts.value(code, 0) + 1;
    m_violationCounts[code] = count;
    if (count < qMax(1, requiredChecks))
        return;

    QVariantMap alarm;
    alarm["code"] = code;
    alarm["severity"] = static_cast<int>(severity);
    alarm["severityText"] = severity == Alarm ? tr("报警") : tr("警告");
    alarm["device"] = device;
    alarm["message"] = message;
    alarm["value"] = valueText;
    alarm["normal"] = normalText;
    alarm["suggestion"] = suggestion;
    target.append(alarm);
}

void HealthMonitor::evaluatePump(QVariantList &target,
                                 const QString &keyPrefix,
                                 const QString &displayName,
                                 PumpModel *pump)
{
    if (!pump) return;

    appendCondition(target,
                    keyPrefix + ".offline",
                    !pump->online(),
                    Alarm,
                    displayName,
                    tr("设备离线"),
                    tr("离线"),
                    tr("应保持在线"),
                    tr("检查泵浦供电、通信地址、RS485连接及主从板通信状态。"));

    if (!pump->online())
        return;

    appendCondition(target,
                    keyPrefix + ".status",
                    pump->statusCode() != 0,
                    Alarm,
                    displayName,
                    tr("状态码异常"),
                    QString("0x%1").arg(pump->statusCode(), 0, 16).toUpper(),
                    QStringLiteral("0x0"),
                    tr("根据泵浦驱动器状态码检查故障来源，并核对使能及触发条件。"),
                    1);

    // The UI currently allows 0-130 A, 15-200 V and 1-500 Hz.
    appendCondition(target,
                    keyPrefix + ".currentRange",
                    pump->currentActual() > 130,
                    Alarm,
                    displayName,
                    tr("实际电流超出当前软件允许范围"),
                    QString::number(pump->currentActual()) + " A",
                    tr("0 ~ 130 A"),
                    tr("检查电流设定、驱动器输出及负载状态。"));

    appendCondition(target,
                    keyPrefix + ".voltageRange",
                    pump->voltageActual() > 200.0,
                    Alarm,
                    displayName,
                    tr("实际电压超出当前软件允许范围"),
                    QString::number(pump->voltageActual(), 'f', 1) + " V",
                    tr("0 ~ 200 V"),
                    tr("检查泵浦驱动限压设置、电源和负载连接。"));

    appendCondition(target,
                    keyPrefix + ".frequencyRange",
                    pump->freqActual() > 500,
                    Alarm,
                    displayName,
                    tr("实际频率超出当前软件允许范围"),
                    QString::number(pump->freqActual()) + " Hz",
                    tr("0 ~ 500 Hz"),
                    tr("检查触发源、内部频率设置和时序配置。"));

    if (pump->enableRb()) {
        const double voltageLimit = pump->voltageLimitRb() > 0
            ? static_cast<double>(pump->voltageLimitRb())
            : static_cast<double>(pump->voltageLimit());

        appendCondition(target,
                        keyPrefix + ".voltageLimit",
                        voltageLimit > 0.0 && pump->voltageActual() > voltageLimit * 1.02,
                        Alarm,
                        displayName,
                        tr("实际电压超过限压值"),
                        QString("%1 V").arg(pump->voltageActual(), 0, 'f', 1),
                        QString("≤ %1 V").arg(voltageLimit, 0, 'f', 1),
                        tr("检查限压设定、驱动输出及泵浦负载。"));

        if (pump->currentRb() > 0) {
            const double currentError = qAbs(static_cast<double>(pump->currentActual()) - pump->currentRb());
            const double currentTol = qMax(3.0, static_cast<double>(pump->currentRb()) * 0.10);
            appendCondition(target,
                            keyPrefix + ".currentDeviation",
                            currentError > currentTol,
                            Warning,
                            displayName,
                            tr("实际电流与回读设定偏差较大"),
                            QString("实际 %1 A / 回读 %2 A").arg(pump->currentActual()).arg(pump->currentRb()),
                            tr("偏差建议≤10%或3 A"),
                            tr("检查泵浦负载、驱动器输出能力和电流采样。"));
        }

        if (pump->mode() == 0 && pump->intFreqRb() > 0) {
            const double freqError = qAbs(static_cast<double>(pump->freqActual()) - pump->intFreqRb());
            const double freqTol = qMax(2.0, static_cast<double>(pump->intFreqRb()) * 0.10);
            appendCondition(target,
                            keyPrefix + ".frequencyDeviation",
                            freqError > freqTol,
                            Warning,
                            displayName,
                            tr("内部触发时实际频率与设定频率偏差较大"),
                            QString("实际 %1 Hz / 设定 %2 Hz").arg(pump->freqActual()).arg(pump->intFreqRb()),
                            tr("偏差建议≤10%或2 Hz"),
                            tr("检查内部触发配置、时序同步和驱动器工作状态。"));
        }
    }
}

void HealthMonitor::applyResult(bool monitoring,
                                const QVariantList &alarms,
                                const QString &inactiveStatus)
{
    int warningCount = 0;
    int alarmCount = 0;
    for (const QVariant &item : alarms) {
        const QVariantMap map = item.toMap();
        if (map.value("severity").toInt() >= Alarm)
            ++alarmCount;
        else
            ++warningCount;
    }

    int level = 0;
    QString status = inactiveStatus.isEmpty() ? tr("未监控") : inactiveStatus;
    if (monitoring) {
        if (alarmCount > 0) {
            level = 3;
            status = tr("报警");
        } else if (warningCount > 0) {
            level = 2;
            status = tr("警告");
        } else {
            level = 1;
            status = tr("正常");
        }
    }

    const bool changed = m_monitoring != monitoring
        || m_overallLevel != level
        || m_overallStatus != status
        || m_warningCount != warningCount
        || m_alarmCount != alarmCount
        || m_alarms != alarms;

    m_monitoring = monitoring;
    m_overallLevel = level;
    m_overallStatus = status;
    m_warningCount = warningCount;
    m_alarmCount = alarmCount;
    m_alarms = alarms;

    if (changed)
        emit healthChanged();
}

void HealthMonitor::evaluateNow()
{
    if (!m_comm || !m_pollingService) {
        m_violationCounts.clear();
        applyResult(false, {}, tr("未初始化"));
        return;
    }

    if (!m_comm->connected()) {
        m_violationCounts.clear();
        applyResult(false, {}, tr("未连接"));
        return;
    }

    if (!m_pollingService->active()) {
        m_violationCounts.clear();
        applyResult(false, {}, tr("轮询停止"));
        return;
    }

    QVariantList alarms;

    auto *system = m_pollingService->systemStatusModel();
    auto *slave = m_pollingService->slaveStatusModel();
    auto *dfb = m_pollingService->dfbModel();
    auto *mpump = m_pollingService->mpumpModel();
    auto *mainCooler = m_pollingService->waterCoolerModel();
    auto *slaveCooler = m_pollingService->slaveWaterCoolerModel();

    appendCondition(alarms,
                    "system.fault",
                    system && system->hasFault(),
                    Alarm,
                    tr("主板系统"),
                    tr("主板存在系统故障"),
                    system ? QString("0x%1").arg(system->sysFault(), 0, 16).toUpper() : QString(),
                    QStringLiteral("0x0"),
                    tr("根据主板故障位检查DFB、泵浦、从板通信、同步和温度状态。"),
                    1);

    appendCondition(alarms,
                    "slave.offline",
                    slave && !slave->online(),
                    Alarm,
                    tr("从板"),
                    tr("从板离线"),
                    tr("离线"),
                    tr("应保持在线"),
                    tr("检查主从板通信、电源、地址和RS485链路。"));

    if (slave && slave->online()) {
        appendCondition(alarms,
                        "slave.fault",
                        slave->sysFault() != 0,
                        Alarm,
                        tr("从板"),
                        tr("从板系统故障"),
                        QString("0x%1").arg(slave->sysFault(), 0, 16).toUpper(),
                        QStringLiteral("0x0"),
                        tr("根据从板故障码检查泵浦、水冷、同步和通信状态。"),
                        1);
        appendCondition(alarms,
                        "slave.sync",
                        slave->syncStatusCode() != 0,
                        Warning,
                        tr("从板"),
                        tr("同步状态异常"),
                        QString::number(slave->syncStatusCode()),
                        QStringLiteral("0"),
                        tr("检查主从时序同步、触发链路和同步信号。"));
    }

    appendCondition(alarms,
                    "dfb.offline",
                    dfb && !dfb->online(),
                    Alarm,
                    tr("DFB激光器"),
                    tr("设备离线"),
                    tr("离线"),
                    tr("应保持在线"),
                    tr("检查DFB供电、通信和主板状态。"));

    if (dfb && dfb->online()) {
        appendCondition(alarms,
                        "dfb.fault",
                        dfb->fault() != 0,
                        Alarm,
                        tr("DFB激光器"),
                        tr("故障码非零"),
                        QString("0x%1").arg(dfb->fault(), 0, 16).toUpper(),
                        QStringLiteral("0x0"),
                        tr("根据DFB故障码检查TEC、LD驱动和供电。"),
                        1);
        appendCondition(alarms,
                        "dfb.tec",
                        dfb->tecStatus() == 0,
                        Warning,
                        tr("DFB激光器"),
                        tr("TEC状态异常"),
                        tr("异常"),
                        tr("TEC应稳定"),
                        tr("检查TEC控制、散热和目标温度设置。"));

        const double tempTarget = dfb->tempTarget();
        const double tempError = qAbs(dfb->temperature() - tempTarget);
        appendCondition(alarms,
                        "dfb.temperatureDeviation",
                        tempTarget > -50.0 && tempTarget < 100.0 && tempError > 3.0,
                        Warning,
                        tr("DFB激光器"),
                        tr("实时温度偏离目标温度"),
                        QString("实际 %1 °C / 目标 %2 °C").arg(dfb->temperature(), 0, 'f', 2).arg(tempTarget, 0, 'f', 2),
                        tr("建议偏差≤3 °C"),
                        tr("检查TEC稳定性、环境散热和温控设置。"));

        appendCondition(alarms,
                        "dfb.currentMax",
                        dfb->currentMax() > 0.0 && dfb->current() > dfb->currentMax() * 1.02,
                        Alarm,
                        tr("DFB激光器"),
                        tr("实际电流超过最大电流设置"),
                        QString("%1 mA").arg(dfb->current(), 0, 'f', 1),
                        QString("≤ %1 mA").arg(dfb->currentMax(), 0, 'f', 1),
                        tr("立即检查DFB驱动电流设置和驱动器输出。"));

        if (dfb->currentSet() > 0.0) {
            const double currentError = qAbs(dfb->current() - dfb->currentSet());
            const double currentTol = qMax(5.0, dfb->currentSet() * 0.10);
            appendCondition(alarms,
                            "dfb.currentDeviation",
                            currentError > currentTol,
                            Warning,
                            tr("DFB激光器"),
                            tr("实际电流与设定电流偏差较大"),
                            QString("实际 %1 mA / 设定 %2 mA").arg(dfb->current(), 0, 'f', 1).arg(dfb->currentSet(), 0, 'f', 1),
                            tr("偏差建议≤10%或5 mA"),
                            tr("检查DFB驱动模式、LD状态和电流闭环。"));
        }
    }

    appendCondition(alarms,
                    "mpump.offline",
                    mpump && !mpump->online(),
                    Alarm,
                    tr("多模泵"),
                    tr("设备离线"),
                    tr("离线"),
                    tr("应保持在线"),
                    tr("检查多模泵供电、通信和主板状态。"));

    if (mpump && mpump->online()) {
        appendCondition(alarms,
                        "mpump.fault",
                        mpump->fault() != 0,
                        Alarm,
                        tr("多模泵"),
                        tr("故障码非零"),
                        QString("0x%1").arg(mpump->fault(), 0, 16).toUpper(),
                        QStringLiteral("0x0"),
                        tr("根据多模泵故障码检查驱动器、供电和泵模块。"),
                        1);

        appendCondition(alarms,
                        "mpump.range",
                        mpump->currentActual() > 10000,
                        Alarm,
                        tr("多模泵"),
                        tr("实际电流超出当前软件允许范围"),
                        QString::number(mpump->currentActual()) + " mA",
                        tr("0 ~ 10000 mA"),
                        tr("检查多模泵电流设定和驱动输出。"));

        if (mpump->pumpSwitch() && mpump->currentSet() > 0) {
            const double error = qAbs(static_cast<double>(mpump->currentActual()) - mpump->currentSet());
            const double tolerance = qMax(100.0, static_cast<double>(mpump->currentSet()) * 0.10);
            appendCondition(alarms,
                            "mpump.currentDeviation",
                            error > tolerance,
                            Warning,
                            tr("多模泵"),
                            tr("实际电流与设定电流偏差较大"),
                            QString("实际 %1 mA / 设定 %2 mA").arg(mpump->currentActual()).arg(mpump->currentSet()),
                            tr("偏差建议≤10%或100 mA"),
                            tr("检查泵浦驱动状态、负载和电流闭环。"));
        }
    }

    auto evaluateCooler = [this, &alarms](const QString &key,
                                           const QString &name,
                                           WaterCoolerModel *cooler) {
        if (!cooler) return;
        appendCondition(alarms,
                        key + ".offline",
                        !cooler->online(),
                        Alarm,
                        name,
                        tr("水冷箱离线"),
                        tr("离线"),
                        tr("应保持在线"),
                        tr("检查水冷箱供电、通信、地址和主从板连接。"));
        if (!cooler->online()) return;

        const double target = cooler->tempSetC();
        const double current = cooler->temperatureC();
        appendCondition(alarms,
                        key + ".absoluteTemperature",
                        current < -20.0 || current > 60.0,
                        Alarm,
                        name,
                        tr("实时温度超出初始工程范围"),
                        QString("%1 °C").arg(current, 0, 'f', 1),
                        tr("-20 ~ 60 °C"),
                        tr("检查制冷、循环流量、温度传感器及设定值。"));

        if (target > -100.0 && target < 100.0 && qAbs(target) > 0.01) {
            const double error = qAbs(current - target);
            appendCondition(alarms,
                            key + ".temperatureDeviation",
                            error > 5.0,
                            Warning,
                            name,
                            tr("实时温度偏离设定温度"),
                            QString("实际 %1 °C / 设定 %2 °C").arg(current, 0, 'f', 1).arg(target, 0, 'f', 1),
                            tr("建议偏差≤5 °C"),
                            tr("检查水冷循环、制冷能力和设定温度。"));
        }
    };

    evaluateCooler("cooler.main", tr("检测激光水冷箱"), mainCooler);
    evaluateCooler("cooler.slave", tr("激发激光水冷箱"), slaveCooler);

    evaluatePump(alarms, "pump.main1", tr("主板泵浦1"), m_pollingService->pump1Model());
    evaluatePump(alarms, "pump.main2", tr("主板泵浦2"), m_pollingService->pump2Model());
    evaluatePump(alarms, "pump.slave1", tr("从板泵浦1"), m_pollingService->slavePump1Model());
    evaluatePump(alarms, "pump.slave2", tr("从板泵浦2"), m_pollingService->slavePump2Model());

    if (auto *temperature = m_pollingService->temperatureModel()) {
        const QList<double> values = temperature->temperatures();
        for (int i = 0; i < values.size(); ++i) {
            const double value = values[i];
            appendCondition(alarms,
                            QString("temperature.ch%1.warning").arg(i),
                            value > 50.0 && value <= 80.0,
                            Warning,
                            tr("温度通道 CH%1").arg(i),
                            tr("温度偏高"),
                            QString("%1 °C").arg(value, 0, 'f', 2),
                            tr("≤ 50 °C 为当前正常区"),
                            tr("检查对应测点散热、环境温度和传感器安装。"));
            appendCondition(alarms,
                            QString("temperature.ch%1.alarm").arg(i),
                            value > 80.0,
                            Alarm,
                            tr("温度通道 CH%1").arg(i),
                            tr("温度过高"),
                            QString("%1 °C").arg(value, 0, 'f', 2),
                            tr("≤ 80 °C"),
                            tr("立即检查对应设备温升、散热和冷却状态。"));
        }
    }

    applyResult(true, alarms);
}
