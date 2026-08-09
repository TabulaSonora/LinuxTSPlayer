#pragma once

// Translation for the host layer.
//
// There is no GTK below this line and no glib either, so <glib/gi18n.h> is unavailable and the
// familiar _() spelling with it. The names here are deliberately different rather than a
// reimplementation of the same ones: an application translation unit that includes both this header
// and <glib/gi18n.h> would otherwise redefine _, N_, C_, NC_ and Q_ on top of glib's.
//
// The domain is named rather than left to the process default. A library should not depend on
// whoever links it having called textdomain(), and the practical consequence is that a test binary
// linking tsgui_host alone still resolves these strings once it has called bindtextdomain().
//
// Nothing needs linking for this. dgettext lives in glibc; the top-level CMakeLists explains why
// there is no find_package(Intl) to go with it.

#include <libintl.h>

#include <cstdio>
#include <string>

/// Look the string up now. GETTEXT_PACKAGE comes from tsgui_host's PUBLIC compile definitions.
#define TS_(String) dgettext(GETTEXT_PACKAGE, String)

/// Mark the string for extraction without looking it up, for initialisers and static tables where
/// the lookup has to happen later at the point of use.
#define TS_N_(String) (String)

namespace ts::host {

/// printf into a std::string.
///
/// Here rather than in either caller because both files that translate need it. std::snprintf and
/// not g_strdup_printf for the reason this header exists at all -- there is no glib below this
/// line -- and not std::format either: a translated format string only exists at run time, and
/// std::format wants its own at compile time.
///
/// Sized by asking snprintf what it needs before writing, so a translation longer than the English
/// original is never truncated.
template <typename... Args>
std::string format_text(const char* format, Args... args)
{
    const int needed = std::snprintf(nullptr, 0, format, args...);
    if (needed <= 0) {
        return {};
    }
    std::string text(static_cast<std::size_t>(needed), '\0');
    std::snprintf(text.data(), static_cast<std::size_t>(needed) + 1, format, args...);
    return text;
}

} // namespace ts::host
