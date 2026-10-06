cmake_minimum_required(VERSION 3.16)

project(MyCodeApp VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # для clangd

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Gui Network Test LinguistTools)

qt_standard_project_setup(REQUIRES 6.10)

include(FetchContent)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1)
FetchContent_MakeAvailable(spdlog)

# Tree-sitter: runtime + грамматики C++/Python как статические
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
FetchContent_Declare(tree_sitter_qmljs
    GIT_REPOSITORY https://github.com/yuja/tree-sitter-qmljs.git
    GIT_TAG 0.3.1
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _no_cmake)
FetchContent_MakeAvailable(tree_sitter tree_sitter_cpp tree_sitter_python tree_sitter_qmljs)

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

add_library(ts_qmljs STATIC
    ${tree_sitter_qmljs_SOURCE_DIR}/src/parser.c
    ${tree_sitter_qmljs_SOURCE_DIR}/src/scanner.c)
target_include_directories(ts_qmljs PRIVATE ${tree_sitter_qmljs_SOURCE_DIR}/src)

# libgit2: статически и без сети — только локальные операции с репозиторием
FetchContent_Declare(libgit2
    GIT_REPOSITORY https://github.com/libgit2/libgit2.git
    GIT_TAG v1.9.7
    GIT_SHALLOW TRUE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CLI OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(USE_HTTPS OFF CACHE STRING "" FORCE)
set(USE_SSH OFF CACHE STRING "" FORCE)
set(USE_GSSAPI OFF CACHE STRING "" FORCE)
set(USE_NTLMCLIENT OFF CACHE BOOL "" FORCE)
set(USE_BUNDLED_ZLIB ON CACHE STRING "" FORCE)
set(REGEX_BACKEND builtin CACHE STRING "" FORCE)
set(CMAKE_C_STANDARD 11 CACHE STRING "" FORCE) # иначе libgit2 оставляет в кэше C90 для всех C-целей
FetchContent_MakeAvailable(libgit2)

set(SYNTAX_SOURCES
    src/services/syntaxhighlighter.h src/services/syntaxhighlighter.cpp
    src/services/palette.h src/services/palette.cpp)
set(SYNTAX_LIBS ts_runtime ts_cpp ts_python ts_qmljs)
set(SYNTAX_QUERIES assets/queries/cpp.scm assets/queries/python.scm assets/queries/qml.scm)

set(CORE_SOURCES
    src/core/piecetable.h src/core/piecetable.cpp
    src/core/textbuffer.h src/core/textbuffer.cpp
    src/core/document.h src/core/document.cpp
    src/core/editing.h src/core/editing.cpp
    src/core/folding.h src/core/folding.cpp
    src/core/mappedtext.h src/core/mappedtext.cpp
    src/core/snippet.h src/core/snippet.cpp
)

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
        qml/FileIcon.qml qml/SearchField.qml qml/FileTree.qml qml/SearchPanel.qml
        qml/Sidebar.qml qml/QuickOpen.qml qml/ReloadDialog.qml
        qml/CompletionPopup.qml qml/HoverPopup.qml qml/BottomPanel.qml qml/RenamePopup.qml
        qml/GitPanel.qml qml/DiffView.qml qml/Command.qml qml/SettingsDialog.qml qml/PluginsPage.qml qml/GoToLinePopup.qml qml/AboutDialog.qml
        SOURCES src/gui/editerview.h src/gui/editerview.cpp
                src/gui/minimap.h src/gui/minimap.cpp
                src/services/workspace.h src/services/workspace.cpp
                src/services/projectmodel.h src/services/projectmodel.cpp
                src/services/projectsearch.h src/services/projectsearch.cpp
                src/lsp/lspmanager.h src/lsp/lspmanager.cpp
                src/git/gitservice.h src/git/gitservice.cpp
                src/services/shellintegration.h src/services/shellintegration.cpp
                src/services/thememanager.h src/services/thememanager.cpp
                src/services/keymap.h src/services/keymap.cpp
                src/services/pluginmanager.h src/services/pluginmanager.cpp
                src/services/snippetstore.h src/services/snippetstore.cpp
                src/services/localization.h src/services/localization.cpp
                src/services/appinfo.h
               src/services/documentmanager.h src/services/documentmanager.cpp
)

# Служебные классы вне QML-модуля
target_sources(appMyCodeApp PRIVATE
    src/services/highlighter.h src/services/highlighter.cpp
    src/services/searchengine.h src/services/searchengine.cpp
    src/services/recovery.h src/services/recovery.cpp
    src/services/ignorerules.h src/services/ignorerules.cpp
    src/lsp/lspclient.h src/lsp/lspclient.cpp
    src/git/gitrepo.h src/git/gitrepo.cpp
    src/services/singleinstance.h src/services/singleinstance.cpp
    src/services/appsettings.h src/services/appsettings.cpp
    ${SYNTAX_SOURCES}
)

target_link_libraries(appMyCodeApp
    PRIVATE Qt6::Quick Qt6::Network spdlog::spdlog ${SYNTAX_LIBS} libgit2package
)
target_include_directories(appMyCodeApp SYSTEM PRIVATE ${libgit2_SOURCE_DIR}/include)
target_compile_definitions(appMyCodeApp PRIVATE APP_VERSION="${PROJECT_VERSION}")

# Демо-плагин: DLL в build/<конфигурация>/plugins рядом с exe
add_library(texttools MODULE plugins/texttools/texttools.cpp plugins/texttools/plugin.json include/mycodeapp/plugin.h)
target_include_directories(texttools PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(texttools PRIVATE Qt6::Core)
set_target_properties(texttools PROPERTIES PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
add_dependencies(appMyCodeApp texttools)

# Перевод интерфейса: исходные строки — русские, en — перевод (Qt Linguist).
# Обновить .ts после правки строк: cmake --build <сборка> --target update_translations
qt_add_translations(appMyCodeApp
    TS_FILES i18n/mycodeapp_en.ts
    SOURCE_TARGETS appMyCodeApp
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete -source-language ru -target-language en)

qt_add_resources(appMyCodeApp "app_assets"
    PREFIX "/"
    FILES assets/highlight.json assets/lsp.json ${SYNTAX_QUERIES}
          assets/themes/dark.json assets/themes/light.json
          assets/snippets/cpp.json assets/snippets/python.json assets/snippets/qml.json
)

set_target_properties(appMyCodeApp PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_include_directories(appMyCodeApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core ${CMAKE_CURRENT_SOURCE_DIR}/src/services
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lsp)

enable_testing()
qt_add_executable(test_core ${CORE_SOURCES} tests/textbuffer_test.cpp)
target_include_directories(test_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_core PRIVATE Qt6::Test)
add_test(NAME textbuffer COMMAND test_core)

qt_add_executable(test_highlighter
    src/services/highlighter.h src/services/highlighter.cpp
    src/services/palette.h src/services/palette.cpp
    tests/highlighter_test.cpp)
target_include_directories(test_highlighter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_highlighter PRIVATE Qt6::Gui Qt6::Test)
add_test(NAME highlighter COMMAND test_highlighter)

qt_add_executable(test_searchengine
    src/core/piecetable.h src/core/piecetable.cpp src/core/mappedtext.h src/core/mappedtext.cpp
    src/services/searchengine.h src/services/searchengine.cpp
    tests/searchengine_test.cpp)
target_include_directories(test_searchengine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_searchengine PRIVATE Qt6::Test)
add_test(NAME searchengine COMMAND test_searchengine)

qt_add_executable(test_piecetable
    src/core/piecetable.h src/core/piecetable.cpp src/core/mappedtext.h src/core/mappedtext.cpp
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

qt_add_executable(test_editing ${CORE_SOURCES} tests/editing_test.cpp)
target_include_directories(test_editing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_editing PRIVATE Qt6::Test)
add_test(NAME editing COMMAND test_editing)

qt_add_executable(test_folding ${CORE_SOURCES} tests/folding_test.cpp)
target_include_directories(test_folding PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_folding PRIVATE Qt6::Test)
add_test(NAME folding COMMAND test_folding)

qt_add_executable(test_project
    src/services/ignorerules.h src/services/ignorerules.cpp
    src/services/workspace.h src/services/workspace.cpp
    src/services/appsettings.h src/services/appsettings.cpp
    tests/project_test.cpp)
target_include_directories(test_project PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_project PRIVATE Qt6::Test Qt6::Qml)
add_test(NAME project COMMAND test_project)

qt_add_executable(test_recovery ${CORE_SOURCES}
    src/services/recovery.h src/services/recovery.cpp
    src/services/appsettings.h src/services/appsettings.cpp tests/recovery_test.cpp)
target_include_directories(test_recovery PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_recovery PRIVATE Qt6::Test)
add_test(NAME recovery COMMAND test_recovery)

# Бенчмарк piece table (запуск вручную): bench_piecetable.exe <файл>
qt_add_executable(bench_piecetable
    src/core/piecetable.h src/core/piecetable.cpp src/core/mappedtext.h src/core/mappedtext.cpp
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

qt_add_executable(test_git src/git/gitrepo.h src/git/gitrepo.cpp tests/git_test.cpp)
target_include_directories(test_git PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(test_git SYSTEM PRIVATE ${libgit2_SOURCE_DIR}/include)
target_link_libraries(test_git PRIVATE Qt6::Test libgit2package)
add_test(NAME git COMMAND test_git)

install(TARGETS appMyCodeApp
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_add_executable(test_snippet src/core/snippet.h src/core/snippet.cpp tests/snippet_test.cpp)
target_include_directories(test_snippet PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_snippet PRIVATE Qt6::Test)
add_test(NAME snippet COMMAND test_snippet)
