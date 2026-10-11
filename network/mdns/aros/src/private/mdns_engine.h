/*
 * mdns_engine.h - mdnsd's engine state
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
    struct Task    *rs_Task;        /* the client that registered it */
    UWORD           rs_State;       /* MDNS_RS_* */
    UWORD           rs_Step;        /* probes or announcements sent so far */
    ULONG           rs_NextTick;    /* mdnsEngineTicks() of the next step */
    ULONG           rs_Answered;    /* et_PacketSerial it last answered */
    BOOL            rs_Proxy;       /* host and address given by the client */
    ULONG           rs_HostAddr;
};

/* A name is asked for three times, 250 ms apart, before it is announced
 * (RFC 6762 section 8.1), and then announced twice, a second apart. */
#define MDNS_RS_PROBING         0
#define MDNS_RS_ANNOUNCED       1
#define MDNS_PROBE_COUNT        3
#define MDNS_PROBE_TICKS        13      /* 250 ms in 1/50 s */
#define MDNS_ANNOUNCE_COUNT     2

/* A browse asks after 1, 2, 4... seconds, then once a minute. */
#define MDNS_BROWSE_INTERVAL_MAX 60

struct MDNSBrowseSession
{
    struct Node     bs_Node;
    APTR            bs_Ref;
    char            bs_Type[MDNS_MAX_TYPE + 1];
    struct Hook    *bs_Hook;
    struct Task    *bs_Task;
    ULONG           bs_Expire;
    ULONG           bs_LastQuery;   /* mdnsEngineNow() of last PTR query */
    ULONG           bs_Interval;    /* seconds until the next one */
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
#define MDNS_CE_LOCAL           0x0002  /* registered on this host: never expires */
#define MDNS_CE_ASKED           0x0004  /* ask for its SRV and TXT if they do not come */

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

/* A QueryRecordA question: every record that answers it goes to the hook. */
struct MDNSQuerySession
{
    struct Node     qs_Node;
    APTR            qs_Ref;
    char            qs_Name[256];
    UWORD           qs_Type;
    UWORD           qs_Pad;
    struct Hook    *qs_Hook;
    struct Task    *qs_Task;
    ULONG           qs_LastQuery;
    ULONG           qs_Interval;
};

/* A host's address, kept apart from the instances: the address record may
 * arrive before the SRV record that names the host. */
#define MDNS_HOSTS_MAX          64

struct MDNSHostEntry
{
    struct Node     he_Node;
    char            he_Name[MDNS_MAX_HOST + 1];
    ULONG           he_Addr;
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
    struct List             et_Hosts;
    struct List             et_Queries;
    struct List             et_Lookups;     /* MDNSIPCMsg not yet answered */
    ULONG                   et_HostCount;
    struct SignalSemaphore  et_ServiceLock;
    struct SignalSemaphore  et_SessionLock;
    volatile BOOL           et_Running;
    volatile BOOL           et_Ready;
    ULONG                   et_NextHandle;
    ULONG                   et_PacketSerial; /* counts received queries */
    UWORD                   et_QueryId;
    UWORD                   et_Pad;
};

BOOL MDNSEngineDispatch(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg);
void MDNSEngineHandlePacket(struct MDNSEngineToken *tok,
    const UBYTE *pkt, LONG pktlen, ULONG srcaddr);
void MDNSEngineTick(struct MDNSEngineToken *tok);

LONG MDNSEngineRegister(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg);
void MDNSEngineDeregister(struct MDNSEngineToken *tok, APTR ref);
LONG MDNSEngineBrowse(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg);
void MDNSEngineStopBrowse(struct MDNSEngineToken *tok, APTR ref);
void MDNSEngineReleaseServices(struct MDNSEngineToken *tok, struct Task *task);
void MDNSEngineReleaseBrowses(struct MDNSEngineToken *tok, struct Task *task);
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
void mdnsRegHostChanged(struct MDNSEngineToken *tok);
void mdnsRegGoodbyeAll(struct MDNSEngineToken *tok);
void mdnsRegHandleQuery(struct MDNSEngineToken *tok, struct MDNSQuestion *q,
    char (*known)[256], LONG knownCount);
void mdnsRegHandleAnswerName(struct MDNSEngineToken *tok, const char *name,
    ULONG srcaddr);
BOOL mdnsRegTick(struct MDNSEngineToken *tok);

void mdnsBrowseSendQuery(struct MDNSEngineToken *tok, const char *type,
    BOOL unicast);
void mdnsSendQuestion(struct MDNSEngineToken *tok, const char *name,
    UWORD qtype, BOOL unicast);
void mdnsBrowseRetick(struct MDNSEngineToken *tok);
void mdnsCacheExpire(struct MDNSEngineToken *tok);
struct MDNSHostEntry *mdnsHostFind(struct MDNSEngineToken *tok,
    const char *name);

BOOL MDNSEngineLookup(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg);
void mdnsLookupHostKnown(struct MDNSEngineToken *tok, const char *name,
    ULONG addr);
void mdnsLookupAbort(struct MDNSEngineToken *tok);
LONG MDNSEngineQuery(struct MDNSEngineToken *tok, struct MDNSIPCMsg *msg);
void MDNSEngineStopQuery(struct MDNSEngineToken *tok, APTR ref);
void MDNSEngineReleaseQueries(struct MDNSEngineToken *tok, struct Task *task);
void mdnsQueryHandleRecord(struct MDNSEngineToken *tok, struct MDNSRecord *r,
    const UBYTE *pkt, LONG pktlen);
void mdnsQueryTick(struct MDNSEngineToken *tok);
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
ULONG mdnsEngineTicks(void);

#endif /* BONAMI_PRIVATE_MDNS_ENGINE_H */
