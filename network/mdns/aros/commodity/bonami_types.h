/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * bonami_types.h - plain-English service-type aliases for the commodity UI.
 */

#ifndef BONAMI_TYPES_H
#define BONAMI_TYPES_H

#include <exec/types.h>

CONST_STRPTR BCTypeFromAlias(CONST_STRPTR word);
CONST_STRPTR BCTypeFriendly(CONST_STRPTR type);
BOOL BCStrEqualCI(const char *a, const char *b);

/* Cycle-gadget label table (NULL-terminated). */
STRPTR *BCTypeCycleLabels(VOID);
CONST_STRPTR BCTypeFromCycleIndex(LONG index);
LONG BCTypeCycleIndex(CONST_STRPTR type);
/* Map a wire type (e.g. "_http._tcp") to a cycle index, or -1 if unknown. */
LONG BCTypeCycleIndexForWire(CONST_STRPTR wire);

#endif /* BONAMI_TYPES_H */
