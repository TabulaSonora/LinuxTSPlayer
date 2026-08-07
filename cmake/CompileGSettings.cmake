# Installs a GSettings schema, and compiles it into the build tree as well so that a development
# run works without installing:
#
#   GSETTINGS_SCHEMA_DIR=build/data ./build/src/tabula-sonora-player
#
#   tsgui_compile_gsettings(TARGET_NAME schema.gschema.xml)
function(tsgui_compile_gsettings target_name schema)
    get_filename_component(_schema_path "${schema}" ABSOLUTE)
    get_filename_component(_schema_name "${schema}" NAME)

    set(_staged "${CMAKE_CURRENT_BINARY_DIR}/${_schema_name}")
    set(_compiled "${CMAKE_CURRENT_BINARY_DIR}/gschemas.compiled")

    add_custom_command(
        OUTPUT "${_compiled}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_schema_path}" "${_staged}"
        COMMAND "${GLIB_COMPILE_SCHEMAS}" "${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS "${_schema_path}"
        COMMENT "Compiling ${_schema_name} for the build tree"
        VERBATIM)

    add_custom_target(${target_name} ALL DEPENDS "${_compiled}")

    install(FILES "${_schema_path}"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/glib-2.0/schemas")

    # Recompile the system-wide cache on install, unless a packager is staging into a DESTDIR.
    install(CODE "
        if(NOT DEFINED ENV{DESTDIR})
            execute_process(COMMAND \"${GLIB_COMPILE_SCHEMAS}\"
                \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATAROOTDIR}/glib-2.0/schemas\")
        endif()")
endfunction()
