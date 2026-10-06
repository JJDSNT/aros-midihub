/*
 * mdns_engine.h - shared mDNS engine state (NamedObject payload)
 */

#ifndef BONAMI_PRIVATE_MDNS_ENGINE_H
#define BONAMI_PRIVATE_MDNS_ENGINE_H

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/ports.h>
#include <exec/semaphores.h>
#include <utility/hooks.h>
#include <utility/tagitem.h>
#include <utility/name.h>

#include "private/bonami_ipc.h"
#include "private/mdns_dns.h"

#ifndef BONAMI_PRIVATE_BONAMI_INTERNAL_H
struct BonamiPrivate;
#endif

#define MDNS_ENGINE_TOKEN       "Envoy.mdns"
#define MDNS_ENGINE_STACK       16384

/*
 * Signature stamped into a live token.  A NamedObject published in the
 * system-wide namespace can outlive the token memory it points at if a run
 * ends abnormally; the next run would then find a dangling no_Object.  We
 * validate this magic before trusting a found token so a stale entry is
 * detected and rebuilt instead of dereferenced.  A freed AllocMem block has
 * its first longwords overwritten by the memory free-list, so a stale token's
 * magic will no longer match.
 */
#define MDNS_TOKEN_MAGIC        0x424F4E41UL    /* 'BONA' */

/* Environment variable that overrides the mDNS UDP port (testing aid). */
#define MDNS_PORT_ENVVAR        "BONAMI_MDNS_PORT"

/* et_StartStage diagnostic values, reported when engine start-up fails. */
#define MDNS_STAGE_NONE         0
#define MDNS_STAGE_SOCKET       1
#define MDNS_STAGE_BIND         2
#define MDNS_STAGE_MSGPORT      3

struct MDNSRegisteredService
{
    struct Node     rs_Node;
    char            rs_Instance[MDNS_MAX_NAME + 1];
    char            rs_Type[MDNS_MAX_TYPE + 1];
    char            rs_Domain[MDNS_MAX_DOMAIN + 1];
    char            rs_HostTarget[MDNS_MAX_HOST + 1];
    char            rs_TXT[MDNS_MAX_TXT + 1];
    UWORD           rs_Port;
    UWORD           rs_Pad;
    APTR            rs_ClientRef;
    ULONG           rs_HostAddr;
};

struct MDNSBrowseSession
{
    struct Node     bs_Node;
    APTR            bs_Ref;
    char            bs_Type[MDNS_MAX_TYPE + 1];
    struct Hook    *bs_Hook;
    struct Task    *bs_Task;
    ULONG           bs_Expire;
    ULONG           bs_LastQuery;   /* mdnsEngineNow() of last PTR query */
    ULONG           bs_MaxResponses;
    ULONG           bs_Responses;
    BOOL            bs_NIPCMode;
    struct TagItem  bs_NIPCTags[16];
    ULONG           bs_NIPCTagCount;
};

struct MDNSInquirySession
{
    struct Node     is_Node;
    struct Hook    *is_Hook;
    struct Task    *is_Task;
    ULONG           is_Expire;
    ULONG           is_MaxResponses;
    ULONG           is_Responses;
    struct TagItem  is_QueryTags[16];
    ULONG           is_QueryCount;
};

/*
 * ce_Flags bits.  A browse ADD is delivered to clients only once per new entry
 * and again only when the entry's data actually changes.  Without this a
 * periodic browse re-query (every few seconds) would re-fire MEVENT_ADD on
 * every refresh, flooding the client with duplicate notifications.
 */
#define MDNS_CE_ANNOUNCED       0x0001  /* MEVENT_ADD already delivered */

struct MDNSCacheEntry
{
    struct Node     ce_Node;
    char            ce_Name[256];
    char            ce_Instance[MDNS_MAX_NAME + 1];
    char            ce_Type[MDNS_MAX_TYPE + 1];
    char            ce_Host[MDNS_MAX_HOST + 1];
    char            ce_TXT[MDNS_MAX_TXT + 1];
    UWORD           ce_Port;
    UWORD           ce_Flags;
    ULONG           ce_HostAddr;
    ULONG           ce_Expire;
};

struct MDNSEngineToken
{
    ULONG                   et_Magic;       /* MDNS_TOKEN_MAGIC when valid */
    struct SignalSemaphore  et_Lock;
    struct NamedObject     *et_NamedObject; /* published singleton handle */
    ULONG                   et_UseCnt;
    UWORD                   et_UdpPort;     /* bind source port (0=ephemeral) */
    UWORD                   et_UdpPad;
    struct Task            *et_Task;
    struct MsgPort         *et_Port;
    LONG                    et_Socket;
    UBYTE                  *et_RxBuf;        /* MDNS_MAX_PACKET receive scratch */
    UBYTE                  *et_TxBuf;        /* MDNS_MAX_PACKET transmit scratch */
    struct Library         *et_SocketBase;  /* opened by mdns.task itself */
    volatile BOOL           et_StartDone;    /* engine startup handshake */
    LONG                    et_StartRc;      /* engine startup result */
    UWORD                   et_StartStage;   /* diagnostics: where start failed */
    LONG                    et_StartErrno;   /* diagnostics: bsdsocket Errno() */
    ULONG                   et_HostAddr;
    ULONG                   et_RxPackets;    /* datagrams recvfrom() returned */
    ULONG                   et_RxAnswers;    /* answer records parsed from RX */
    ULONG                   et_TxPackets;    /* datagrams sendto() accepted */
    ULONG                   et_DbgRecs;      /* records entering browse handler */
    ULONG                   et_DbgPtr;       /* PTR records seen by handler */
    ULONG                   et_DbgNotify;    /* browse hook fires attempted */
    char                    et_HostName[MDNS_MAX_HOST + 1];
    struct List             et_Services;
    struct List             et_BrowseSessions;
    struct List             et_InquirySessions;
    struct List             et_Cache;
    struct SignalSemaphore  et_ServiceLock;
    struct SignalSemaphore  et_SessionLock;
    volatile BOOL           et_Running;
    volatile BOOL           et_Ready;
    ULONG                   et_NextHandle;
    UWORD                   et_QueryId;
    UWORD                   et_Pad;
};

int  MDNSTokenAttach(struct BonamiPrivate *pb);
void MDNSTokenDetach(struct BonamiPrivate *pb);
struct MDNSEngineToken *MDNSTokenGet(void);

void MDNSEngineMain(void);
LONG MDNSEnginePrepare(struct MDNSEngineToken *tok);
void MDNSEnginePrepareUndo(struct MDNSEngineToken *tok);
void MDNSEngineDispatch(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg);
void MDNSEngineHandlePacket(struct MDNSEngineToken *tok,
    const UBYTE *pkt, LONG pktlen, ULONG srcaddr);
void MDNSEngineTick(struct MDNSEngineToken *tok);

LONG MDNSEngineRegister(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg);
void MDNSEngineDeregister(struct MDNSEngineToken *tok, APTR ref);
LONG MDNSEngineBrowse(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg);
void MDNSEngineStopBrowse(struct MDNSEngineToken *tok, APTR ref);
LONG MDNSEngineResolve(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg);
LONG MDNSEngineInquiryNIPC(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg);
void MDNSEngineInquiryTick(struct MDNSEngineToken *tok);

LONG mdnsNetOpen(struct MDNSEngineToken *tok);
void mdnsNetClose(struct MDNSEngineToken *tok);
LONG mdnsNetSend(struct MDNSEngineToken *tok, const UBYTE *pkt, LONG pktlen,
    ULONG destaddr, UWORD destport);
LONG mdnsNetSendMulticast(struct MDNSEngineToken *tok, const UBYTE *pkt,
    LONG pktlen);

void mdnsRegAnnounceAll(struct MDNSEngineToken *tok);
void mdnsRegHandleQuery(struct MDNSEngineToken *tok, struct MDNSQuestion *q);

void mdnsBrowseSendQuery(struct MDNSEngineToken *tok, const char *type);
void mdnsBrowseRetick(struct MDNSEngineToken *tok);
void mdnsBrowseHandleRecord(struct MDNSEngineToken *tok, struct MDNSRecord *r,
    const UBYTE *pkt, LONG pktlen);
void mdnsBrowseFireHook(struct MDNSEngineToken *tok, struct MDNSBrowseSession *bs,
    struct MDNSCacheEntry *ce, ULONG code);
void mdnsBrowseAnnounceLocal(struct MDNSEngineToken *tok,
    struct MDNSRegisteredService *rs);
void mdnsBrowseRemoveLocal(struct MDNSEngineToken *tok,
    struct MDNSRegisteredService *rs);
void mdnsInquiryFireNIPC(struct MDNSEngineToken *tok, struct MDNSInquirySession *is,
    struct MDNSCacheEntry *ce);

APTR mdnsEngineNextHandle(struct MDNSEngineToken *tok);
ULONG mdnsEngineNow(void);

#endif /* BONAMI_PRIVATE_MDNS_ENGINE_H */
