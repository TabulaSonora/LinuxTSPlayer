# Compiles a .gresource.xml into a C source file, tracking the files it references so that editing
# a .ui file rebuilds the bundle.
#
#   tsgui_compile_gresource(OUTPUT_VAR xml SOURCE_DIR dir)
#
# Sets OUTPUT_VAR to the generated .c path, which the caller adds to a target's sources.
function(tsgui_compile_gresource output_var xml source_dir)
    # NAME_WLE, not NAME_WE: the file is named for a reverse-DNS app id, and NAME_WE would truncate
    # "co.losno.TabulaSonoraPlayer.gresource.xml" to "co".
    get_filename_component(_xml_name "${xml}" NAME_WLE)
    set(_generated "${CMAKE_CURRENT_BINARY_DIR}/${_xml_name}.c")

    execute_process(
        COMMAND "${GLIB_COMPILE_RESOURCES}" --generate-dependencies
                --sourcedir "${source_dir}" "${xml}"
        WORKING_DIRECTORY "${source_dir}"
        OUTPUT_VARIABLE _deps
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _deps_result)

    if(NOT _deps_result EQUAL 0)
        message(FATAL_ERROR "Could not read dependencies of ${xml}")
    endif()

    # Already absolute, because --sourcedir was given as an absolute path.
    string(REPLACE "\n" ";" _deps "${_deps}")

    add_custom_command(
        OUTPUT "${_generated}"
        # --target and its value as separate arguments: under VERBATIM the --target="..." form would
        # pass the quotes through as part of the filename.
        # --c-name explicitly, because the default is derived from the filename and truncated at the
        # first dot, so every bundle named for a reverse-DNS id would claim the same symbols.
        COMMAND "${GLIB_COMPILE_RESOURCES}"
                --target "${_generated}" --generate-source --c-name tsgui
                --sourcedir "${source_dir}" "${xml}"
        DEPENDS "${xml}" ${_deps}
        COMMENT "Compiling ${_xml_name} resources"
        VERBATIM)

    set(${output_var} "${_generated}" PARENT_SCOPE)
endfunction()
