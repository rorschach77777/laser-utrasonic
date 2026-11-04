cmake_minimum_required(VERSION 3.16)

project(LST VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick QuickControls2 SerialBus SerialPort Concurrent Network)

qt_standard_project_setup(REQUIRES 6.5)

set(PREBUILT_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/prebuilt)

set(MODEL_HEADERS
    src/models/RegisterDefs.h
    src/models/DeviceInfoModel.h
    src/models/SystemStatusModel.h
    src/models/SlaveStatusModel.h
    src/models/DfbModel.h
    src/models/MultiModePumpModel.h
    src/models/PumpModel.h
    src/models/TemperatureModel.h
    src/models/PulseChannelModel.h
    src/models/PendingWriteSupport.h
)

add_library(lst_models SHARED IMPORTED GLOBAL)

set_target_properties(lst_models PROPERTIES
    IMPORTED_LOCATION ${PREBUILT_LIB_DIR}/liblstcore.dll
    IMPORTED_IMPLIB ${PREBUILT_LIB_DIR}/liblstcore.a
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/src
    INTERFACE_LINK_LIBRARIES Qt6::Core
)

set(COMMUNICATION_SOURCES
    src/communication/DeviceComm.cpp
    src/communication/PollingService.cpp
)

set(COMMUNICATION_HEADERS
    src/modbus/ModbusResponse.h
    src/communication/DeviceComm.h
    src/communication/PollingService.h
)

qt_add_library(lst_communication SHARED
    ${COMMUNICATION_SOURCES}
    ${COMMUNICATION_HEADERS}
    src/models/GateModel.cpp
    src/controllers/GateController.cpp
    src/models/WaterCoolerModel.h
    src/communication/DeviceComm.cpp src/communication/DeviceComm.h   src/communication/PollingService.cpp src/communication/PollingService.h
    src/models/DeviceInfoModel.h src/models/DfbModel.h src/models/GateModel.cpp src/models/GateModel.h src/models/MultiModePumpModel.h src/models/PendingWriteSupport.h src/models/PulseChannelModel.h src/models/PumpModel.h src/models/RegisterDefs.h src/models/SlaveStatusModel.h src/models/SystemStatusModel.h src/models/TemperatureModel.h src/models/WaterCoolerModel.h
    src/controllers/CommandController.h src/controllers/ConnectionController.h src/controllers/DashboardController.h src/controllers/DfbController.h src/controllers/GateController.cpp src/controllers/GateController.h src/controllers/MultiModePumpController.h src/controllers/PumpController.h src/controllers/SystemSettingsController.h src/controllers/TimingController.h src/controllers/WaterCoolerController.h

)

set_target_properties(lst_communication PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_include_directories(lst_communication
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(lst_communication
    PUBLIC
        lst_models
        Qt6::Core
        Qt6::SerialBus
        Qt6::SerialPort
        Qt6::Quick
        Qt6::Concurrent
)

set(CONTROLLER_HEADERS
    src/controllers/ConnectionController.h
    src/controllers/DashboardController.h
    src/controllers/DfbController.h
    src/controllers/PumpController.h
    src/controllers/MultiModePumpController.h
    src/controllers/TimingController.h
    src/controllers/SystemSettingsController.h
    src/controllers/CommandController.h
)

# Data module sources
set(DATA_SOURCES
    src/data/DataController.cpp
    src/communication/RadarUdpReceiver.cpp
    src/communication/RadarPositionSidecar.cpp
    src/communication/RadarUdpSharedService.cpp
    src/communication/PipePresenceStateController.cpp
    src/algorithm/ThicknessProcessor.cpp
    src/data/SimpleGraphItem.cpp
    src/data/AxialThicknessPlotItem.cpp
    src/data/ThicknessHistoryItem.cpp
    src/data/HistoryManager.cpp
    src/data/HistoryGraphItem.cpp
    src/data/ProcessAnalyzer.cpp
)

set(DATA_HEADERS
    src/data/DataController.h
    src/data/DynamicDetectionRateCounter.h
    src/communication/RadarHostClock.h
    src/communication/BdaqHostTimeAligner.h
    src/communication/PipeHeadAxialTracker.h
    src/communication/RadarLaserGeometry.h
    src/communication/LstDataExport.h
    src/communication/RadarUdpReceiver.h
    src/communication/RadarUdpProtocol.h
    src/communication/RadarFrameMatcher.h
    src/communication/RadarPositionSidecar.h
    src/communication/RadarUdpSharedService.h
    src/communication/PipePresenceStateController.h
    src/algorithm/ThicknessProcessor.h
    src/algorithm/EchoMeasurementModeRouter.h
    src/algorithm/SecondEchoSearchWindow.h
    src/algorithm/SecondEchoOnlyTracker.h
    src/algorithm/EchoPairTracker.h
    src/algorithm/RawPhaseBranchTracker.h
    src/algorithm/WeakRawTemporalTracker.h
    src/algorithm/RecoveryTemporalConfirmTracker.h
    src/algorithm/PacketSpline.h
    src/data/SimpleGraphItem.h
    src/data/AxialThicknessPlotItem.h
    src/data/AxialPipeFinalizer.h
    src/data/ThicknessHistoryItem.h
    src/data/HistoryRecord.h
    src/data/HistoryManager.h
    src/data/HistoryGraphItem.h
    src/data/ProcessAnalyzer.h
)

qt_add_library(lst_data SHARED
    ${DATA_SOURCES}
    ${DATA_HEADERS}
)

set_target_properties(lst_data PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
# Explicit Q_OBJECT symbol import/export across DLL boundary.
target_compile_definitions(lst_data PRIVATE LST_DATA_DLL_EXPORTS)
target_compile_definitions(lst_data INTERFACE LST_DATA_DLL_IMPORTS)
# GNU ld normally stops auto-exporting other classes once a few symbols
# are explicitly dllexported; preserve the earlier DLL's full public ABI.
if(MINGW)
    target_link_options(lst_data PRIVATE "-Wl,--export-all-symbols")
endif()

target_include_directories(lst_data
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(lst_data
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::Quick
        Qt6::Concurrent
)

add_library(lst_controllers SHARED IMPORTED GLOBAL)

set_target_properties(lst_controllers PROPERTIES
    IMPORTED_LOCATION ${PREBUILT_LIB_DIR}/liblstcore.dll
    IMPORTED_IMPLIB ${PREBUILT_LIB_DIR}/liblstcore.a
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/src
    INTERFACE_LINK_LIBRARIES "lst_models;lst_communication;Qt6::Core"
)

set(PREBUILT_RUNTIME_LIBS
    ${PREBUILT_LIB_DIR}/liblstcore.dll
)

set(PREBUILT_IMPORT_LIBS
    ${PREBUILT_LIB_DIR}/liblstcore.a
)

# 迁移一致性测试直接链接主程序实际使用的 ThicknessProcessor，
# 避免另一套未接入实现造成“测试通过但主程序不同”的假阳性。
qt_add_executable(IndustrialAlgorithmFileTest
    tools/IndustrialAlgorithmFileTest.cpp
)

target_include_directories(IndustrialAlgorithmFileTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(IndustrialAlgorithmFileTest
    PRIVATE
        lst_data
)

# Optional field-only PROTON UDP probe; never launched by appLST.
# Excluded from normal builds to keep production targets small.
qt_add_executable(RadarUdpProbe
    tools/RadarUdpProbe.cpp
    src/communication/RadarUdpReceiver.h
    src/communication/RadarUdpReceiver.cpp
    src/communication/RadarUdpProtocol.h
    src/communication/RadarUdpDiagnostics.h
)
target_include_directories(RadarUdpProbe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(RadarUdpProbe PRIVATE Qt6::Core Qt6::Network)
set_target_properties(RadarUdpProbe PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Guard the previously fixed Windows DLL/Qt signal boundary.
# Build explicitly when radar wiring is changed, not with appLST.
qt_add_executable(PipePresenceDllWiringTest
    tools/PipePresenceLiveWiringTest.cpp
)
target_include_directories(PipePresenceDllWiringTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(PipePresenceDllWiringTest PRIVATE lst_data Qt6::Core Qt6::Network)
set_target_properties(PipePresenceDllWiringTest PROPERTIES EXCLUDE_FROM_ALL TRUE)

qt_add_executable(PipePresenceStateTests
    tools/PipePresenceStateTests.cpp
    src/communication/PipePresenceStateController.cpp
    src/communication/PipePresenceStateController.h
)
target_include_directories(PipePresenceStateTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(PipePresenceStateTests PRIVATE Qt6::Core)
set_target_properties(PipePresenceStateTests PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Isolated 100Hz radar/DAQ host-time sidecar matcher regression.
qt_add_executable(RadarFrameMatcherTests
    tools/RadarFrameMatcherTests.cpp
    src/communication/RadarFrameMatcher.h
    src/communication/RadarUdpProtocol.h
)
target_include_directories(RadarFrameMatcherTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(RadarFrameMatcherTests PRIVATE Qt6::Core)
set_target_properties(RadarFrameMatcherTests PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Incremental GPU axial plot; real-time 100Hz input, <=10Hz scene graph redraw.
qt_add_executable(AxialThicknessPlotTests tools/AxialThicknessPlotTests.cpp)
target_include_directories(AxialThicknessPlotTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(AxialThicknessPlotTests PRIVATE lst_data Qt6::Core Qt6::Quick)
set_target_properties(AxialThicknessPlotTests PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Candidate LB2 pipe-head anchor, fail-closed after stop or mid-pipe startup.
qt_add_executable(PipeHeadAxialTrackerTests
    tools/PipeHeadAxialTrackerTests.cpp
    src/communication/PipeHeadAxialTracker.h
    src/communication/RadarFrameMatcher.h
)
target_include_directories(PipeHeadAxialTrackerTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(PipeHeadAxialTrackerTests PRIVATE Qt6::Core)
set_target_properties(PipeHeadAxialTrackerTests PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Optional BDAQ relative-clock / Frame ID thickness-position CSV regression.
qt_add_executable(RadarBdaqJoinTests
    tools/RadarBdaqJoinTests.cpp
    src/communication/RadarPositionSidecar.cpp
    src/communication/RadarPositionSidecar.h
    src/communication/RadarUdpSharedService.cpp
    src/communication/RadarUdpSharedService.h
    src/communication/RadarUdpReceiver.cpp
    src/communication/RadarUdpReceiver.h
    src/communication/RadarFrameMatcher.h
    src/communication/BdaqHostTimeAligner.h
    src/communication/RadarHostClock.h
    src/communication/RadarUdpProtocol.h
)
target_include_directories(RadarBdaqJoinTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(RadarBdaqJoinTests PRIVATE Qt6::Core Qt6::Network)
set_target_properties(RadarBdaqJoinTests PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Dynamic detection rate between stable thickness arrival and confirmed absence.
qt_add_executable(DynamicDetectionRateTests tools/DynamicDetectionRateTests.cpp)
target_include_directories(DynamicDetectionRateTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(DynamicDetectionRateTests PRIVATE Qt6::Core)
set_target_properties(DynamicDetectionRateTests PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Tail-only display/CSV interpolation; independent of physical 51mm geometry.
qt_add_executable(AxialTailFillTests tools/AxialTailFillTests.cpp)
target_include_directories(AxialTailFillTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(AxialTailFillTests PRIVATE Qt6::Core)
set_target_properties(AxialTailFillTests PROPERTIES EXCLUDE_FROM_ALL TRUE)

# One-shot final pipe profile: internal interpolation, measured-only stats
# and candidate total length after the existing tail completion event.
qt_add_executable(AxialPipeFinalizerTests tools/AxialPipeFinalizerTests.cpp)
target_include_directories(AxialPipeFinalizerTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(AxialPipeFinalizerTests PRIVATE Qt6::Core)
set_target_properties(AxialPipeFinalizerTests PROPERTIES EXCLUDE_FROM_ALL TRUE)

# End-to-end T2 false-positive guard on synthetic no-pipe waveforms.
qt_add_executable(T2NoiseOnlyRegressionTests tools/T2NoiseOnlyRegressionTests.cpp)
target_include_directories(T2NoiseOnlyRegressionTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(T2NoiseOnlyRegressionTests PRIVATE lst_data Qt6::Core)
set_target_properties(T2NoiseOnlyRegressionTests PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Header-only T2 tracker regression (shape changes must not lose LOCK).
add_executable(SecondEchoThickWallTrackerTests
    tools/SecondEchoThickWallTrackerTests.cpp
)
target_include_directories(SecondEchoThickWallTrackerTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

qt_add_executable(appLST
    main.cpp
    src/monitor/HealthMonitor.cpp
    src/monitor/HealthMonitor.h
)

qt_add_qml_module(appLST
    URI LST
    VERSION 1.0
    QML_FILES
        Main.qml
        qml/Theme.qml
        qml/components/StatusIndicator.qml
        qml/components/ValueDisplay.qml
        qml/components/ParamEditor.qml
        qml/components/SwitchControl.qml
        qml/components/CommandButton.qml
        qml/components/EngineeringPlotFrame.qml
        qml/components/EngineeringTrendPlot.qml
        qml/components/CardPanel.qml
        qml/components/NavigationRail.qml
        qml/components/ConnectionBar.qml
        qml/components/PulseChannelEditor.qml
        qml/components/InlineParamRow.qml
        qml/components/InlineMaskEditor.qml
        qml/pages/ConnectionPage.qml
        qml/pages/DashboardPage.qml
        qml/pages/SlaveBoardPage.qml
        qml/pages/MainBoardPage.qml
        qml/pages/SystemSettingsPage.qml
        qml/pages/CommandPage.qml
        qml/pages/DataPage.qml
        qml/pages/ExperimentDataPage.qml
        qml/pages/HistoryPage.qml
        qml/pages/ProcessAnalysisPage.qml
        SOURCES
            src/data/bdaqctrl.h
            src/data/HistoryRecord.h
            src/data/HistoryManager.h
            src/data/HistoryGraphItem.h
            src/data/ProcessAnalyzer.h
            QML_FILES
            QML_FILES
            QML_FILES qml/components/WaterCoolerCard.qml
)

set_target_properties(appLST PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_include_directories(appLST PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../AI_StreamingAI_Retrigger_Qt6
)

target_link_libraries(appLST
    PRIVATE
        lst_models
        lst_communication
        lst_controllers
        lst_data
        Qt6::Quick
        Qt6::QuickControls2
)

# BDAQ SDK library (Advantech)
if(DEFINED BDAQ_SDK_ROOT)
    if(WIN32)
        find_library(BDAQ_LIBRARY
            NAMES biodaq libbiodaq
            PATHS "${BDAQ_SDK_ROOT}/lib" "${BDAQ_SDK_ROOT}/Lib" "${BDAQ_SDK_ROOT}/x64"
            NO_DEFAULT_PATH
        )
        if(BDAQ_LIBRARY)
            target_link_libraries(lst_data PRIVATE ${BDAQ_LIBRARY})
            message(STATUS "BDAQ library found: ${BDAQ_LIBRARY}")
        else()
            message(WARNING "BDAQ library not found in BDAQ_SDK_ROOT=${BDAQ_SDK_ROOT}")
        endif()
    endif()
else()
    find_library(BDAQ_LIBRARY NAMES biodaq libbiodaq)
    if(BDAQ_LIBRARY)
        target_link_libraries(lst_data PRIVATE ${BDAQ_LIBRARY})
        message(STATUS "BDAQ library found: ${BDAQ_LIBRARY}")
    else()
        message(STATUS "BDAQ library (biodaq) not found. Set BDAQ_SDK_ROOT to the Advantech BDAQ SDK path.")
    endif()
endif()

# Windows multimedia timer
if(WIN32)
    target_link_libraries(appLST PRIVATE winmm)
endif()

add_custom_command(TARGET appLST POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${PREBUILT_RUNTIME_LIBS}
        $<TARGET_FILE_DIR:appLST>
)

include(GNUInstallDirs)
install(TARGETS appLST lst_communication lst_data
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${PREBUILT_RUNTIME_LIBS}
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${PREBUILT_IMPORT_LIBS}
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
)