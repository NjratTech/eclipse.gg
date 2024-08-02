#pragma once

// Security
#include "lazy_importer.h"
#include "xorstr.h"

// Hooking
#include "MinHook/MinHook.h"

// Utils for cheat base
#include "tinyformat.h"

#ifdef DEBUG
#define WINCALL(func) func
#else
#define WINCALL(func) LI_FN(func).cached()
#endif