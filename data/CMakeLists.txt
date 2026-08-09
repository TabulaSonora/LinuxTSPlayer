tsgui_compile_gsettings(tsgui-gsettings "${CMAKE_CURRENT_SOURCE_DIR}/${TSGUI_APP_ID}.gschema.xml")

# Two stages, because two different tools each want to own the file: configure_file substitutes
# @VARS@ at configure time into gen/, then msgfmt merges the catalogues into that result at build
# time and writes the finished artefact into merged/.
#
# The order only works one way round. msgfmt would pass @TSGUI_BINARY_NAME@ through untouched, and
# configure_file cannot consume something that does not exist until the build.
#
# Each stage gets a directory of its own, and neither is the directory the sources are in. That is
# not tidiness: flatpak-builder configures cmake-ninja modules *in the source directory*, so
# CMAKE_CURRENT_BINARY_DIR and CMAKE_CURRENT_SOURCE_DIR are the same path there, and an output
# written beside the sources lands on a file that is itself an input. The desktop entry and the
# metainfo are saved by their names differing by `.in`; `.mime.xml` has no `.in` to distinguish it,
# so msgfmt took the source as its template and overwrote it with the merged result. The next build
# then merged translations into a file that already had them, and the one after that again, until
# libxml2 gave up mid-build on a nodeset limit. Two subdirectories make that impossible to
# reintroduce.
#
# It is load-bearing that no @VAR@ ever appears inside a translatable field. The msgids come from
# the source files while the template msgfmt reads is the configured copy, so the two only agree as
# long as substitution never touches translated text. Today the placeholders are confined to Exec=,
# Icon=, <id>, <launchable>, <binary> and <release version>. Putting one into Comment= or <summary>
# would not fail the build -- it would silently stop that string from being translated.
set(_gen "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(_out "${CMAKE_CURRENT_BINARY_DIR}/merged")

configure_file("${TSGUI_APP_ID}.desktop.in" "gen/${TSGUI_APP_ID}.desktop" @ONLY)
configure_file("${TSGUI_APP_ID}.metainfo.xml.in" "gen/${TSGUI_APP_ID}.metainfo.xml" @ONLY)

# The MIME definitions carry no placeholders, so their first stage is a copy rather than a
# substitution. It still has to happen, so that the template msgfmt reads is never the source file.
configure_file("${TSGUI_APP_ID}.mime.xml" "gen/${TSGUI_APP_ID}.mime.xml" COPYONLY)

set(_desktop "${_out}/${TSGUI_APP_ID}.desktop")
set(_metainfo "${_out}/${TSGUI_APP_ID}.metainfo.xml")
set(_mime "${_out}/${TSGUI_APP_ID}.mime.xml")

if(TSGUI_ENABLE_NLS)
    # --desktop and --xml pick the keys and elements to translate from the template's own name;
    # neither needs to be told which ITS rules apply.
    add_custom_command(OUTPUT "${_desktop}"
        COMMAND "${MSGFMT}" --desktop --template "${_gen}/${TSGUI_APP_ID}.desktop"
                -d "${TSGUI_PO_DIR}" -o "${_desktop}"
        DEPENDS "${_gen}/${TSGUI_APP_ID}.desktop" ${TSGUI_PO_FILES} "${TSGUI_PO_DIR}/LINGUAS"
        COMMENT "Merging translations into ${TSGUI_APP_ID}.desktop"
        VERBATIM)

    add_custom_command(OUTPUT "${_metainfo}"
        COMMAND "${MSGFMT}" --xml --template "${_gen}/${TSGUI_APP_ID}.metainfo.xml"
                -d "${TSGUI_PO_DIR}" -o "${_metainfo}"
        DEPENDS "${_gen}/${TSGUI_APP_ID}.metainfo.xml" ${TSGUI_PO_FILES} "${TSGUI_PO_DIR}/LINGUAS"
        COMMENT "Merging translations into ${TSGUI_APP_ID}.metainfo.xml"
        VERBATIM)

    add_custom_command(OUTPUT "${_mime}"
        COMMAND "${MSGFMT}" --xml --template "${_gen}/${TSGUI_APP_ID}.mime.xml"
                -d "${TSGUI_PO_DIR}" -o "${_mime}"
        DEPENDS "${_gen}/${TSGUI_APP_ID}.mime.xml" ${TSGUI_PO_FILES} "${TSGUI_PO_DIR}/LINGUAS"
        COMMENT "Merging translations into ${TSGUI_APP_ID}.mime.xml"
        VERBATIM)
else()
    # Same outputs in the same places, so nothing downstream has to know whether translations were
    # built. Only the merge step is skipped.
    add_custom_command(OUTPUT "${_desktop}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_gen}/${TSGUI_APP_ID}.desktop" "${_desktop}"
        DEPENDS "${_gen}/${TSGUI_APP_ID}.desktop"
        VERBATIM)

    add_custom_command(OUTPUT "${_metainfo}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_gen}/${TSGUI_APP_ID}.metainfo.xml" "${_metainfo}"
        DEPENDS "${_gen}/${TSGUI_APP_ID}.metainfo.xml"
        VERBATIM)

    add_custom_command(OUTPUT "${_mime}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_gen}/${TSGUI_APP_ID}.mime.xml" "${_mime}"
        DEPENDS "${_gen}/${TSGUI_APP_ID}.mime.xml"
        VERBATIM)
endif()

add_custom_target(tsgui-metadata ALL DEPENDS "${_desktop}" "${_metainfo}" "${_mime}")

install(FILES "${_desktop}"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")

install(FILES "${_metainfo}"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo")

# The legacy container formats, which shared-mime-info does not know. Without these the desktop
# entry's associations for them are unreachable: nothing can recognise the files in the first place.
install(FILES "${_mime}"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/mime/packages")

# Rebuilding the MIME cache is what makes the definitions take effect. Skipped under DESTDIR, where
# a packager is staging and the distribution's own hook will do it at install time.
find_program(UPDATE_MIME_DATABASE update-mime-database)
if(UPDATE_MIME_DATABASE)
    install(CODE "
        if(NOT DEFINED ENV{DESTDIR})
            execute_process(COMMAND \"${UPDATE_MIME_DATABASE}\"
                \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATAROOTDIR}/mime\")
        endif()")
endif()

# The launcher icon is derived from the Icon Composer master the Apple build uses, so it is raster
# at fixed sizes rather than scalable. See tools/build-icons.sh for how the set is regenerated.
foreach(_size 16 24 32 48 64 128 256 512)
    install(FILES "icons/hicolor/${_size}x${_size}/apps/${TSGUI_APP_ID}.png"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${_size}x${_size}/apps")
endforeach()

install(FILES "icons/hicolor/symbolic/apps/${TSGUI_APP_ID}-symbolic.svg"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/symbolic/apps")
