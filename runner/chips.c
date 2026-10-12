/* Unmodified chips implementations live in one C translation unit. */
#define CHIPS_IMPL
#define mem_init chips_mem_init
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "chips/chips_common.h"
#include "chips/m6502.h"
#include "chips/m6526.h"
#include "chips/m6569.h"
#include "chips/m6581.h"
#include "chips/kbd.h"
#include "chips/mem.h"
#include "chips/clk.h"
#include "systems/c1530.h"
#include "chips/m6522.h"
#include "systems/c1541.h"
#include "systems/c64.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#undef mem_init
