#pragma once

#include <QObject>
#include <QHash>
#include <QTimer>
#include <QVariantList>

class DeviceComm;
class PollingService;
class PumpModel;

class HealthMonitor : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool monitoring READ monitoring NOTIFY healthChanged)
    Q_PROPERTY(int overallLevel READ overallLevel NOTIFY healthChanged)
    Q_PROPERTY(QString overallStatus READ overallStatus NOTIFY healthChanged)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY healthChanged)
    Q_PROPERTY(int alarmCount READ alarmCount NOTIFY healthChanged)
    Q_PROPERTY(QVariantList alarms READ alarms NOTIFY healthChanged)
    Q_PROPERTY(QString thresholdNote READ thresholdNote CONSTANT)

public:
    explicit HealthMonitor(QObject *parent = nullptr);

    void setSources(DeviceComm *comm, PollingService *pollingService);

    bool monitoring() const { return m_monitoring; }
    int overallLevel() const { return m_overallLevel; }
    QString overallStatus() const { return m_overallStatus; }
    int warningCount() const { return m_warningCount; }
    int alarmCount() const { return m_alarmCount; }
    QVariantList alarms() const { return m_alarms; }
    QString thresholdNote() const;

    Q_INVOKABLE void evaluateNow();

signals:
    void healthChanged();

private:
    enum Severity {
        Warning = 1,
        Alarm = 2
    };

    void appendCondition(QVariantList &target,
                         const QString &code,
                         bool condition,
                         Severity severity,
                         const QString &device,
                         const QString &message,
                         const QString &valueText,
                         const QString &normalText,
                         const QString &suggestion,
                         int requiredChecks = 3);

    void evaluatePump(QVariantList &target,
                      const QString &keyPrefix,
                      const QString &displayName,
                      PumpModel *pump);

    void applyResult(bool monitoring,
                     const QVariantList &alarms,
                     const QString &inactiveStatus = QString());

    DeviceComm *m_comm = nullptr;
    PollingService *m_pollingService = nullptr;
    QTimer m_timer;
    QHash<QString, int> m_violationCounts;

    bool m_monitoring = false;
    int m_overallLevel = 0; // 0=未监控, 1=正常, 2=警告, 3=报警
    QString m_overallStatus = QStringLiteral("未监控");
    int m_warningCount = 0;
    int m_alarmCount = 0;
    QVariantList m_alarms;
};
