cmake_minimum_required(VERSION 3.24)

project(LinuxTSPlayer
    VERSION 0.1.0
    DESCRIPTION "A Libadwaita player for the Tabula Sonora Sound Canvas voice"
    # C as well as C++: glib-compile-resources emits C, and gconstructor.h is not C++.
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(TSGUI_APP_ID "co.losno.TabulaSonoraPlayer" CACHE STRING
    "Reverse-DNS application id, used for the bus name, GSettings schema and desktop file")
set(TSGUI_BINARY_NAME "tabula-sonora-player")

# ---------------------------------------------------------------------------------------------
# The engine
#
# NativeTS is consumed either in-tree or from an install; both paths yield ts::tabulasonora, and
# with it C++20 and the numeric-semantics flags the engine's correctness depends on.
# ---------------------------------------------------------------------------------------------

option(TSGUI_USE_INSTALLED_NATIVETS "Use find_package(TabulaSonora) instead of an in-tree copy" OFF)

# The submodule first, then a sibling ../NativeTS checkout, so both a fresh recursive clone and a
# working tree that already has the engine next to it build without an argument.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/nativets/CMakeLists.txt")
    set(_tsgui_default_nativets "${CMAKE_CURRENT_SOURCE_DIR}/nativets")
else()
    set(_tsgui_default_nativets "${CMAKE_CURRENT_SOURCE_DIR}/../NativeTS")
endif()

set(TSGUI_NATIVETS_DIR "${_tsgui_default_nativets}" CACHE PATH
    "Path to a NativeTS source tree, when not using an installed one")

if(TSGUI_USE_INSTALLED_NATIVETS)
    find_package(TabulaSonora REQUIRED)
else()
    if(NOT EXISTS "${TSGUI_NATIVETS_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "No NativeTS source tree at '${TSGUI_NATIVETS_DIR}'.\n"
            "Initialise the submodule, pass -DTSGUI_NATIVETS_DIR=/path/to/NativeTS, or configure "
            "with -DTSGUI_USE_INSTALLED_NATIVETS=ON.")
    endif()

    # We want the library and nothing else: no Catch2, no CLI11, no install rules leaking into ours.
    set(TS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(TS_BUILD_CLI OFF CACHE BOOL "" FORCE)
    set(TS_BUILD_PLAYER OFF CACHE BOOL "" FORCE)
    set(TS_BUILD_TUI OFF CACHE BOOL "" FORCE)
    set(TS_BUILD_WEB OFF CACHE BOOL "" FORCE)
    set(TS_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(TS_INSTALL OFF CACHE BOOL "" FORCE)

    # The engine links FLAC, Vorbis and Ogg whenever pkg-config can find them, so that its
    # SoundFont exporter can write compressed sample banks. This player never calls that exporter:
    # those four libraries would be dead weight in the binary and four more runtime dependencies on
    # every package built from it.
    #
    # Upstream has no option to decline them, and they are the *only* thing it uses pkg-config for
    # (src/CMakeLists.txt lines 9-13), so pkg-config is pointed at an empty directory for the length
    # of the add_subdirectory and put back straight afterwards. Our own gtk4 and libadwaita lookups
    # happen further down, with the real search path restored.
    #
    # Restoring means *unsetting* where the variable was unset to begin with: an empty
    # PKG_CONFIG_LIBDIR is not "use the default search path", it is "search nowhere", which would
    # take gtk4 down with the codecs.
    set(_tsgui_had_pkg_libdir FALSE)
    set(_tsgui_had_pkg_path FALSE)
    if(DEFINED ENV{PKG_CONFIG_LIBDIR})
        set(_tsgui_had_pkg_libdir TRUE)
        set(_tsgui_saved_pkg_libdir "$ENV{PKG_CONFIG_LIBDIR}")
    endif()
    if(DEFINED ENV{PKG_CONFIG_PATH})
        set(_tsgui_had_pkg_path TRUE)
        set(_tsgui_saved_pkg_path "$ENV{PKG_CONFIG_PATH}")
    endif()

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/no-pkg-config")
    set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_BINARY_DIR}/no-pkg-config")
    unset(ENV{PKG_CONFIG_PATH})

    add_subdirectory("${TSGUI_NATIVETS_DIR}" "${CMAKE_BINARY_DIR}/nativets" EXCLUDE_FROM_ALL)

    if(_tsgui_had_pkg_libdir)
        set(ENV{PKG_CONFIG_LIBDIR} "${_tsgui_saved_pkg_libdir}")
    else()
        unset(ENV{PKG_CONFIG_LIBDIR})
    endif()
    if(_tsgui_had_pkg_path)
        set(ENV{PKG_CONFIG_PATH} "${_tsgui_saved_pkg_path}")
    else()
        unset(ENV{PKG_CONFIG_PATH})
    endif()
endif()

# Unoptimised, the synth renders only ~1.4x realtime and no ring can absorb that. The Apple package
# force-optimises the engine in debug builds for exactly this reason; do the same, while leaving the
# GUI's own sources at -O0 so they stay debuggable.
option(TSGUI_FAST_DEBUG "Compile the engine at -O2 even in Debug builds" ON)

if(TSGUI_FAST_DEBUG AND TARGET tabulasonora)
    target_compile_options(tabulasonora PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()

# ---------------------------------------------------------------------------------------------
# System dependencies
# ---------------------------------------------------------------------------------------------

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk4>=4.14)
pkg_check_modules(ADW REQUIRED IMPORTED_TARGET libadwaita-1>=1.6)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0>=2.74)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
find_program(GLIB_COMPILE_SCHEMAS glib-compile-schemas REQUIRED)

# miniaudio is a single header; the implementation lives in one translation unit of ours.
#
# PATH_SUFFIXES because distributions disagree about where it goes: vcpkg installs it as
# <prefix>/include/miniaudio.h, while Arch's package puts it in a directory of its own at
# /usr/include/miniaudio/miniaudio.h. Either way what we want is the directory that makes
# `#include <miniaudio.h>` resolve.
find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h PATH_SUFFIXES miniaudio REQUIRED)

include(GNUInstallDirs)
include(cmake/CompileGResource.cmake)
include(cmake/CompileGSettings.cmake)

# ---------------------------------------------------------------------------------------------
# Warnings
#
# Deliberately without -Wold-style-cast: GTK's G_OBJECT/GTK_WIDGET macros are C casts and would
# bury everything else. NativeTS's own ts::warnings is build-interface-only and never reaches us.
# ---------------------------------------------------------------------------------------------

add_library(tsgui_warnings INTERFACE)
target_compile_options(tsgui_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wdouble-promotion
    # Generated GResource code is C, which has no virtual destructors to warn about.
    $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>)

# Absolute source paths otherwise reach the finished binary through __FILE__, which GLib embeds in
# G_OBJECT_WARN_INVALID_PROPERTY_ID and the g_return_if_fail family -- so every GObject property
# getter leaves the build directory's full path in the executable. makepkg reports a package that
# mentions its own $srcdir, and rightly: it is build-host detail nobody downstream should receive.
#
# Placed here rather than at the top of the file so it applies to our own targets and not to the
# engine, which is added above and is upstream's to configure.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=." TSGUI_HAVE_FILE_PREFIX_MAP)
if(TSGUI_HAVE_FILE_PREFIX_MAP)
    add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
endif()

add_subdirectory(src)
add_subdirectory(data)

option(TSGUI_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
if(TSGUI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# NOTICE.md is not optional: reverb and chorus coefficients compiled into the engine are
# Roland-derived, and NativeTS requires the notice to travel with any binary.
install(FILES LICENSE NOTICE.md
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/${TSGUI_BINARY_NAME}")
