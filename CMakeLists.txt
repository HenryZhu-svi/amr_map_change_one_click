cmake_minimum_required(VERSION 3.21)

project(AMRMapManager VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Network)

qt_add_executable(amr-map-manager
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/core/Robot.h
    src/protocol/RbkProtocol.h
    src/protocol/RbkProtocol.cpp
    src/network/RequestJob.h
    src/network/RequestJob.cpp
    src/map/MapView.h
    src/map/MapView.cpp
)

# Source files use project-relative includes such as
# "protocol/RbkProtocol.h" and "network/RequestJob.h".
target_include_directories(amr-map-manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(amr-map-manager PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
)

if(WIN32)
    set_target_properties(amr-map-manager PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

install(TARGETS amr-map-manager
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
