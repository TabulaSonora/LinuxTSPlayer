# Translations.
#
# Two kinds of thing live here and they run at different times. Compiling a catalogue into a .mo is
# part of the build, because the result is installed. Regenerating the template and merging it back
# into the catalogues is not: it rewrites POT-Creation-Date on every invocation, so putting it in
# ALL would leave `git status` dirty after a no-op build. Those are the `update-pot` and `update-po`
# targets, run by hand when strings change.

set(TSGUI_PO_DIR "${CMAKE_CURRENT_SOURCE_DIR}" PARENT_SCOPE)

set(_pot "${CMAKE_CURRENT_SOURCE_DIR}/${TSGUI_GETTEXT_PACKAGE}.pot")

file(STRINGS LINGUAS _linguas REGEX "^[^#]")

set(_po_files "")
foreach(_lang ${_linguas})
    list(APPEND _po_files "${CMAKE_CURRENT_SOURCE_DIR}/${_lang}.po")
endforeach()
set(TSGUI_PO_FILES "${_po_files}" PARENT_SCOPE)

# ---------------------------------------------------------------------------------------------
# Maintainer targets
# ---------------------------------------------------------------------------------------------

# The keyword list is load-bearing. xgettext's defaults recognise almost nothing of what this code
# actually uses -- of _, N_, C_, NC_, Q_, g_dgettext and g_dngettext, the stock list catches none --
# so anything missing from here is silently absent from the template rather than reported.
#
# Q_ needs the :1g form, which tells xgettext the context is separated by a '|' inside one literal;
# plain --keyword=Q_ would extract "context|string" whole. TS_ and TS_N_ are the host layer's, which
# cannot include <glib/gi18n.h> and so cannot use the glib spellings.
set(_xgettext_keywords
    --keyword=_ --keyword=N_
    --keyword=C_:1c,2 --keyword=NC_:1c,2 --keyword=Q_:1g
    --keyword=g_dgettext:2 --keyword=g_dngettext:2,3 --keyword=g_dpgettext2:2c,3
    --keyword=TS_ --keyword=TS_N_)

if(XGETTEXT)
    add_custom_target(update-pot
        COMMAND "${XGETTEXT}"
            --files-from "${CMAKE_CURRENT_SOURCE_DIR}/POTFILES.in"
            --directory "${CMAKE_SOURCE_DIR}"
            --from-code=UTF-8
            --add-comments=TRANSLATORS
            --package-name "Tabula Sonora Player"
            --package-version "${PROJECT_VERSION}"
            --msgid-bugs-address "https://github.com/TabulaSonora/TSGui/issues"
            ${_xgettext_keywords}
            --output "${_pot}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Regenerating ${TSGUI_GETTEXT_PACKAGE}.pot"
        VERBATIM)
endif()

if(MSGMERGE)
    set(_merge_commands "")
    foreach(_lang ${_linguas})
        list(APPEND _merge_commands
            COMMAND "${MSGMERGE}" --quiet --backup=none --update
                    "${CMAKE_CURRENT_SOURCE_DIR}/${_lang}.po" "${_pot}")
    endforeach()

    add_custom_target(update-po ${_merge_commands}
        COMMENT "Merging ${TSGUI_GETTEXT_PACKAGE}.pot into the catalogues"
        VERBATIM)

    if(TARGET update-pot)
        add_dependencies(update-po update-pot)
    endif()
endif()

# ---------------------------------------------------------------------------------------------
# The catalogues themselves, which are part of the build
# ---------------------------------------------------------------------------------------------

if(NOT TSGUI_ENABLE_NLS)
    return()
endif()

set(_mo_files "")

foreach(_lang ${_linguas})
    # Laid out as a real locale tree rather than a flat directory of .mo files, so that an
    # uninstalled run can point straight at it:
    #   TS_LOCALE_DIR=build/dev/po/locale ./build/dev/src/tabula-sonora-player
    set(_mo_dir "${CMAKE_CURRENT_BINARY_DIR}/locale/${_lang}/LC_MESSAGES")
    set(_mo "${_mo_dir}/${TSGUI_GETTEXT_PACKAGE}.mo")

    # --check-format rather than -c: -c also implies --check-header, which rejects a catalogue whose
    # header is still marked fuzzy -- the state msginit leaves a new language in, and not a reason to
    # fail the build.
    add_custom_command(OUTPUT "${_mo}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mo_dir}"
        COMMAND "${MSGFMT}" --check-format --output-file "${_mo}"
                "${CMAKE_CURRENT_SOURCE_DIR}/${_lang}.po"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_lang}.po"
        COMMENT "Compiling ${_lang}.mo"
        VERBATIM)

    list(APPEND _mo_files "${_mo}")

    install(FILES "${_mo}" DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/${_lang}/LC_MESSAGES")
endforeach()

add_custom_target(tsgui-locale ALL DEPENDS ${_mo_files})
