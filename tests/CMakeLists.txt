# The engine is inert without a licensed SCCore.dll, so every test here is conditional on one being
# available. A checkout with no ROM configures and builds; it just has nothing to assert against.
set(TSGUI_TEST_ROM "$ENV{TS_SCCORE_DLL}" CACHE FILEPATH
    "SCCore.dll to run the engine tests against; defaults to $TS_SCCORE_DLL")

set(TSGUI_TEST_MIDI "" CACHE FILEPATH "A MIDI file to render in the engine tests")

find_program(TSGUI_TABULA_SONORA_CLI tabula-sonora
    HINTS "${TSGUI_NATIVETS_DIR}/build/player/apps/cli"
          "${TSGUI_NATIVETS_DIR}/build/release/apps/cli"
    DOC "tabula-sonora CLI to compare exports against")

add_executable(ts-export-tool ts_export_tool.cpp)
target_link_libraries(ts-export-tool PRIVATE tsgui_host tsgui_warnings)

add_executable(ts-host-smoke ts_host_smoke.cpp)
target_link_libraries(ts-host-smoke PRIVATE tsgui_host tsgui_warnings)

# --- Metadata, which needs no ROM ---------------------------------------------------------------

find_program(DESKTOP_FILE_VALIDATE desktop-file-validate)
if(DESKTOP_FILE_VALIDATE)
    add_test(NAME desktop-file
        COMMAND "${DESKTOP_FILE_VALIDATE}"
                "${CMAKE_BINARY_DIR}/data/${TSGUI_APP_ID}.desktop")
endif()

find_program(APPSTREAM_UTIL appstream-util)
if(APPSTREAM_UTIL)
    add_test(NAME metainfo
        COMMAND "${APPSTREAM_UTIL}" validate-relax
                "${CMAKE_BINARY_DIR}/data/${TSGUI_APP_ID}.metainfo.xml")
endif()

# --- The engine, which does ----------------------------------------------------------------------

if(NOT TSGUI_TEST_ROM OR NOT EXISTS "${TSGUI_TEST_ROM}")
    message(STATUS "No SCCore.dll configured; engine tests will not be registered. "
                   "Set TS_SCCORE_DLL or -DTSGUI_TEST_ROM=...")
    return()
endif()

if(NOT TSGUI_TEST_MIDI OR NOT EXISTS "${TSGUI_TEST_MIDI}")
    message(STATUS "No test MIDI configured; engine tests will not be registered. "
                   "Set -DTSGUI_TEST_MIDI=...")
    return()
endif()

# Producer and consumer on two threads across the ring, which is what makes this the port's TSan
# test as well as its does-it-make-sound test.
add_test(NAME host-smoke
    COMMAND ts-host-smoke "${TSGUI_TEST_ROM}" "${TSGUI_TEST_MIDI}")
set_tests_properties(host-smoke PROPERTIES LABELS "ring" TIMEOUT 120)

if(TSGUI_TABULA_SONORA_CLI)
    add_test(NAME export-matches-cli
        COMMAND "${CMAKE_COMMAND}"
                -DTOOL=$<TARGET_FILE:ts-export-tool>
                -DCLI=${TSGUI_TABULA_SONORA_CLI}
                -DROM=${TSGUI_TEST_ROM}
                -DMIDI=${TSGUI_TEST_MIDI}
                -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/export
                -P "${CMAKE_CURRENT_SOURCE_DIR}/CompareExport.cmake")
    set_tests_properties(export-matches-cli PROPERTIES TIMEOUT 600)
else()
    message(STATUS "No tabula-sonora CLI found; the export byte-comparison will not be registered.")
endif()
