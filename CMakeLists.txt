cmake_minimum_required(VERSION 3.16)

project(MyCodeApp VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Test)

qt_standard_project_setup(REQUIRES 6.10)

set(CORE_SOURCES
    src/core/textbuffer.h src/core/textbuffer.cpp
    src/core/document.h src/core/document.cpp
)

# Content of downloaded design from Figma to Qt or similar
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/importedcontent/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/importedcontent)
endif()

qt_add_executable(appMyCodeApp
    main.cpp
    ${CORE_SOURCES}
)

qt_add_qml_module(appMyCodeApp
    URI MyCodeApp
    QML_FILES
        Main.qml
        SOURCES textspikeiten.h textspikeiten.cpp
               textspikeqsgitem.h textspikeqsgitem.cpp
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appMyCodeApp PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appMyCodeApp
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_include_directories(appMyCodeApp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(appMyCodeApp
    PRIVATE Qt6::Quick
)

enable_testing()
qt_add_executable(test_core ${CORE_SOURCES} tests/textbuffer_test.cpp)
target_include_directories(test_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_core PRIVATE Qt6::Test)
add_test(NAME textbuffer COMMAND test_core)

install(TARGETS appMyCodeApp
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
