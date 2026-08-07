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
    host/ts_player.cpp
    host/ts_audio_device.cpp
    host/ts_midi_input.cpp)

target_include_directories(tsgui_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

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
    app/ts-settings.cpp
    app/ts-mpris.cpp
    "${TSGUI_RESOURCES}")

# Generated code, and not ours to keep tidy: embedding a PNG produces a string literal far longer
# than C99 requires a compiler to accept, which -Wpedantic duly reports.
set_source_files_properties("${TSGUI_RESOURCES}" PROPERTIES COMPILE_OPTIONS "-w")

target_compile_definitions(${TSGUI_BINARY_NAME} PRIVATE
    TSGUI_APP_ID="${TSGUI_APP_ID}"
    TSGUI_VERSION="${PROJECT_VERSION}")

target_link_libraries(${TSGUI_BINARY_NAME}
    PRIVATE
        tsgui_host
        tsgui_warnings
        PkgConfig::GTK
        PkgConfig::ADW
        PkgConfig::GIO)

install(TARGETS ${TSGUI_BINARY_NAME} RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
