// The single translation unit that compiles the miniaudio implementation.
// Consumers include "miniaudio.h" for DECLARATIONS only (they must NOT define
// MINIAUDIO_IMPLEMENTATION), and link the `miniaudio` static lib built here.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
