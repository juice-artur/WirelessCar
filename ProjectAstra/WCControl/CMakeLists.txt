cmake_minimum_required(VERSION 3.16)

project(WCControl VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Bluetooth)

qt_standard_project_setup(REQUIRES 6.10)

# Content of downloaded design from Figma to Qt or similar
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/importedcontent/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/importedcontent)
endif()

# Theme.qml is a QML singleton
set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_executable(appWCControl
    main.cpp
    src/util/HexUtils.h
    src/log/LogModel.h
    src/log/LogModel.cpp
    src/ble/BleDeviceModel.h
    src/ble/BleDeviceModel.cpp
    src/ble/NusClient.h
    src/ble/NusClient.cpp
    src/app/AppController.h
    src/app/AppController.cpp
)

# The generated QML type registration includes headers by their file name, so
# every header directory has to be on the include path.
target_include_directories(appWCControl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ble
    ${CMAKE_CURRENT_SOURCE_DIR}/src/log
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
)

qt_add_qml_module(appWCControl
    URI WCControl
    QML_FILES
        Main.qml
        Theme.qml
        AppHeader.qml
        HomePage.qml
        ConnectionView.qml
        ConnectPage.qml
        LogView.qml
        SendBar.qml
        AboutPage.qml
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appWCControl PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appWCControl
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appWCControl
    PRIVATE Qt6::Quick Qt6::Bluetooth
)

install(TARGETS appWCControl
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
