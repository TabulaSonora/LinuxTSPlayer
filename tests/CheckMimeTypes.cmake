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

# The system's own definitions are copied in beside ours, and the queries below then run against
# this database *alone* -- XDG_DATA_DIRS is pointed here and nowhere else.
#
# Not paranoia. Once the application is installed, by package or by flatpak, its MIME package is on
# the host, and a check that let the real database through would be reading the installed copy
# rather than the one in the working tree: it passed happily with the file it was testing deleted.
# Copying freedesktop.org.xml keeps the conflict this is really about -- text/x-xmi already claims
# *.xmi -- without letting anything else in.
file(COPY "${BASE_MIME}" DESTINATION "${WORK_DIR}/mime/packages")

execute_process(COMMAND "${UPDATE_MIME_DATABASE}" "${WORK_DIR}/mime"
    RESULT_VARIABLE _built ERROR_VARIABLE _build_error OUTPUT_QUIET)
if(NOT _built EQUAL 0)
    message(FATAL_ERROR "The MIME package would not compile:\n${_build_error}")
endif()

# Exit status is not enough. update-mime-database reports a malformed package on stderr, skips it
# entirely, and still exits 0 -- so a stray "--" inside an XML comment silently ships a package that
# defines nothing. The type checks below would catch it too, but not say why.
if(_build_error MATCHES "parser error|error :")
    message(FATAL_ERROR "The MIME package is malformed and was skipped:\n${_build_error}")
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
                "XDG_DATA_DIRS=${WORK_DIR}"
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

# The same question again with every magic rule removed, which is the situation inside a flatpak:
# it strips magic from an exported MIME package rather than let a sandboxed application teach the
# host to sniff file contents. Extensions are all that survive there, and a type whose glob ties
# with one already in shared-mime-info loses arbitrarily -- *.xmi is claimed by text/x-xmi.
file(REMOVE_RECURSE "${WORK_DIR}/globs")
file(MAKE_DIRECTORY "${WORK_DIR}/globs/mime/packages")

# A line range through sed rather than a regex over the whole file: CMake's regex engine is greedy
# with no way to ask otherwise, so `<magic.*</magic>` matches from the first magic block to the last
# and takes every mime-type between them with it.
execute_process(
    COMMAND sed -e "/<magic/,/<\\/magic>/d" "${MIME_XML}"
    OUTPUT_FILE "${WORK_DIR}/globs/mime/packages/globs-only.xml"
    RESULT_VARIABLE _stripped)

if(NOT _stripped EQUAL 0)
    message(FATAL_ERROR "Could not strip magic rules for the extension-only check")
endif()

# The base package goes in whole: flatpak strips magic from *our* export, not from the host's own
# definitions, so text/x-xmi keeps everything it has and our glob has to out-weigh it on merit.
file(COPY "${BASE_MIME}" DESTINATION "${WORK_DIR}/globs/mime/packages")

# The strip has to leave the file intact, or this checks nothing at all -- which is exactly the trap
# the greedy version fell into: it removed the glob it was meant to be testing and still passed.
file(STRINGS "${WORK_DIR}/globs/mime/packages/globs-only.xml" _kept REGEX "<mime-type")
list(LENGTH _kept _kept_count)
file(STRINGS "${MIME_XML}" _all REGEX "<mime-type")
list(LENGTH _all _all_count)

if(NOT _kept_count EQUAL _all_count)
    message(FATAL_ERROR
        "Stripping magic also removed mime-type definitions: ${_kept_count} of ${_all_count} left.")
endif()

execute_process(COMMAND "${UPDATE_MIME_DATABASE}" "${WORK_DIR}/globs/mime"
    RESULT_VARIABLE _globs_built ERROR_VARIABLE _globs_error OUTPUT_QUIET)

if(NOT _globs_built EQUAL 0)
    message(FATAL_ERROR "The magic-free MIME package would not compile:\n${_globs_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "XDG_DATA_HOME=${WORK_DIR}/globs"
            "XDG_DATA_DIRS=${WORK_DIR}/globs"
            "${XDG_MIME}" query filetype "${WORK_DIR}/files/song.xmi"
    OUTPUT_VARIABLE _xmi_by_glob OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

if(NOT _xmi_by_glob STREQUAL "audio/x-xmi")
    message(FATAL_ERROR
        "With magic stripped -- as flatpak does -- *.xmi resolves to '${_xmi_by_glob}' rather than "
        "audio/x-xmi. The glob needs a weight above the one shared-mime-info already gives it.")
endif()

message(STATUS "All MIME definitions recognise their format, by magic and by extension alone")
