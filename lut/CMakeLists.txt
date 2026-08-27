cmake_minimum_required(VERSION 3.16)

project(LST VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick QuickControls2 SerialBus SerialPort Concurrent)

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
    src/data/SimpleGraphItem.cpp
    src/data/ThicknessHistoryItem.cpp
    src/data/HistoryManager.cpp
    src/data/HistoryGraphItem.cpp
)

set(DATA_HEADERS
    src/data/DataController.h
    src/data/SimpleGraphItem.h
    src/data/ThicknessHistoryItem.h
    src/data/HistoryRecord.h
    src/data/HistoryManager.h
    src/data/HistoryGraphItem.h
)

qt_add_library(lst_data SHARED
    ${DATA_SOURCES}
    ${DATA_HEADERS}
)

set_target_properties(lst_data PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_include_directories(lst_data
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(lst_data
    PUBLIC
        Qt6::Core
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

qt_add_executable(appLST
    main.cpp
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
        qml/pages/HistoryPage.qml
        SOURCES
            src/data/bdaqctrl.h
            src/data/HistoryRecord.h
            src/data/HistoryManager.h
            src/data/HistoryGraphItem.h
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
