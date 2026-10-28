cmake_minimum_required(VERSION 3.21)
project(graphite VERSION 0.26.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GRAPHITE_BUILD_UI "Build the Qt interface" ON)
# A packaged build (distro package, macOS app bundle) has no source checkout: the update watcher,
# "install from Settings" and their sidebar row are left out.
if(APPLE)
    set(GRAPHITE_PACKAGED ON CACHE BOOL "Build for a package: no in-app updater" FORCE)
else()
    option(GRAPHITE_PACKAGED "Build for a package: no in-app updater" OFF)
endif()

# File handling core: plain C++, no Qt.
add_library(graphite_core STATIC src/core/document.cpp)
target_include_directories(graphite_core PUBLIC src)
target_compile_options(graphite_core PRIVATE -Wall -Wextra)

enable_testing()
add_executable(core_test tests/core_test.cpp)
target_link_libraries(core_test PRIVATE graphite_core)
add_test(NAME core COMMAND core_test)

if(GRAPHITE_BUILD_UI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
    set(CMAKE_AUTOMOC ON)

    # MicroTeX (MIT) typesets the formulas; tinyxml2 reads its font tables. MicroTeX is a git
    # submodule (our fork, branch graphite: one patch to the Qt backend); its samples and the
    # Cairo / Skia / Windows backends are left out.
    if(NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/microtex/src/latex.cpp)
        message(FATAL_ERROR "third_party/microtex is empty. Fetch it with:\n"
                            "  git submodule update --init\n"
                            "(or clone with: git clone --recursive ...)")
    endif()
    file(GLOB_RECURSE MICROTEX_SOURCES CONFIGURE_DEPENDS third_party/microtex/src/*.cpp)
    list(FILTER MICROTEX_SOURCES EXCLUDE REGEX "/src/(samples|platform/(cairo|skia|gdi_win))/")
    add_library(microtex STATIC ${MICROTEX_SOURCES} third_party/tinyxml2/tinyxml2.cpp)
    target_include_directories(microtex PUBLIC third_party/microtex/src third_party/tinyxml2)
    target_compile_definitions(microtex PUBLIC BUILD_QT)
    target_compile_options(microtex PRIVATE -w)
    target_link_libraries(microtex PUBLIC Qt6::Gui)
    set_target_properties(microtex PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(NOT APPLE)
        install(DIRECTORY third_party/microtex/res/ DESTINATION share/graphite/tex PATTERN SAMPLES.tex EXCLUDE)
    endif()

    add_library(graphite_ui STATIC
        # Window, outliner, panels, theme
        src/ui/explorer.cpp
        src/ui/explorer.hpp
        src/ui/mainwindow.cpp
        src/ui/mainwindow.hpp
        src/ui/nervpanel.cpp
        src/ui/nervpanel.hpp
        src/ui/outlinerview.cpp
        src/ui/outlinerview.hpp
        src/ui/overlay.cpp
        src/ui/overlay.hpp
        src/ui/settingspage.cpp
        src/ui/settingspage.hpp
        src/ui/snippets.cpp
        src/ui/snippets.hpp
        src/ui/theme.cpp
        src/ui/theme.hpp
        src/ui/thirdimpact.cpp
        src/ui/thirdimpact.hpp
        src/ui/unitart.cpp
        src/ui/unitart.hpp
        # Link index, definitions, Graphite Essentials
        src/notes/essentials.cpp
        src/notes/essentials.hpp
        src/notes/glossary.cpp
        src/notes/glossary.hpp
        src/notes/graphindex.cpp
        src/notes/graphindex.hpp
        # Tasks, progress bars, timetable, timers
        src/planner/operation.cpp
        src/planner/operation.hpp
        src/planner/progress.cpp
        src/planner/progress.hpp
        src/planner/tasks.cpp
        src/planner/tasks.hpp
        src/planner/timetable.cpp
        src/planner/timetable.hpp
        # Chemistry: \ce, molar masses, balancing, oxidation states
        src/chem/balance.cpp
        src/chem/balance.hpp
        src/chem/chem.cpp
        src/chem/chem.hpp
        src/chem/molar.cpp
        src/chem/molar.hpp
        src/chem/oxidation.cpp
        src/chem/oxidation.hpp
        # Quick maths with units, lab tables, graphs
        src/maths/calc.cpp
        src/maths/calc.hpp
        src/maths/graph.cpp
        src/maths/graph.hpp
        src/maths/labtable.cpp
        src/maths/labtable.hpp
        # Formulas (MicroTeX) and images inside the text
        src/render/imageobject.cpp
        src/render/imageobject.hpp
        src/render/mathobject.cpp
        src/render/mathobject.hpp
        src/render/tex.cpp
        src/render/tex.hpp
        # Wallpaper colours, Hyprland blur, the update watcher
        src/desktop/blur.cpp
        src/desktop/blur.hpp
        src/desktop/palette.cpp
        src/desktop/palette.hpp
        src/desktop/updates.cpp
        src/desktop/updates.hpp
    )
    target_include_directories(graphite_ui PUBLIC src)
    target_link_libraries(graphite_ui PUBLIC graphite_core microtex Qt6::Widgets Qt6::Network)
    # Graphite Essentials: the pages every tree gets once (essentials.cpp).
    qt_add_resources(graphite_ui "essentials" PREFIX "/essentials" BASE src/notes/essentials
        FILES "src/notes/essentials/Lab tables.md" "src/notes/essentials/Graphs.md")
    target_compile_definitions(graphite_ui PRIVATE GRAPHITE_VERSION_TEXT="${PROJECT_VERSION}")
    if(NOT GRAPHITE_PACKAGED)  # a package must not point into the folder it was built in
        target_compile_definitions(graphite_ui PRIVATE GRAPHITE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    endif()
    target_compile_options(graphite_ui PRIVATE -Wall -Wextra)
    if(GRAPHITE_PACKAGED)
        target_compile_definitions(graphite_ui PUBLIC GRAPHITE_PACKAGED)
    endif()
    if(APPLE)
        find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
        find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
        target_link_libraries(graphite_ui PUBLIC ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
    endif()

    # The field manual (F2) as a page that opens from disk: docs/field-manual.html is written for
    # claude.ai, which adds the doctype and charset; the build copy gets them here.
    file(READ ${CMAKE_SOURCE_DIR}/docs/field-manual.html GRAPHITE_MANUAL)
    file(WRITE ${CMAKE_BINARY_DIR}/field-manual.html
        "<!doctype html>\n<html lang=\"en\">\n<meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        "${GRAPHITE_MANUAL}\n</html>\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/docs/field-manual.html)
    if(NOT APPLE)
        install(FILES ${CMAKE_BINARY_DIR}/field-manual.html DESTINATION share/graphite)
    endif()

    add_executable(graphite src/main.cpp)
    target_link_libraries(graphite PRIVATE graphite_ui)
    target_compile_definitions(graphite PRIVATE GRAPHITE_VERSION="${PROJECT_VERSION}")
    if(APPLE)
        # Graphite.app: Contents/MacOS/graphite, Contents/Resources/{tex,fonts,Graphite.icns}.
        # The CI adds fonts/ (Noto, OFL) before macdeployqt and signing.
        set(MACOSX_BUNDLE_BUNDLE_NAME "Graphite")
        set(MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.graphite-planner")
        set(MACOSX_BUNDLE_COPYRIGHT "Copyright (c) Graphite contributors")
        set(MACOSX_BUNDLE_ICON_FILE "")
        set_target_properties(graphite PROPERTIES
            MACOSX_BUNDLE ON
            OUTPUT_NAME "Graphite"
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
        set(GRAPHITE_ICNS ${CMAKE_SOURCE_DIR}/packaging/macos/Graphite.icns)
        if(EXISTS ${GRAPHITE_ICNS})
            set(MACOSX_BUNDLE_ICON_FILE "Graphite.icns")
            target_sources(graphite PRIVATE ${GRAPHITE_ICNS})
            set_source_files_properties(${GRAPHITE_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        endif()
        add_custom_command(TARGET graphite POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/third_party/microtex/res
                    $<TARGET_BUNDLE_CONTENT_DIR:graphite>/Resources/tex
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/field-manual.html
                    $<TARGET_BUNDLE_CONTENT_DIR:graphite>/Resources/field-manual.html
            COMMENT "Copying the MicroTeX resources and the field manual into the bundle")
        install(TARGETS graphite BUNDLE DESTINATION .)
    else()
        install(TARGETS graphite RUNTIME DESTINATION bin)
    endif()

    add_executable(palette_test tests/palette_test.cpp)
    target_link_libraries(palette_test PRIVATE graphite_ui)
    add_test(NAME palette COMMAND palette_test)

    add_executable(index_test tests/index_test.cpp)
    target_link_libraries(index_test PRIVATE graphite_ui)
    add_test(NAME index COMMAND index_test)

    add_executable(chem_test tests/chem_test.cpp)
    target_link_libraries(chem_test PRIVATE graphite_ui)
    add_test(NAME chem COMMAND chem_test)
    set_tests_properties(chem PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;GRAPHITE_TEX_RES=${CMAKE_SOURCE_DIR}/third_party/microtex/res")

    add_executable(ui_test tests/ui_test.cpp)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    target_link_libraries(ui_test PRIVATE graphite_ui Qt6::Test)
    add_test(NAME ui COMMAND ui_test)
    set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;GRAPHITE_TEX_RES=${CMAKE_SOURCE_DIR}/third_party/microtex/res")
endif()
