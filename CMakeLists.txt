cmake_minimum_required(VERSION 3.20)
project(relapsegtk LANGUAGES C)

# Avoid embedding the builder's checkout path in diagnostics or debug records.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options("-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.16)
pkg_check_modules(GTKSOURCE REQUIRED IMPORTED_TARGET gtksourceview-5>=5.16)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-unix-2.0)
pkg_check_modules(JSON_GLIB REQUIRED IMPORTED_TARGET json-glib-1.0)
pkg_check_modules(LIBCURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)

add_library(relapse-settings STATIC
    src/settings/editor.c
    src/settings/session.c
    src/settings/output.c
    src/settings/store.c
    src/settings/launcher.c
    src/settings/appearance.c
    src/settings/scripts.c
)
target_compile_features(relapse-settings PUBLIC c_std_11)
target_include_directories(relapse-settings PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(relapse-settings PUBLIC PkgConfig::GIO)

add_library(relapse-state STATIC src/state/session.c)
target_compile_features(relapse-state PUBLIC c_std_11)
target_include_directories(relapse-state PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(relapse-state PUBLIC PkgConfig::GIO PRIVATE PkgConfig::JSON_GLIB)

add_library(relapse-profiles STATIC
    src/profiles/store.c
    src/profiles/config.c
    src/profiles/account.c
)
target_compile_features(relapse-profiles PUBLIC c_std_11)
target_include_directories(relapse-profiles PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(relapse-profiles PUBLIC PkgConfig::GIO PkgConfig::JSON_GLIB PkgConfig::LIBCURL)

add_library(relapse-socket STATIC src/socket/transport.c src/socket/client.c)
target_compile_features(relapse-socket PUBLIC c_std_11)
target_include_directories(relapse-socket PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(relapse-socket PUBLIC PkgConfig::GIO PRIVATE PkgConfig::JSON_GLIB)

add_library(relapse-bridge STATIC
    src/socket/bridge.c
    src/socket/console.c
    src/socket/dialogs.c
)
target_compile_features(relapse-bridge PUBLIC c_std_11)
target_include_directories(relapse-bridge PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(relapse-bridge PUBLIC relapse-socket PkgConfig::JSON_GLIB)

add_library(relapse-sessions STATIC src/socket/sessions.c)
target_compile_features(relapse-sessions PUBLIC c_std_11)
target_include_directories(relapse-sessions PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(relapse-sessions PUBLIC relapse-bridge)

add_library(relapse-ui STATIC
    src/ui/accounts.c
    src/ui/blur.c
    src/ui/configuration.c
    src/ui/console.c
    src/ui/dialogs.c
    src/ui/editor.c
    src/ui/editor_wrap.c
    src/ui/output.c
    src/ui/picker.c
    src/ui/profiles.c
    src/ui/scripts.c
    src/ui/search.c
    src/ui/settings.c
    src/ui/shader_strip.c
    src/ui/style.c
    src/ui/tabs.c
    src/ui/tab_strip.c
    src/ui/toolbar.c
    src/ui/topographic.c
    src/ui/window.c
    src/themes/theme.c
)
target_compile_features(relapse-ui PUBLIC c_std_11)
target_include_directories(relapse-ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(relapse-ui PUBLIC
    PkgConfig::GTK4
    PkgConfig::GTKSOURCE
    relapse-settings
    relapse-profiles
    PRIVATE
    relapse-state
    PkgConfig::EPOXY
    PkgConfig::JSON_GLIB
    m
)

pkg_check_modules(WAYLAND IMPORTED_TARGET wayland-client)
if(WAYLAND_FOUND)
    target_compile_definitions(relapse-ui PRIVATE RELAPSEGTK_HAVE_WAYLAND=1)
    target_link_libraries(relapse-ui PRIVATE PkgConfig::WAYLAND)
endif()

find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
set(APP_RESOURCES "${CMAKE_CURRENT_BINARY_DIR}/relapsegtk-resources.c")
add_custom_command(
    OUTPUT "${APP_RESOURCES}"
    COMMAND "${GLIB_COMPILE_RESOURCES}"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/relapsegtk.gresource.xml"
        --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/resources
        --target=${APP_RESOURCES}
        --generate-source --c-name=relapsegtk
    DEPENDS
        resources/relapsegtk.gresource.xml
        resources/style.css
        resources/scripts/session.luau
        resources/scripts/console.luau
        resources/scripts/dialogs.luau
        resources/styles/relapse.xml
        resources/icons/relapse.svg
        resources/icons/org.relapse.RelapseGtk.png
        resources/icons/caret-down.svg
        resources/icons/profiles.svg
        resources/icons/editor.svg
        resources/icons/settings.svg
        resources/icons/folder-open.svg
        resources/icons/play.svg
        resources/icons/save.svg
        resources/icons/output.svg
    VERBATIM)

add_library(relapse-resources OBJECT "${APP_RESOURCES}")
target_link_libraries(relapse-resources PRIVATE PkgConfig::GIO)

option(RELAPSEGTK_ENABLE_UPDATER "Enable in-app updater support" OFF)
if(RELAPSEGTK_ENABLE_UPDATER)
    if(NOT DEFINED RELAPSEGTK_UPDATE_PUBLIC_KEY OR NOT EXISTS "${RELAPSEGTK_UPDATE_PUBLIC_KEY}")
        message(FATAL_ERROR "RELAPSEGTK_UPDATE_PUBLIC_KEY must point to a valid public key file")
    endif()
    file(READ "${RELAPSEGTK_UPDATE_PUBLIC_KEY}" KEY_HEX HEX)
    string(LENGTH "${KEY_HEX}" KEY_HEX_LEN)
    math(EXPR RELAPSEGTK_UPDATE_KEY_SIZE "${KEY_HEX_LEN} / 2")
    string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1, " RELAPSEGTK_UPDATE_KEY_BYTES "${KEY_HEX}")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/update-key.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/relapsegtk-update-key.h"
        @ONLY
    )
    add_library(relapse-update STATIC src/update/update.c)
    target_compile_features(relapse-update PUBLIC c_std_11)
    target_include_directories(relapse-update PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(relapse-update PUBLIC
        RELAPSEGTK_UPDATE_ENABLED=1
        "RELAPSEGTK_VERSION=\"${RELAPSEGTK_VERSION}\""
        "RELAPSEGTK_UPDATE_REPOSITORY=\"${RELAPSEGTK_UPDATE_REPOSITORY}\""
    )
    target_link_libraries(relapse-update PUBLIC PkgConfig::GIO PkgConfig::JSON_GLIB)
endif()

add_executable(relapsegtk src/main.c src/app.c)
target_compile_features(relapsegtk PRIVATE c_std_11)
target_include_directories(relapsegtk PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(relapsegtk PRIVATE relapse-ui relapse-sessions relapse-resources)
if(RELAPSEGTK_ENABLE_UPDATER)
    target_link_libraries(relapsegtk PRIVATE relapse-update)
endif()
