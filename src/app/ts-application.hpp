#pragma once

#include <adwaita.h>

// No G_BEGIN_DECLS anywhere under app/: this is a C++ program throughout, and several of these
// headers carry C++ types across (spans, references to engine structs) which an extern "C" block
// cannot describe. Nothing here is ever included from C.

#define TS_TYPE_APPLICATION (ts_application_get_type())
G_DECLARE_FINAL_TYPE(TsApplication, ts_application, TS, APPLICATION, AdwApplication)

TsApplication* ts_application_new(void);
