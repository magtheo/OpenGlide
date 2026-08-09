cmake_minimum_required(VERSION 3.16)
project(openglide-qt LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick)
qt_standard_project_setup()

qt_add_executable(openglide-qt
    src/main.cpp
    src/swipesurface.cpp src/swipesurface.h
    src/decoderbridge.cpp src/decoderbridge.h
    src/injector.cpp src/injector.h
)
qt_add_resources(openglide-qt "qml" PREFIX "/" FILES main.qml)
target_link_libraries(openglide-qt PRIVATE Qt6::Core Qt6::Gui Qt6::Quick)
