/*
 * bonami_debug.h - console trace for bonami.library
 *
 * Output appears after L_OpenLibs has set DOSBase (same as hotlinks hl_debug.h).
 * Calls before that are silently skipped.
 */

#ifndef BONAMI_PRIVATE_BONAMI_DEBUG_H
#define BONAMI_PRIVATE_BONAMI_DEBUG_H

#include <exec/types.h>

void baDbgOut(CONST_STRPTR s);
void baDbgOutLong(CONST_STRPTR fmt, LONG v);
void baDbgOutPtr(CONST_STRPTR fmt, APTR p);

/*
 * Console tracing is compiled out by default.  Define BONAMI_DEBUG (e.g. via
 * the smakefile: DEFINE BONAMI_DEBUG) to re-enable the baDbgPut* trace calls
 * across the library.  When disabled the macros expand to nothing, so no call
 * sites need to change and no trace strings are linked into the release build.
 */
#ifdef BONAMI_DEBUG

#define baDbgPut(s)           baDbgOut((CONST_STRPTR)(s))
#define baDbgPutLong(s, v)    baDbgOutLong((CONST_STRPTR)(s), (LONG)(v))
#define baDbgPutPtr(s, v)     baDbgOutPtr((CONST_STRPTR)(s), (APTR)(v))

#else

#define baDbgPut(s)           ((void)0)
#define baDbgPutLong(s, v)    ((void)0)
#define baDbgPutPtr(s, v)     ((void)0)

#endif /* BONAMI_DEBUG */

#endif /* BONAMI_PRIVATE_BONAMI_DEBUG_H */
