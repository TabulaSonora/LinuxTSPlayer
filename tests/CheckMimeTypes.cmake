# Checks that the MIME definitions compile and that each one actually recognises its format.
#
# Validating the XML alone would not catch the mistake worth catching: a signature that parses
# perfectly and matches nothing. So the package is installed into a throwaway MIME database, a
# minimal file carrying each format's real magic is written, and the database is asked what it
# thinks each one is.
#
# Expects: MIME_XML, UPDATE_MIME_DATABASE, XDG_MIME, WORK_DIR.

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/mime/packages" "${WORK_DIR}/files")
file(COPY "${MIME_XML}" DESTINATION "${WORK_DIR}/mime/packages")

execute_process(COMMAND "${UPDATE_MIME_DATABASE}" "${WORK_DIR}/mime"
    RESULT_VARIABLE _built ERROR_VARIABLE _build_error OUTPUT_QUIET)
if(NOT _built EQUAL 0)
    message(FATAL_ERROR "The MIME package would not compile:\n${_build_error}")
endif()

# name -> the bytes that identify it, as hex, and the type it must resolve to. The signatures are
# the engine's own (NativeTS src/midi/midi_formats.cpp).
set(_padding "00000000000000000000000000000000")
set(_cases
    "song.rmi|52494646C8000000524D4944666D7420|audio/x-rmid"
    "song.mids|52494646C80000004D494453666D7420|audio/x-mids"
    "song.mus|4D55531A|audio/x-mus"
    "song.xmi|464F524D00000064584449520000000000000000000000000000000000000000584D4944|audio/x-xmi"
    "song.gmf|474D4601|audio/x-gmf"
    "song.hmi|484D492D4D494449534F4E47|audio/x-hmi"
    "song.hmp|484D494D49444950|audio/x-hmp"
    "song.lds|01|audio/x-lds")

set(_failures "")

foreach(_case IN LISTS _cases)
    string(REPLACE "|" ";" _parts "${_case}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _hex)
    list(GET _parts 2 _expected)

    # Padded, because a sniffer will not look at a file shorter than the offsets it must read.
    file(WRITE "${WORK_DIR}/files/${_name}.hex" "${_hex}${_padding}${_padding}${_padding}${_padding}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
                "${CMAKE_COMMAND}" -DIN=${WORK_DIR}/files/${_name}.hex -DOUT=${WORK_DIR}/files/${_name} -P "${CMAKE_CURRENT_LIST_DIR}/HexToBinary.cmake"
        RESULT_VARIABLE _written OUTPUT_QUIET ERROR_QUIET)

    # Pointed at the throwaway database, with the system one still behind it so the definitions are
    # judged in the presence of the ones they have to win against -- text/x-xmi already owns *.xmi.
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
                "XDG_DATA_HOME=${WORK_DIR}"
                "XDG_DATA_DIRS=${WORK_DIR}:/usr/share"
                "${XDG_MIME}" query filetype "${WORK_DIR}/files/${_name}"
        OUTPUT_VARIABLE _actual OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _queried)

    if(NOT _actual STREQUAL _expected)
        list(APPEND _failures "  ${_name}: expected ${_expected}, got '${_actual}'")
    endif()
endforeach()

if(_failures)
    string(REPLACE ";" "\n" _report "${_failures}")
    message(FATAL_ERROR "MIME definitions do not recognise their own formats:\n${_report}")
endif()

message(STATUS "All MIME definitions recognise their format")
