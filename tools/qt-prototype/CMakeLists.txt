cmake_minimum_required(VERSION 3.24)
project(openglide-qt LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick)
qt_standard_project_setup()

# ---- IBus (GNOME UTF-8 commit path, ADR-0002) ----
find_package(PkgConfig REQUIRED)
pkg_check_modules(IBUS REQUIRED ibus-1.0)

# ---- layer-shell (KDE/wlroots focus guarantee; optional — GNOME builds without) ----
find_package(LayerShellQt QUIET)
if (LayerShellQt_FOUND)
    message(STATUS "LayerShellQt found: Wayland builds get keyboard-interactivity=none")
else ()
    message(STATUS "LayerShellQt NOT found: xcb-only focus semantics (fine on GNOME)")
endif ()
pkg_check_modules(GIO REQUIRED gio-2.0)

# ---- native FUTO decoder: build ExecuTorch (XNNPACK — the .pte is delegated) ----
set(EXECUTORCH_BUILD_XNNPACK          ON CACHE BOOL "" FORCE)
set(EXECUTORCH_BUILD_EXTENSION_TENSOR  ON CACHE BOOL "" FORCE)
set(EXECUTORCH_BUILD_EXTENSION_MODULE  ON CACHE BOOL "" FORCE)
set(EXECUTORCH_BUILD_EXTENSION_NAMED_DATA_MAP ON CACHE BOOL "" FORCE)
set(EXECUTORCH_BUILD_TESTS            OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING                     OFF CACHE BOOL "" FORCE)
set(ET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/executorch)
add_subdirectory(${ET_DIR} ${CMAKE_BINARY_DIR}/executorch EXCLUDE_FROM_ALL)
set(ET_LIBS executorch portable_ops_lib
           extension_data_loader extension_tensor extension_module
           xnnpack_backend)

# swipe_engine.{h,cpp} lives in the spike dir (shared with its corpus test)
set(SPIKE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../native-decode-spike)

qt_add_executable(openglide-qt
    src/main.cpp
    src/windowctl.cpp src/windowctl.h
    src/swipesurface.cpp src/swipesurface.h
    src/decoderbridge.cpp src/decoderbridge.h
    src/injector.cpp src/injector.h
    src/appsettings.cpp src/appsettings.h
    src/togglelistener.cpp src/togglelistener.h
    src/pointerspeed.cpp src/pointerspeed.h
    src/ibus_engine.c src/ibus_engine.h
    ${SPIKE_DIR}/swipe_engine.cpp ${SPIKE_DIR}/swipe_engine.h
)
qt_add_resources(openglide-qt "qml" PREFIX "/" FILES main.qml)

target_include_directories(openglide-qt PRIVATE ${SPIKE_DIR} ${IBUS_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS})
target_link_libraries(openglide-qt PRIVATE Qt6::Core Qt6::Gui Qt6::Quick ${ET_LIBS} ${IBUS_LIBRARIES} ${GIO_LIBRARIES})
if (LayerShellQt_FOUND)
    target_link_libraries(openglide-qt PRIVATE LayerShellQt::Interface)
    target_compile_definitions(openglide-qt PRIVATE OPENGLIDE_HAVE_LAYERSHELL)
endif ()

# ---- install: binary + desktop entry ----
# The .desktop carries no environment on purpose: platform selection happens
# in-process (src/main.cpp), so installed launches behave like run.sh launches.
include(GNUInstallDirs)
install(TARGETS openglide-qt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES org.openglide.OpenGlide.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
