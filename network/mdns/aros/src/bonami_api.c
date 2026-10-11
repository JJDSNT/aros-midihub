/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 * Copyright 2026 Jaime Dias
 *
 * AROS library entry points for the Bonami engine.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <aros/libcall.h>
#include <aros/macros.h>
#include <string.h>

#include <bonami/mdns.h>
#include <bonami/nipc.h>
#include "private/bonami_internal.h"
#include "private/bonami_ipc.h"

static void store_ref(struct TagItem *tags, ULONG which, APTR value)
{
    struct TagItem *tag;

    if (tags == NULL)
        return;
    tag = FindTagItem(which, tags);
    if (tag != NULL && tag->ti_Data != 0)
        *((APTR *)tag->ti_Data) = value;
}

AROS_LH0(LONG, OpenMDNSEngine,
    struct BonamiPrivate *, BonamiBase, 5, Bonami)
{
    AROS_LIBFUNC_INIT
    return MDNSEnsureEngine(BonamiBase);
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, CloseMDNSEngine,
    struct BonamiPrivate *, BonamiBase, 6, Bonami)
{
    AROS_LIBFUNC_INIT
    /* The engine is the system's and stays; the caller's objects go. */
    MDNSReleaseTask(BonamiBase);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, RegisterServiceA,
    AROS_LHA(struct TagItem *, tags, A0),
    struct BonamiPrivate *, BonamiBase, 7, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;
    LONG rc;

    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_REGISTER;
    rc = MDNSTagsToRegisterMsg(tags, &msg);
    if (rc == MDNS_OK) {
        rc = MDNSSendIPC(BonamiBase, &msg);
        if (rc == MDNS_OK)
            store_ref(tags, MDNS_RegisterRef, msg.bim_Handle);
    }
    return rc;
    AROS_LIBFUNC_EXIT
}

AROS_LH2(LONG, UpdateServiceA,
    AROS_LHA(MDNSRegisterRef, ref, A0),
    AROS_LHA(struct TagItem *, tags, A1),
    struct BonamiPrivate *, BonamiBase, 8, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;
    LONG rc;

    memset(&msg, 0, sizeof(msg));
    if (ref == NULL || tags == NULL)
        return MDNSERR_BADPARAM;
    /* Only the text and the port of a registered service can change; a tag
     * that is not given leaves its value alone. */
    MDNSTagsToRegisterMsg(tags, &msg);
    msg.bim_Command = BONAMI_MSG_UPDATE;
    msg.bim_Handle = ref;
    msg.bim_Flags = FindTagItem(MDNS_TXTRecord, tags) != NULL ?
        BONAMI_UPDF_TXT : 0;
    rc = MDNSSendIPC(BonamiBase, &msg);
    return rc;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, DeregisterService,
    AROS_LHA(MDNSRegisterRef, ref, A0),
    struct BonamiPrivate *, BonamiBase, 9, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;

    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_UNREGISTER;
    msg.bim_Handle = ref;
    MDNSSendIPC(BonamiBase, &msg);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, BrowseServiceTypeA,
    AROS_LHA(STRPTR, type, A0),
    AROS_LHA(struct Hook *, hook, A1),
    AROS_LHA(struct TagItem *, tags, A2),
    struct BonamiPrivate *, BonamiBase, 10, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;
    LONG rc;

    if (type == NULL || hook == NULL)
        return MDNSERR_BADPARAM;
    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_BROWSE;
    msg.bim_Hook = hook;
    strncpy(msg.bim_ServiceType, type, MDNS_MAX_TYPE);
    msg.bim_ServiceType[MDNS_MAX_TYPE] = '\0';
    rc = MDNSSendIPC(BonamiBase, &msg);
    if (rc == MDNS_OK)
        store_ref(tags, MDNS_BrowseRef, msg.bim_Handle);
    return rc;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, StopBrowse,
    AROS_LHA(MDNSBrowseRef, ref, A0),
    struct BonamiPrivate *, BonamiBase, 11, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;

    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_STOPBROWSE;
    msg.bim_Handle = ref;
    MDNSSendIPC(BonamiBase, &msg);
    AROS_LIBFUNC_EXIT
}

AROS_LH4(LONG, ResolveServiceA,
    AROS_LHA(STRPTR, name, A0),
    AROS_LHA(STRPTR, type, A1),
    AROS_LHA(struct Hook *, hook, A2),
    AROS_LHA(struct TagItem *, tags, A3),
    struct BonamiPrivate *, BonamiBase, 12, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;
    LONG rc;

    if (name == NULL || type == NULL || hook == NULL)
        return MDNSERR_BADPARAM;
    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_RESOLVE;
    msg.bim_Hook = hook;
    strncpy(msg.bim_Name, name, MDNS_MAX_NAME);
    msg.bim_Name[MDNS_MAX_NAME] = '\0';
    strncpy(msg.bim_ServiceType, type, MDNS_MAX_TYPE);
    msg.bim_ServiceType[MDNS_MAX_TYPE] = '\0';
    rc = MDNSSendIPC(BonamiBase, &msg);
    if (rc == MDNS_OK)
        store_ref(tags, MDNS_ResolveRef, msg.bim_Handle);
    return rc;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, StopResolve,
    AROS_LHA(MDNSResolveRef, ref, A0),
    struct BonamiPrivate *, BonamiBase, 13, Bonami)
{
    AROS_LIBFUNC_INIT
    /* The imported Bonami engine resolves from its cache synchronously and
     * does not create a persistent resolve operation yet. */
    (void)ref;
    (void)BonamiBase;
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, MakeTXTRecordA,
    AROS_LHA(struct TagItem *, tags, A0),
    AROS_LHA(APTR *, record, A1),
    AROS_LHA(ULONG *, recordLen, A2),
    struct BonamiPrivate *, BonamiBase, 14, Bonami)
{
    AROS_LIBFUNC_INIT
    struct TagItem *tag;
    STRPTR text;
    UBYTE *buffer;
    ULONG length;

    (void)BonamiBase;
    if (tags == NULL || record == NULL || recordLen == NULL)
        return MDNSERR_BADPARAM;
    tag = FindTagItem(MDNS_TXTRecord, tags);
    text = tag != NULL ? (STRPTR)tag->ti_Data : NULL;
    if (text == NULL || (length = strlen(text)) > MDNS_MAX_TXT)
        return MDNSERR_BADPARAM;
    buffer = AllocMem(MDNS_MAX_TXT + 1, MEMF_CLEAR);
    if (buffer == NULL)
        return MDNSERR_NOMEM;
    memcpy(buffer, text, length + 1);
    *record = buffer;
    *recordLen = length;
    return MDNS_OK;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, FreeTXTRecord,
    AROS_LHA(APTR, record, A0),
    struct BonamiPrivate *, BonamiBase, 15, Bonami)
{
    AROS_LIBFUNC_INIT
    (void)BonamiBase;
    if (record != NULL)
        FreeMem(record, MDNS_MAX_TXT + 1);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, GetMDNSStatusA,
    AROS_LHA(struct TagItem *, tags, A0),
    struct BonamiPrivate *, BonamiBase, 16, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;
    struct TagItem *state;
    struct TagItem *tag;
    ULONG *out;
    LONG rc;

    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_STATUS;
    rc = MDNSSendIPC(BonamiBase, &msg);
    state = tags;
    while (rc == MDNS_OK &&
           (tag = NextTagItem(&state)) != NULL) {
        out = (ULONG *)tag->ti_Data;
        if (out == NULL)
            continue;
        switch (tag->ti_Tag) {
        case MDNS_StatusRxPackets: *out = msg.bim_HostAddr; break;
        case MDNS_StatusTxPackets: *out = msg.bim_Flags; break;
        case MDNS_StatusRxAnswers: *out = msg.bim_Port; break;
        case MDNS_StatusDbgRecs: *out = msg.bim_Dbg0; break;
        case MDNS_StatusDbgPtr: *out = msg.bim_Dbg1; break;
        case MDNS_StatusDbgNotify: *out = msg.bim_Dbg2; break;
        default: break;
        }
    }
    return rc;
    AROS_LIBFUNC_EXIT
}

/* The tags of <envoy/nipc.h> this bridge understands. */
#define NIPC_QUERY_IPADDR       (TAG_USER + 0x2000)
#define NIPC_MATCH_IPADDR       (TAG_USER + 0x2001)
#define NIPC_QUERY_HOSTNAME     (TAG_USER + 0x2004)
#define NIPC_MATCH_HOSTNAME     (TAG_USER + 0x2005)
#define NIPC_QUERY_SERVICE      (TAG_USER + 0x2006)
#define NIPC_MATCH_SERVICE      (TAG_USER + 0x2007)
#define NIPC_QUERY_ENTITY       (TAG_USER + 0x2008)
#define NIPC_MATCH_ENTITY       (TAG_USER + 0x2009)
#define NIPC_QUERY_OWNER        (TAG_USER + 0x200A)
#define NIPC_MATCH_OWNER        (TAG_USER + 0x200B)

static void copy_match(char *to, ULONG size, IPTR from)
{
    if (from != 0) {
        strncpy(to, (STRPTR)from, size - 1);
        to[size - 1] = '\0';
    }
}

AROS_LH4(LONG, InquiryFromNIPCTagsA,
    AROS_LHA(struct Hook *, hook, A0),
    AROS_LHA(ULONG, maxTime, D0),
    AROS_LHA(ULONG, maxResponses, D1),
    AROS_LHA(struct TagItem *, tags, A1),
    struct BonamiPrivate *, BonamiBase, 17, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;
    struct TagItem *state = tags;
    struct TagItem *tag;

    if (hook == NULL)
        return MDNSERR_BADPARAM;
    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_INQUIRY;
    msg.bim_Hook = hook;
    msg.bim_Flags = maxTime > 3600 ? 3600 : maxTime;
    msg.bim_Port = (UWORD)(maxResponses > 65535 ? 65535 : maxResponses);
    while ((tag = NextTagItem(&state)) != NULL) {
        switch (tag->ti_Tag) {
        case NIPC_QUERY_HOSTNAME: msg.bim_Dbg0 |= 0x01; break;
        case NIPC_QUERY_IPADDR:   msg.bim_Dbg0 |= 0x02; break;
        case NIPC_QUERY_ENTITY:   msg.bim_Dbg0 |= 0x04; break;
        case NIPC_QUERY_SERVICE:  msg.bim_Dbg0 |= 0x08; break;
        case NIPC_QUERY_OWNER:    msg.bim_Dbg0 |= 0x10; break;
        case NIPC_MATCH_IPADDR:   msg.bim_HostAddr = (ULONG)tag->ti_Data; break;
        case NIPC_MATCH_ENTITY:
            copy_match(msg.bim_QName, MDNS_MAX_NAME + 1, tag->ti_Data);
            break;
        case NIPC_MATCH_SERVICE:
            copy_match(msg.bim_Name, sizeof(msg.bim_Name), tag->ti_Data);
            break;
        case NIPC_MATCH_OWNER:
            copy_match(msg.bim_TXT, MDNS_MAX_NAME + 1, tag->ti_Data);
            break;
        case NIPC_MATCH_HOSTNAME:
            copy_match(msg.bim_HostName, sizeof(msg.bim_HostName),
                tag->ti_Data);
            break;
        default:
            /* What DNS-SD cannot tell is neither matched nor answered. */
            break;
        }
    }
    return MDNSSendIPC(BonamiBase, &msg);
    AROS_LIBFUNC_EXIT
}

/* Appends " key=value" if it fits and the value has no space in it. */
static BOOL add_txt(char *txt, const char *key, IPTR value)
{
    STRPTR s = (STRPTR)value;
    size_t len;

    if (s == NULL || s[0] == '\0' || strchr(s, ' ') != NULL)
        return FALSE;
    len = strlen(txt);
    if (len + 1 + strlen(key) + 1 + strlen(s) > MDNS_MAX_TXT)
        return FALSE;
    if (len != 0)
        txt[len++] = ' ';
    strcpy(txt + len, key);
    strcat(txt, "=");
    strcat(txt, s);
    return TRUE;
}

AROS_LH1(LONG, PublishNIPCEntitiesA,
    AROS_LHA(struct TagItem *, tags, A0),
    struct BonamiPrivate *, BonamiBase, 18, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;
    struct TagItem *state = tags;
    struct TagItem *tag;
    BOOL fits = TRUE;

    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_REGISTER;
    msg.bim_Flags = BONAMI_REGF_NIPC;
    msg.bim_Port = MDNS_NIPC_DEFAULT_PORT;
    strcpy(msg.bim_ServiceType, MDNS_TYPE_ENVOY_NIPC);
    strcpy(msg.bim_Domain, MDNS_DOMAIN_LOCAL);
    strcpy(msg.bim_TXT, MDNS_TXTKEY_VERSION "=1");
    while ((tag = NextTagItem(&state)) != NULL) {
        switch (tag->ti_Tag) {
        case MDNS_NIPC_Entity:
            fits &= add_txt(msg.bim_TXT, MDNS_TXTKEY_ENTITY, tag->ti_Data);
            break;
        case MDNS_NIPC_Service:
            fits &= add_txt(msg.bim_TXT, MDNS_TXTKEY_SERVICE, tag->ti_Data);
            break;
        case MDNS_NIPC_Owner:
            fits &= add_txt(msg.bim_TXT, MDNS_TXTKEY_OWNER, tag->ti_Data);
            break;
        case MDNS_NIPC_Port:
            msg.bim_Port = (UWORD)tag->ti_Data;
            break;
        default:
            break;
        }
    }
    /* A name with a space, or more names than one TXT record of this
     * library holds, cannot be published. */
    if (!fits)
        return MDNSERR_BADPARAM;
    return MDNSSendIPC(BonamiBase, &msg);
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, UnpublishNIPCEntitiesA,
    struct BonamiPrivate *, BonamiBase, 19, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;

    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_UNPUBLISH;
    MDNSSendIPC(BonamiBase, &msg);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, LookupHostA,
    AROS_LHA(STRPTR, host, A0),
    AROS_LHA(ULONG *, addr, A1),
    AROS_LHA(struct TagItem *, tags, A2),
    struct BonamiPrivate *, BonamiBase, 20, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;
    LONG rc;

    if (host == NULL || addr == NULL)
        return MDNSERR_BADPARAM;
    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_LOOKUP;
    msg.bim_Flags = (ULONG)GetTagData(MDNS_Timeout, 3, tags);
    strncpy(msg.bim_QName, host, sizeof(msg.bim_QName) - 1);
    rc = MDNSSendIPC(BonamiBase, &msg);
    if (rc == MDNS_OK)
        *addr = AROS_LONG2BE(msg.bim_HostAddr);
    return rc;
    AROS_LIBFUNC_EXIT
}

AROS_LH4(LONG, QueryRecordA,
    AROS_LHA(STRPTR, name, A0),
    AROS_LHA(ULONG, rrtype, D0),
    AROS_LHA(struct Hook *, hook, A1),
    AROS_LHA(struct TagItem *, tags, A2),
    struct BonamiPrivate *, BonamiBase, 21, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;
    LONG rc;

    if (name == NULL || hook == NULL || rrtype == 0 || rrtype > 0xffff)
        return MDNSERR_BADPARAM;
    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_QUERY;
    msg.bim_Hook = hook;
    msg.bim_Port = (UWORD)rrtype;
    strncpy(msg.bim_QName, name, sizeof(msg.bim_QName) - 1);
    rc = MDNSSendIPC(BonamiBase, &msg);
    if (rc == MDNS_OK)
        store_ref(tags, MDNS_QueryRef, msg.bim_Handle);
    return rc;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, StopQuery,
    AROS_LHA(MDNSQueryRef, ref, A0),
    struct BonamiPrivate *, BonamiBase, 22, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;

    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_STOPQUERY;
    msg.bim_Handle = ref;
    MDNSSendIPC(BonamiBase, &msg);
    AROS_LIBFUNC_EXIT
}
