/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * bonami_types.c - service-type alias table for the commodity cycle gadgets.
 */

#include <exec/types.h>
#include <bonami/mdns.h>

#include "bonami_types.h"

struct BCServiceType
{
    const char *bc_Alias;
    const char *bc_Type;
    const char *bc_Friendly;
};

/*
 * First entry per type supplies BATypeFriendly text.  Several aliases may map
 * to the same DNS-SD type string.
 */
static const struct BCServiceType BCServiceTypes[] =
{
    { "web",         "_http._tcp",           "Web (HTTP)" },
    { "http",        "_http._tcp",           "Web (HTTP)" },
    { "https",       "_https._tcp",          "Secure web" },
    { "files",       "_afpovertcp._tcp",     "Apple files" },
    { "afp",         "_afpovertcp._tcp",     "Apple files" },
    { "smb",         "_smb._tcp",            "Windows share" },
    { "ftp",         "_ftp._tcp",            "FTP" },
    { "ssh",         "_ssh._tcp",            "SSH" },
    { "printer",     "_ipp._tcp",            "Printer" },
    { "ipp",         "_ipp._tcp",            "Printer" },
    { "vnc",         "_rfb._tcp",            "Screen share" },
    { "airplay",     "_airplay._tcp",        "AirPlay" },
    { "cast",        "_googlecast._tcp",     "Chromecast" },
    { "companion",   "_companion-link._tcp", "Companion" },
    { "envoy",       "_envoy-nipc._tcp",     "Envoy host" },
    { NULL,          NULL,                   NULL }
};

/* Cycle gadget shows friendly names; parallel array of wire types. */
static STRPTR BC_CycleLabels[20];
static const char *BC_CycleTypes[20];

BOOL
BCStrEqualCI(const char *a, const char *b)
{
    LONG i;
    char ca;
    char cb;

    if (a == NULL || b == NULL) {
        return FALSE;
    }
    i = 0;
    for (;;) {
        ca = a[i];
        cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb + ('a' - 'A'));
        }
        if (ca != cb) {
            return FALSE;
        }
        if (ca == '\0') {
            return TRUE;
        }
        i++;
    }
}

CONST_STRPTR
BCTypeFromAlias(CONST_STRPTR word)
{
    LONG i;

    if (word == NULL) {
        return NULL;
    }
    if (word[0] == '_') {
        return word;
    }
    for (i = 0; BCServiceTypes[i].bc_Alias != NULL; i++) {
        if (BCStrEqualCI((const char *)word, BCServiceTypes[i].bc_Alias)) {
            return (CONST_STRPTR)BCServiceTypes[i].bc_Type;
        }
    }
    return word;
}

CONST_STRPTR
BCTypeFriendly(CONST_STRPTR type)
{
    LONG i;

    if (type == NULL) {
        return (CONST_STRPTR)"?";
    }
    for (i = 0; BCServiceTypes[i].bc_Type != NULL; i++) {
        if (BCStrEqualCI((const char *)type, BCServiceTypes[i].bc_Type)) {
            return (CONST_STRPTR)BCServiceTypes[i].bc_Friendly;
        }
    }
    return type;
}

STRPTR *
BCTypeCycleLabels(VOID)
{
    LONG i;
    LONG n;
    LONG j;
    BOOL dup;

    /* First entry: special meta browse (service-type enumeration). */
    n = 0;
    BC_CycleLabels[n] = (STRPTR)"All";
    BC_CycleTypes[n] = (const char *)MDNS_TYPE_SERVICES_DNS_SD;
    n++;

    for (i = 0; BCServiceTypes[i].bc_Type != NULL; i++) {
        if (BCServiceTypes[i].bc_Alias == NULL ||
            BCServiceTypes[i].bc_Alias[0] == '\0') {
            continue;
        }
        dup = FALSE;
        for (j = 0; j < n; j++) {
            if (BCStrEqualCI(BC_CycleTypes[j], BCServiceTypes[i].bc_Type)) {
                dup = TRUE;
                break;
            }
        }
        if (!dup) {
            BC_CycleLabels[n] = (STRPTR)BCServiceTypes[i].bc_Friendly;
            BC_CycleTypes[n] = BCServiceTypes[i].bc_Type;
            n++;
        }
    }
    BC_CycleLabels[n] = NULL;
    BC_CycleTypes[n] = NULL;
    return BC_CycleLabels;
}

LONG
BCTypeCycleIndex(CONST_STRPTR type)
{
    LONG i;

    if (type == NULL) {
        return 0;
    }
    for (i = 0; BC_CycleTypes[i] != NULL; i++) {
        if (BCStrEqualCI((const char *)type, BC_CycleTypes[i])) {
            return i;
        }
    }
    return 0;
}

CONST_STRPTR
BCTypeFromCycleIndex(LONG index)
{
    if (index < 0) {
        return (CONST_STRPTR)"_http._tcp";
    }
    if (BC_CycleTypes[index] != NULL) {
        return (CONST_STRPTR)BC_CycleTypes[index];
    }
    return (CONST_STRPTR)"_http._tcp";
}

LONG
BCTypeCycleIndexForWire(CONST_STRPTR wire)
{
    LONG i;

    if (wire == NULL || wire[0] == '\0') {
        return -1;
    }
    for (i = 0; BC_CycleTypes[i] != NULL; i++) {
        if (BCStrEqualCI((const char *)wire, BC_CycleTypes[i])) {
            return i;
        }
    }
    return -1;
}
