tsgui_compile_gsettings(tsgui-gsettings "${CMAKE_CURRENT_SOURCE_DIR}/${TSGUI_APP_ID}.gschema.xml")

configure_file("${TSGUI_APP_ID}.desktop.in" "${TSGUI_APP_ID}.desktop" @ONLY)
configure_file("${TSGUI_APP_ID}.metainfo.xml.in" "${TSGUI_APP_ID}.metainfo.xml" @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${TSGUI_APP_ID}.desktop"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${TSGUI_APP_ID}.metainfo.xml"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo")

# The launcher icon is derived from the Icon Composer master the Apple build uses, so it is raster
# at fixed sizes rather than scalable. See tools/build-icons.sh for how the set is regenerated.
foreach(_size 16 24 32 48 64 128 256 512)
    install(FILES "icons/hicolor/${_size}x${_size}/apps/${TSGUI_APP_ID}.png"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${_size}x${_size}/apps")
endforeach()

install(FILES "icons/hicolor/symbolic/apps/${TSGUI_APP_ID}-symbolic.svg"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/symbolic/apps")
