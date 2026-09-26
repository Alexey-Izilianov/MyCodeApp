cmake_minimum_required(VERSION 3.16)

project(MyCodeApp VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Gui Test)

qt_standard_project_setup(REQUIRES 6.10)

# Логирование (M1-7)
include(FetchContent)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1)
FetchContent_MakeAvailable(spdlog)

set(CORE_SOURCES
    src/core/piecetable.h src/core/piecetable.cpp
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
        SOURCES src/gui/editerview.h src/gui/editerview.cpp
               src/services/documentmanager.h src/services/documentmanager.cpp
)

# Служебные классы вне QML-модуля
target_sources(appMyCodeApp PRIVATE
    src/services/highlighter.h src/services/highlighter.cpp
    src/services/searchengine.h src/services/searchengine.cpp
)

target_link_libraries(appMyCodeApp
    PRIVATE Qt6::Quick spdlog::spdlog
)

# JSON-правила подсветки — в ресурсы приложения
qt_add_resources(appMyCodeApp "app_assets"
    PREFIX "/"
    FILES assets/highlight.json
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

target_include_directories(appMyCodeApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core ${CMAKE_CURRENT_SOURCE_DIR}/src/services)

enable_testing()
qt_add_executable(test_core ${CORE_SOURCES} tests/textbuffer_test.cpp)
target_include_directories(test_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_core PRIVATE Qt6::Test)
add_test(NAME textbuffer COMMAND test_core)

qt_add_executable(test_highlighter
    src/services/highlighter.h src/services/highlighter.cpp
    tests/highlighter_test.cpp)
target_include_directories(test_highlighter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_highlighter PRIVATE Qt6::Gui Qt6::Test)
add_test(NAME highlighter COMMAND test_highlighter)

qt_add_executable(test_searchengine
    src/core/piecetable.h src/core/piecetable.cpp
    src/services/searchengine.h src/services/searchengine.cpp
    tests/searchengine_test.cpp)
target_include_directories(test_searchengine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_searchengine PRIVATE Qt6::Test)
add_test(NAME searchengine COMMAND test_searchengine)

qt_add_executable(test_piecetable
    src/core/piecetable.h src/core/piecetable.cpp
    tests/piecetable_test.cpp)
target_include_directories(test_piecetable PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_piecetable PRIVATE Qt6::Test)
add_test(NAME piecetable COMMAND test_piecetable)

# Бенчмарк M2-1 (запуск вручную): bench_piecetable.exe <файл>
qt_add_executable(bench_piecetable
    src/core/piecetable.h src/core/piecetable.cpp
    tests/bench_piecetable.cpp)
target_include_directories(bench_piecetable PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bench_piecetable PRIVATE Qt6::Core)
set_target_properties(bench_piecetable PROPERTIES WIN32_EXECUTABLE FALSE)

qt_add_executable(test_document
    src/core/document.h src/core/document.cpp
    ${CORE_SOURCES}
    tests/document_test.cpp)
target_include_directories(test_document PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_document PRIVATE Qt6::Test)
add_test(NAME document COMMAND test_document)

install(TARGETS appMyCodeApp
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
