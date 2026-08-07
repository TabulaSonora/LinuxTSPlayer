tsgui_compile_gsettings(tsgui-gsettings "${CMAKE_CURRENT_SOURCE_DIR}/${TSGUI_APP_ID}.gschema.xml")

configure_file("${TSGUI_APP_ID}.desktop.in" "${TSGUI_APP_ID}.desktop" @ONLY)
configure_file("${TSGUI_APP_ID}.metainfo.xml.in" "${TSGUI_APP_ID}.metainfo.xml" @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${TSGUI_APP_ID}.desktop"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${TSGUI_APP_ID}.metainfo.xml"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo")

# The legacy container formats, which shared-mime-info does not know. Without these the desktop
# entry's associations for them are unreachable: nothing can recognise the files in the first place.
install(FILES "${TSGUI_APP_ID}.mime.xml"
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
