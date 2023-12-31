# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.16)
project(openglunderqml LANGUAGES CXX)

if(NOT DEFINED INSTALL_EXAMPLESDIR)
    set(INSTALL_EXAMPLESDIR "examples")
endif()

set(INSTALL_EXAMPLEDIR "${INSTALL_EXAMPLESDIR}/quick/scenegraph/openglunderqml")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)

qt_standard_project_setup()

qt_add_executable(openglunderqml WIN32 MACOSX_BUNDLE
    main.cpp
    squircle.cpp squircle.h
)

set_target_properties(openglunderqml PROPERTIES

    # Prevent name clash with build subdirectory on case-insensitive file systems
    OUTPUT_NAME openglunderqmlapp
)

target_link_libraries(openglunderqml PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)

qt_add_qml_module(openglunderqml
    URI OpenGLUnderQML
    QML_FILES main.qml
    RESOURCE_PREFIX /scenegraph/openglunderqml
    NO_RESOURCE_TARGET_PATH
)

install(TARGETS openglunderqml
    RUNTIME DESTINATION "${INSTALL_EXAMPLEDIR}"
    BUNDLE DESTINATION "${INSTALL_EXAMPLEDIR}"
    LIBRARY DESTINATION "${INSTALL_EXAMPLEDIR}"
)
