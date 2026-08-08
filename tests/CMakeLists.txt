# The engine is inert without a licensed SCCore.dll, so every test here is conditional on one being
# available. A checkout with no ROM configures and builds; it just has nothing to assert against.
set(TSGUI_TEST_ROM "$ENV{TS_SCCORE_DLL}" CACHE FILEPATH
    "SCCore.dll to run the engine tests against; defaults to $TS_SCCORE_DLL")

set(TSGUI_TEST_MIDI "" CACHE FILEPATH "A MIDI file to render in the engine tests")

add_executable(ts-export-tool ts_export_tool.cpp)
target_link_libraries(ts-export-tool PRIVATE tsgui_host tsgui_warnings)

# The CLI comes from the engine tree this program is linked against, not from whatever happens to be
# on PATH or left over in a sibling checkout's build directory. Comparing our render against a
# *different* build of the engine would fail whenever upstream changed the voice, which says nothing
# about whether this program renders it faithfully -- the only question the test is asking.
#
# The engine is added with EXCLUDE_FROM_ALL, so the target has to be pulled in explicitly.
if(TARGET tabula-sonora)
    add_dependencies(ts-export-tool tabula-sonora)
endif()

add_executable(ts-host-smoke ts_host_smoke.cpp)
target_link_libraries(ts-host-smoke PRIVATE tsgui_host tsgui_warnings)

# --- Metadata, which needs no ROM ---------------------------------------------------------------

# What a file says about itself, over files this test assembles byte by byte. No ROM and no corpus:
# the cases worth pinning are the malformed and the unusual, which is exactly what nobody can be
# relied on to have a copy of.
add_executable(ts-song-info-test ts_song_info_test.cpp)
target_link_libraries(ts-song-info-test PRIVATE tsgui_host tsgui_warnings)
add_test(NAME song-info COMMAND ts-song-info-test)

find_program(DESKTOP_FILE_VALIDATE desktop-file-validate)
if(DESKTOP_FILE_VALIDATE)
    add_test(NAME desktop-file
        COMMAND "${DESKTOP_FILE_VALIDATE}"
                "${CMAKE_BINARY_DIR}/data/${TSGUI_APP_ID}.desktop")
endif()

find_program(XDG_MIME xdg-mime)

# The system's own MIME definitions, copied into the throwaway database so the check runs against a
# known set rather than against whatever this machine happens to have installed -- including, once
# the application is installed anywhere, its own definitions.
find_file(FREEDESKTOP_MIME freedesktop.org.xml
    PATHS /usr/share/mime/packages /usr/local/share/mime/packages
    NO_DEFAULT_PATH)

if(UPDATE_MIME_DATABASE AND XDG_MIME AND FREEDESKTOP_MIME)
    add_test(NAME mime-types
        COMMAND "${CMAKE_COMMAND}"
                -DMIME_XML=${CMAKE_SOURCE_DIR}/data/${TSGUI_APP_ID}.mime.xml
                -DUPDATE_MIME_DATABASE=${UPDATE_MIME_DATABASE}
                -DXDG_MIME=${XDG_MIME}
                -DBASE_MIME=${FREEDESKTOP_MIME}
                -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/mime
                -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckMimeTypes.cmake")
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

if(TARGET tabula-sonora)
    add_test(NAME export-matches-cli
        COMMAND "${CMAKE_COMMAND}"
                -DTOOL=$<TARGET_FILE:ts-export-tool>
                -DCLI=$<TARGET_FILE:tabula-sonora>
                -DROM=${TSGUI_TEST_ROM}
                -DMIDI=${TSGUI_TEST_MIDI}
                -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/export
                -P "${CMAKE_CURRENT_SOURCE_DIR}/CompareExport.cmake")
    set_tests_properties(export-matches-cli PROPERTIES TIMEOUT 600)
else()
    message(WARNING
        "The engine's CLI target is absent, so the export byte-comparison -- the test that proves "
        "this program renders the engine faithfully -- will not run.")
endif()
