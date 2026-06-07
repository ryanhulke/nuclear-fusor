cmake_minimum_required(VERSION 3.21)

project(FusorControlPanel LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc videoio)

include(FetchContent)

set(GLAD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/tools/glad")

if(NOT EXISTS "${GLAD_ROOT}/src/gl.c")
    message(FATAL_ERROR "Missing GLAD source: ${GLAD_ROOT}/src/gl.c")
endif()

if(NOT EXISTS "${GLAD_ROOT}/include/glad/gl.h")
    message(FATAL_ERROR "Missing GLAD header: ${GLAD_ROOT}/include/glad/gl.h")
endif()

add_library(glad STATIC
    "${GLAD_ROOT}/src/gl.c"
)

target_include_directories(glad PUBLIC
    "${GLAD_ROOT}/include"
)

find_package(glfw3 CONFIG QUIET)

if(NOT TARGET glfw)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
    )

    FetchContent_MakeAvailable(glfw)
endif()

set(SIMPLEBLE_TEST OFF CACHE BOOL "" FORCE)
set(SIMPLEBLE_EXCLUDE_C ON CACHE BOOL "" FORCE)
set(SIMPLEBLE_LOG_LEVEL INFO CACHE STRING "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    simpleble
    GIT_REPOSITORY https://github.com/simpleble/simpleble.git
    GIT_TAG v0.14.0
    GIT_SHALLOW YES
    SOURCE_SUBDIR simpleble
)

FetchContent_MakeAvailable(simpleble)

set(APP_SOURCES
    lib/Ble/BleScan.cpp
    lib/Camera/Camera.cpp
    lib/Control/PressureController.cpp
    lib/Geometry/Geometry.cpp
    lib/Math/Math.cpp
    lib/Meter/MeterMonitor.cpp
    lib/Plasma/PlasmaRenderer.cpp
    lib/Pressure/PressureMonitor.cpp
    lib/Render/Hud.cpp
    lib/Render/Render.cpp
    lib/Valve/ValveMonitor.cpp
    lib/Video/VideoBackground.cpp
    lib/Video/VideoStream.cpp
    src/main.cpp
)

if(WIN32)
    list(APPEND APP_SOURCES app.rc)
endif()

add_executable(app ${APP_SOURCES})

add_executable(playback
    lib/Render/Hud.cpp
    lib/Render/Render.cpp
    lib/Video/VideoBackground.cpp
    src/playback.cpp
)

target_include_directories(app PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/lib"
    ${OpenCV_INCLUDE_DIRS}
)

target_include_directories(playback PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/lib"
    ${OpenCV_INCLUDE_DIRS}
)

if(WIN32)
    target_compile_definitions(app PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(playback PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(app PROPERTIES
        OUTPUT_NAME "Fusor Control Panel"
        WIN32_EXECUTABLE TRUE
    )
    target_link_options(app PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/ENTRY:mainCRTStartup>)
    target_link_libraries(app PRIVATE dwmapi)
endif()

target_link_libraries(app PRIVATE
    glad
    glfw
    OpenGL::GL
    ${OpenCV_LIBS}
    simpleble::simpleble
)

target_link_libraries(playback PRIVATE
    glad
    glfw
    OpenGL::GL
    ${OpenCV_LIBS}
)

set_target_properties(playback PROPERTIES
    OUTPUT_NAME "Fusor Playback"
)

if(UNIX AND NOT APPLE)
    target_link_libraries(app PRIVATE ${CMAKE_DL_LIBS})
endif()
