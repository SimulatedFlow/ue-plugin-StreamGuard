// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/**
 * Everything StreamGuard says.
 *
 * A diagnostic that fails quietly is worse than one that is not installed, because you go on believing the
 * empty board. Every reason StreamGuard has for showing you nothing - a world with no streaming levels, a
 * level it could not find by package name, an unreachable CSV folder, a hold it had to give up on - is
 * logged here at Warning, never swallowed.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogStreamGuard, Log, All);
