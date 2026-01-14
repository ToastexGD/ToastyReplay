// Wrapper header to fix FMOD redefinition issues in GeodeBindings
// This is force-included before any other headers to ensure fmod.h is included first
#pragma once

// Include fmod.h first to get correct declarations with F_API
#include <Geode/fmod/fmod.h>

// Note: Standalones.hpp will redeclare FMOD functions without F_API
// This causes C2373 errors which cannot be suppressed via pragma in MSVC
// The force-include ensures fmod.h's correct declarations are available first
