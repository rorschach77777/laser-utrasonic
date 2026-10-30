#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
#include <QTimer>
#include "communication/RadarHostClock.h"
#include "communication/RadarUdpSharedService.h"
#include "communication/PipePresenceStateController.h"

#include "communication/DeviceComm.h"
#include "communication/PollingService.h"
#include "controllers/ConnectionController.h"
#include "controllers/DashboardController.h"
#include "controllers/DfbController.h"
#include "controllers/GateController.h"
#include "controllers/PumpController.h"
#include "controllers/MultiModePumpController.h"
#include "controllers/WaterCoolerController.h"
#include "controllers/TimingController.h"
#include "controllers/SystemSettingsController.h"
#include "controllers/CommandController.h"
#include "models/RegisterDefs.h"
#include "data/DataController.h"
#include "data/SimpleGraphItem.h"
#include "data/AxialThicknessPlotItem.h"
#include "data/ThicknessHistoryItem.h"
#include "data/HistoryManager.h"
#include "data/HistoryGraphItem.h"
#include "data/ProcessAnalyzer.h"
#include "monitor/HealthMonitor.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Material");

    // Register custom QML types
    qmlRegisterType<SimpleGraphItem>("LST.Data", 1, 0, "SimpleGraphItem");
    qmlRegisterType<AxialThicknessPlotItem>("LST.Data", 1, 0, "AxialThicknessPlotItem");
    qmlRegisterType<ThicknessHistoryItem>("LST.Data", 1, 0, "ThicknessHistoryItem");
    qmlRegisterType<HistoryGraphItem>("LST.Data", 1, 0, "HistoryGraphItem");

    QQmlApplicationEngine engine;

    // Communication layer
    auto *comm = new DeviceComm(&app);
    auto *pollingService = new PollingService(comm, &app);

    // Controllers
    auto *connCtrl      = new ConnectionController(comm, &app);
    auto *dashCtrl      = new DashboardController(&app);
    auto *dfbCtrl       = new DfbController(comm, &app);
    auto *gateCtrl      = new GateController(comm,
                                             Reg::Input::GATE_STATUS_BASE,
                                             Reg::Hold::GATE_CTRL_BASE,
                                             Reg::Hold::GATE_ENABLE,
                                             Reg::Hold::GATE_RISE_DELAY_US,
                                             Reg::Hold::GATE_FALL_DELAY_US,
                                             Reg::Hold::GATE_POLARITY,
                                             &app);
    auto *pump1Ctrl     = new PumpController(comm, Reg::Hold::PUMP1_CTRL_BASE,
                                              Reg::Input::PUMP1_STATUS_BASE, &app);
    auto *pump2Ctrl     = new PumpController(comm, Reg::Hold::PUMP2_CTRL_BASE,
                                              Reg::Input::PUMP2_STATUS_BASE, &app);
    auto *slavePump1Ctrl = new PumpController(comm, Reg::Hold::SLAVE_PUMP1_CTRL_BASE,
                                               Reg::Input::SLAVE_PUMP1_STATUS_BASE, &app);
    auto *slavePump2Ctrl = new PumpController(comm, Reg::Hold::SLAVE_PUMP2_CTRL_BASE,
                                               Reg::Input::SLAVE_PUMP2_STATUS_BASE, &app);
    auto *slaveGateCtrl = new GateController(comm,
                                             Reg::Input::SLAVE_GATE_STATUS_BASE,
                                             Reg::Hold::SLAVE_GATE_CTRL_BASE,
                                             Reg::Hold::SLAVE_GATE_ENABLE,
                                             Reg::Hold::SLAVE_GATE_RISE_DELAY_US,
                                             Reg::Hold::SLAVE_GATE_FALL_DELAY_US,
                                             Reg::Hold::SLAVE_GATE_POLARITY,
                                             &app);
    auto *mpumpCtrl     = new MultiModePumpController(comm, &app);
    auto *waterCoolerCtrl = new WaterCoolerController(comm,
                                                      Reg::Input::WATER_COOLER_STATUS_BASE,
                                                      Reg::Hold::WATER_COOLER_TEMP_SET,
                                                      &app);
    auto *slaveWaterCoolerCtrl = new WaterCoolerController(comm,
                                                           Reg::Input::SLAVE_WATER_COOLER_STATUS_BASE,
                                                           Reg::Hold::SLAVE_WATER_COOLER_TEMP_SET,
                                                           &app);
    auto *mainTimingCtrl = new TimingController(comm, Reg::Hold::MAIN_TIMING_BASE, &app);
    auto *slaveTimingCtrl = new TimingController(comm, Reg::Hold::SLAVE_TIMING_BASE, &app);
    auto *sysSetCtrl    = new SystemSettingsController(comm, &app);
    auto *cmdCtrl       = new CommandController(comm, &app);
    auto *dataCtrl      = new DataController(&app);
    auto *radarUdpShared = new RadarUdpSharedService(&app);
    auto *pipePresenceState = new PipePresenceStateController(&app);
    // Single GUI-owned installation setting shared by the DAQ sidecar and
    // the real-time pipe-presence estimate. Apply changes between acquisitions.
    pipePresenceState->setRadarToLaserDistanceMm(dataCtrl->radarToLaserDistanceMm());
    QObject::connect(dataCtrl, &DataController::radarToLaserDistanceMmChanged,
                     pipePresenceState, [dataCtrl, pipePresenceState] {
        pipePresenceState->setRadarToLaserDistanceMm(dataCtrl->radarToLaserDistanceMm());
    });
    dataCtrl->setSharedRadarSource(radarUdpShared);
    // Direct callback only fills a mutex-protected 256-item ring. The existing
    // 100ms GUI timer drains it; no per-UDP queued Qt MetaCall can pile up.
    QObject::connect(radarUdpShared, &RadarUdpSharedService::sampleReceived,
                     pipePresenceState, &PipePresenceStateController::enqueueRadarSample,
                     Qt::DirectConnection);
    QObject::connect(radarUdpShared, &RadarUdpSharedService::statusMessage,
                     pipePresenceState, &PipePresenceStateController::reportRadarServiceStatus);
    QObject::connect(radarUdpShared, &RadarUdpSharedService::statusMessage,
                     &app, [](const QString &status) { qInfo().noquote() << "[RadarUDP]" << status; });
    // No physical LB1/LB2 digital inputs are available. PipePresenceStateController
    // infers both operating/not-operating solely from effective radar measurements;
    // these labels are operational proxies, never actual digital-input readings.
    auto *presenceClock = new QTimer(&app);
    presenceClock->setInterval(100);
    QObject::connect(presenceClock, &QTimer::timeout, pipePresenceState,
                     [pipePresenceState] { pipePresenceState->tick(RadarHostClock::nowUs()); });
    presenceClock->start();
    radarUdpShared->start();

    auto *historyMgr    = new HistoryManager(&app);
    auto *processAnalyzer = new ProcessAnalyzer(&app);
    auto *healthMonitor = new HealthMonitor(&app);

    // Wire history / process analysis
    dataCtrl->setHistoryManager(historyMgr);
    processAnalyzer->setHistoryManager(historyMgr);
    healthMonitor->setSources(comm, pollingService);

    // Load existing history from disk
    historyMgr->loadFromDisk(QDir::currentPath() + "/HistoryData");

    dashCtrl->setSystemStatusModel(pollingService->systemStatusModel());
    dashCtrl->setSlaveStatusModel(pollingService->slaveStatusModel());
    dashCtrl->setDfbStatusModel(pollingService->dfbModel());
    dashCtrl->setMpumpStatusModel(pollingService->mpumpModel());
    dashCtrl->setPump1StatusModel(pollingService->pump1Model());
    dashCtrl->setPump2StatusModel(pollingService->pump2Model());
    dashCtrl->setTemperatureModel(pollingService->temperatureModel());
    dashCtrl->setSlavePump1StatusModel(pollingService->slavePump1Model());
    dashCtrl->setSlavePump2StatusModel(pollingService->slavePump2Model());

    dfbCtrl->setModel(pollingService->dfbModel());
    gateCtrl->setModel(pollingService->gateModel());
    pump1Ctrl->setModel(pollingService->pump1Model());
    pump2Ctrl->setModel(pollingService->pump2Model());
    slavePump1Ctrl->setModel(pollingService->slavePump1Model());
    slavePump2Ctrl->setModel(pollingService->slavePump2Model());
    slaveGateCtrl->setModel(pollingService->slaveGateModel());
    mpumpCtrl->setModel(pollingService->mpumpModel());
    waterCoolerCtrl->setModel(pollingService->waterCoolerModel());
    slaveWaterCoolerCtrl->setModel(pollingService->slaveWaterCoolerModel());
    mainTimingCtrl->setModel(pollingService->mainTimingModel());
    slaveTimingCtrl->setModel(pollingService->slaveTimingModel());
    // Expose to QML
    auto *ctx = engine.rootContext();
    ctx->setContextProperty("deviceComm", comm);
    ctx->setContextProperty("pollingService", pollingService);
    ctx->setContextProperty("connectionController", connCtrl);
    ctx->setContextProperty("dashboardController", dashCtrl);
    ctx->setContextProperty("dfbController", dfbCtrl);
    ctx->setContextProperty("gateController", gateCtrl);
    ctx->setContextProperty("pump1Controller", pump1Ctrl);
    ctx->setContextProperty("pump2Controller", pump2Ctrl);
    ctx->setContextProperty("slavePump1Controller", slavePump1Ctrl);
    ctx->setContextProperty("slavePump2Controller", slavePump2Ctrl);
    ctx->setContextProperty("slaveGateController", slaveGateCtrl);
    ctx->setContextProperty("multiModePumpController", mpumpCtrl);
    ctx->setContextProperty("waterCoolerController", waterCoolerCtrl);
    ctx->setContextProperty("slaveWaterCoolerController", slaveWaterCoolerCtrl);
    ctx->setContextProperty("mainTimingController", mainTimingCtrl);
    ctx->setContextProperty("slaveTimingController", slaveTimingCtrl);
    ctx->setContextProperty("systemSettingsController", sysSetCtrl);
    ctx->setContextProperty("commandController", cmdCtrl);
    ctx->setContextProperty("dataController", dataCtrl);
    ctx->setContextProperty("pipePresenceState", pipePresenceState);
    ctx->setContextProperty("historyManager", historyMgr);
    ctx->setContextProperty("processAnalyzer", processAnalyzer);
    ctx->setContextProperty("healthMonitor", healthMonitor);

    // Initialize data controller
    dataCtrl->initialize();

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("LST", "Main");

    return app.exec();
}
