// Single translation unit that emits the rgbcx (BC1/BC3/BC4/BC5) implementation.
// bc7enc.c provides the BC7 encoder separately (C). rgbcx.h is header-only and
// must have its implementation defined in exactly one TU.
#include <cstring>   // memset / memcpy
#include <cmath>     // fabs / fabsf
#define RGBCX_IMPLEMENTATION
#include "rgbcx.h"
