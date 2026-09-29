cmake_minimum_required(VERSION 3.16)

project(MyCodeApp VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Gui Test)

qt_standard_project_setup(REQUIRES 6.10)

# Логирование (M1-7)
include(FetchContent)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1)
FetchContent_MakeAvailable(spdlog)

# Tree-sitter (M2-2): runtime + грамматики C++/Python как статические
# библиотеки. SOURCE_SUBDIR на несуществующую папку — MakeAvailable только
# скачивает исходники, свои таргеты строим сами (не зависим от CMake-файлов
# самих репозиториев). Грамматики должны быть ABI 13+ (runtime 0.27 — 13..15).
FetchContent_Declare(tree_sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG v0.27.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _no_cmake)
FetchContent_Declare(tree_sitter_cpp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG v0.23.4
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _no_cmake)
FetchContent_Declare(tree_sitter_python
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-python.git
    GIT_TAG v0.25.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _no_cmake)
FetchContent_MakeAvailable(tree_sitter tree_sitter_cpp tree_sitter_python)

add_library(ts_runtime STATIC ${tree_sitter_SOURCE_DIR}/lib/src/lib.c)
target_include_directories(ts_runtime PUBLIC
    ${tree_sitter_SOURCE_DIR}/lib/include
    ${tree_sitter_SOURCE_DIR}/lib/src)

add_library(ts_cpp STATIC
    ${tree_sitter_cpp_SOURCE_DIR}/src/parser.c
    ${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c)
target_include_directories(ts_cpp PRIVATE ${tree_sitter_cpp_SOURCE_DIR}/src)

add_library(ts_python STATIC
    ${tree_sitter_python_SOURCE_DIR}/src/parser.c
    ${tree_sitter_python_SOURCE_DIR}/src/scanner.c)
target_include_directories(ts_python PRIVATE ${tree_sitter_python_SOURCE_DIR}/src)

set(SYNTAX_SOURCES
    src/services/syntaxhighlighter.h src/services/syntaxhighlighter.cpp)
set(SYNTAX_LIBS ts_runtime ts_cpp ts_python)
set(SYNTAX_QUERIES assets/queries/cpp.scm assets/queries/python.scm)

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

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(appMyCodeApp
    URI MyCodeApp
    QML_FILES
        Main.qml
        qml/Theme.qml qml/AppMenu.qml qml/AppMenuSeparator.qml qml/CrossIcon.qml
        qml/IconButton.qml qml/FlatButton.qml qml/TabStrip.qml qml/FindBar.qml
        qml/StatusBar.qml qml/EmptyState.qml qml/LoadOverlay.qml qml/CloseDialog.qml
        SOURCES src/gui/editerview.h src/gui/editerview.cpp
               src/services/documentmanager.h src/services/documentmanager.cpp
)

# Служебные классы вне QML-модуля
target_sources(appMyCodeApp PRIVATE
    src/services/highlighter.h src/services/highlighter.cpp
    src/services/searchengine.h src/services/searchengine.cpp
    ${SYNTAX_SOURCES}
)

target_link_libraries(appMyCodeApp
    PRIVATE Qt6::Quick spdlog::spdlog ${SYNTAX_LIBS}
)

qt_add_resources(appMyCodeApp "app_assets"
    PREFIX "/"
    FILES assets/highlight.json ${SYNTAX_QUERIES}
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

qt_add_executable(test_treesitter tests/treesitter_test.cpp)
target_include_directories(test_treesitter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_treesitter PRIVATE Qt6::Test ${SYNTAX_LIBS})
add_test(NAME treesitter COMMAND test_treesitter)

qt_add_executable(test_syntaxhighlighter
    ${CORE_SOURCES} ${SYNTAX_SOURCES} tests/syntaxhighlighter_test.cpp)
target_include_directories(test_syntaxhighlighter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_syntaxhighlighter PRIVATE Qt6::Gui Qt6::Test ${SYNTAX_LIBS})
qt_add_resources(test_syntaxhighlighter "test_queries" PREFIX "/" FILES ${SYNTAX_QUERIES})
add_test(NAME syntaxhighlighter COMMAND test_syntaxhighlighter)

qt_add_executable(test_editorview
    ${CORE_SOURCES} ${SYNTAX_SOURCES}
    src/gui/editerview.h src/gui/editerview.cpp
    src/services/highlighter.h src/services/highlighter.cpp
    src/services/searchengine.h src/services/searchengine.cpp
    tests/editorview_test.cpp)
target_include_directories(test_editorview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_editorview PRIVATE Qt6::Quick Qt6::Test ${SYNTAX_LIBS})
qt_add_resources(test_editorview "test_editor_assets" PREFIX "/"
    FILES assets/highlight.json ${SYNTAX_QUERIES})
add_test(NAME editorview COMMAND test_editorview)

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
