cmake_minimum_required(VERSION 3.16)

project(lstlib LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

add_library(lstlib SHARED
  lstlib_global.h
  lstlib.cpp
  lstlib.h
)

target_link_libraries(lstlib PRIVATE Qt${QT_VERSION_MAJOR}::Core)

target_compile_definitions(lstlib PRIVATE LSTLIB_LIBRARY)
