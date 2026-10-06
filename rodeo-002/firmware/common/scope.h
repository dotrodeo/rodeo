// rodeo-002 — the platform's scope, under the name this instrument uses.
//
// Moved to rodeo-sdk/common/rodeo_scope.h on 2026-10-01. See
// dsp_util.h for why the bridge exists rather than a rename at every call site.

#pragma once

#include "rodeo_scope.h"

namespace rodeo002
{
using rodeo::Scope;
} // namespace rodeo002
