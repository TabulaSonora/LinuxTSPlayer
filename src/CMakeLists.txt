# ---------------------------------------------------------------------------------------------
# miniaudio, compiled alone and without our warnings
#
# It is a vendored single-header library: its warnings are not this project's to fix, and silencing
# them target-wide would silence them for our own code too. Same arrangement NativeTS uses in
# apps/audio.
# ---------------------------------------------------------------------------------------------

add_library(tsgui_miniaudio STATIC host/ts_miniaudio.cpp)
target_include_directories(tsgui_miniaudio SYSTEM PUBLIC "${MINIAUDIO_INCLUDE_DIR}")
target_compile_options(tsgui_miniaudio PRIVATE -w)
target_link_libraries(tsgui_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)

# ---------------------------------------------------------------------------------------------
# The engine host layer: session, render thread, device, MIDI input. No GTK below this line, so it
# can be driven headless by the tests.
# ---------------------------------------------------------------------------------------------

add_library(tsgui_host STATIC
    host/ts_session.cpp
    host/ts_song_info.cpp
    host/ts_player.cpp
    host/ts_audio_device.cpp
    host/ts_midi_input.cpp)

target_include_directories(tsgui_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# The host layer translates its own user-visible prose, which the application shows in dialogs and
# in the song information. PUBLIC because host/ts_i18n.hpp expands the macro in a header, so every
# consumer -- the executable and the test binaries -- has to agree on it. Nothing is linked for
# this: on glibc dgettext is in libc, and this application targets nothing else.
target_compile_definitions(tsgui_host PUBLIC GETTEXT_PACKAGE="${TSGUI_GETTEXT_PACKAGE}")

target_link_libraries(tsgui_host
    PUBLIC
        ts::tabulasonora
        tsgui_miniaudio
        PkgConfig::ALSA
        Threads::Threads
    PRIVATE
        tsgui_warnings)

# ---------------------------------------------------------------------------------------------
# The application
# ---------------------------------------------------------------------------------------------

tsgui_compile_gresource(TSGUI_RESOURCES
    "${CMAKE_SOURCE_DIR}/data/${TSGUI_APP_ID}.gresource.xml"
    "${CMAKE_SOURCE_DIR}/data")

add_executable(${TSGUI_BINARY_NAME}
    app/main.cpp
    app/ts-application.cpp
    app/ts-window.cpp
    app/ts-part-object.cpp
    app/ts-player-model.cpp
    app/ts-voice-meter.cpp
    app/ts-part-row.cpp
    app/ts-mixer.cpp
    app/ts-transport.cpp
    app/ts-rom-setup.cpp
    app/ts-prefs-dialog.cpp
    app/ts-song-info-window.cpp
    app/ts-settings.cpp
    app/ts-mpris.cpp
    "${TSGUI_RESOURCES}")

# Generated code, and not ours to keep tidy: embedding a PNG produces a string literal far longer
# than C99 requires a compiler to accept, which -Wpedantic duly reports.
set_source_files_properties("${TSGUI_RESOURCES}" PROPERTIES COMPILE_OPTIONS "-w")

target_compile_definitions(${TSGUI_BINARY_NAME} PRIVATE
    TSGUI_APP_ID="${TSGUI_APP_ID}"
    TSGUI_VERSION="${PROJECT_VERSION}"
    # Always defined, empty when the engine's commit could not be read, so the dialog tests a string
    # rather than the build testing a macro. See the block that sets it in the top-level CMakeLists.
    TSGUI_NATIVETS_COMMIT="${TSGUI_NATIVETS_COMMIT}"
    # Where the installed catalogues are. An uninstalled run overrides it with TS_LOCALE_DIR, the
    # same way it already has to override the schema directory with GSETTINGS_SCHEMA_DIR.
    TSGUI_LOCALE_DIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")

target_link_libraries(${TSGUI_BINARY_NAME}
    PRIVATE
        tsgui_host
        tsgui_warnings
        PkgConfig::GTK
        PkgConfig::ADW
        PkgConfig::GIO)

install(TARGETS ${TSGUI_BINARY_NAME} RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
