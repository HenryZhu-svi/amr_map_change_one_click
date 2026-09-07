cmake_minimum_required(VERSION 3.21)

project(AMRMapManager VERSION 0.2.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

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
    resources.qrc
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
    set(APP_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/svi-logo.ico")
    configure_file(packaging/windows/app.rc.in
                   "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    target_sources(amr-map-manager PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

install(TARGETS amr-map-manager
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/amr-map-manager.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES logo/svi_logo.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME amr-map-manager.svg)

    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "amr-map-manager")
    set(CPACK_PACKAGE_VENDOR "SVI")
    set(CPACK_PACKAGE_CONTACT "shuang.zhu@svi.co.th")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Manage, transfer, verify, switch, and preview SEER AMR maps")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist/ubuntu")
    include(CPack)
endif()
