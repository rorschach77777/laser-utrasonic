#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "communication/DeviceComm.h"
#include "communication/PollingService.h"
#include "controllers/ConnectionController.h"
#include "controllers/DashboardController.h"
#include "controllers/DfbController.h"
#include "controllers/GateController.h"
#include "controllers/PumpController.h"
#include "controllers/MultiModePumpController.h"
#include "controllers/TimingController.h"
#include "controllers/SystemSettingsController.h"
#include "controllers/CommandController.h"
#include "models/RegisterDefs.h"
#include "data/DataController.h"
#include "data/SimpleGraphItem.h"
#include "data/ThicknessHistoryItem.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Material");

    // Register custom QML types
    qmlRegisterType<SimpleGraphItem>("LST.Data", 1, 0, "SimpleGraphItem");
    qmlRegisterType<ThicknessHistoryItem>("LST.Data", 1, 0, "ThicknessHistoryItem");

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
    auto *mainTimingCtrl = new TimingController(comm, Reg::Hold::MAIN_TIMING_BASE, &app);
    auto *slaveTimingCtrl = new TimingController(comm, Reg::Hold::SLAVE_TIMING_BASE, &app);
    auto *sysSetCtrl    = new SystemSettingsController(comm, &app);
    auto *cmdCtrl       = new CommandController(comm, &app);
    auto *dataCtrl      = new DataController(&app);

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
    ctx->setContextProperty("mainTimingController", mainTimingCtrl);
    ctx->setContextProperty("slaveTimingController", slaveTimingCtrl);
    ctx->setContextProperty("systemSettingsController", sysSetCtrl);
    ctx->setContextProperty("commandController", cmdCtrl);
    ctx->setContextProperty("dataController", dataCtrl);

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
