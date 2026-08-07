#pragma once

#include <adwaita.h>

/// The first-run screen: what the engine needs, and a button to go and find it.
///
/// Spells the pinned file's identity out rather than just saying "wrong file", because there are
/// several builds of SCCore.dll in circulation and only one of them has the tables at the offsets
/// the engine reads. The identity is read from the compiled-in manifest, never hard-coded here.
#define TS_TYPE_ROM_SETUP (ts_rom_setup_get_type())
G_DECLARE_FINAL_TYPE(TsRomSetup, ts_rom_setup, TS, ROM_SETUP, GtkWidget)

GtkWidget* ts_rom_setup_new(void);

/// Swaps the button for a spinner while the 27 MB is hashed.
void ts_rom_setup_set_verifying(TsRomSetup* self, gboolean verifying);
