// The single translation unit that compiles miniaudio itself.
//
// Kept alone in its own file so the GUI's warnings never mix with miniaudio's, and so tsgui_warnings
// can be left off this target without weakening it anywhere else. The defines mirror NativeTS's
// apps/audio/miniaudio.cpp: nothing here decodes or encodes, because the engine produces the samples
// and ts::wav writes the files.

#define MINIAUDIO_IMPLEMENTATION

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE

#include <miniaudio.h>
