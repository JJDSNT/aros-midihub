/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * bonami_main.c - Bonami commodity broker, event loop, and library startup.
 *
 * Standalone commodity framework (no example/common dependency).  Opens the
 * window on Ctrl-Alt-N (CX_POPKEY) or from Exchange CXCMD_APPEAR.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <workbench/startup.h>
#include <intuition/intuition.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/commodities.h>
#include <proto/icon.h>
#include <proto/gadtools.h>
#include <proto/utility.h>

#include "bonami_ce.h"
#include "bonami_window.h"
#include "bonami_mdns.h"

/*****************************************************************************/

BOOL CreateCx(struct MsgPort *port);
VOID DisposeCx(VOID);
BOOL ProcessCxMsg(CxMsg *msg);

/*****************************************************************************/

/* AROS: the startup code provides WBenchMsg and opens the libraries. */
extern struct WBStartup *WBenchMsg;

struct MsgPort *cxPort;
CxObj          *cxBroker;
LONG            cxSignal;
LONG            cxPri;
BOOL            cxPopUp;
STRPTR          cxPopKey;

struct MsgPort *intuiPort;
ULONG           intuiMask;

static const char version_tag[] __attribute__((used)) =
    "\0$VER: Bonami 40.1 (09.07.26)";

/*****************************************************************************/

int
main(void)
{
    ULONG sigs;
    APTR msg;
    ULONG cxSigMask;
    ULONG cxMask;
    BOOL ok;
    IPTR opts[OPT_COUNT];
    BPTR oldCD;
    STRPTR arg;
    struct WBArg *wbArg;
    struct RDArgs *rdargs;
    struct DiskObject *diskObj;

    cxPort        = CreateMsgPort();
    cxSignal      = AllocSignal(-1);
    cxBroker      = NULL;
    cxPri         = CE_PRIORITY;
    cxPopUp       = CE_POPUP;
    cxPopKey      = (STRPTR)CE_POPKEY;
    rdargs        = NULL;
    diskObj       = NULL;
    intuiMask     = 0;
    ok            = FALSE;

    if (cxPort && (cxSignal >= 0)) {
        cxMask    = (1L << cxPort->mp_SigBit);
        cxSigMask = (1L << cxSignal);
        oldCD     = 0;

        if (WBenchMsg) {
            wbArg = WBenchMsg->sm_ArgList;
            /*
             * Only change the current directory when started from Workbench.
             * The commodity examples do this so icon-relative resources (like
             * tool types) resolve correctly.  When started from CLI, changing
             * the current dir (even briefly) can confuse the shell or other
             * running code, and if we ever exit early we could leave it
             * altered.  Keep CLI startup side-effect free.
             */
            oldCD = CurrentDir(wbArg->wa_Lock);
            diskObj = GetDiskObject(wbArg->wa_Name);
            if (diskObj != NULL) {
                arg = FindToolType(diskObj->do_ToolTypes, "CX_PRIORITY");
                if (arg != NULL) {
                    StrToLong(arg, &cxPri);
                }
                arg = FindToolType(diskObj->do_ToolTypes, "CX_POPUP");
                if (arg != NULL) {
                    cxPopUp = (Stricmp(arg, "NO") != 0) ? TRUE : FALSE;
                }
                arg = FindToolType(diskObj->do_ToolTypes, "CX_POPKEY");
                if (arg != NULL) {
                    cxPopKey = arg;
                }
            }
            ok = TRUE;
        } else {
            memset(opts, 0, sizeof(opts));
            rdargs = ReadArgs((STRPTR)TEMPLATE, opts, NULL);
            if (rdargs != NULL) {
                if (opts[OPT_PRIORITY] != 0) {
                    cxPri = *((LONG *)opts[OPT_PRIORITY]);
                }
                if (opts[OPT_POPUP] != 0) {
                    cxPopUp = (Stricmp((STRPTR)opts[OPT_POPUP], "NO") != 0) ?
                        TRUE : FALSE;
                }
                if (opts[OPT_POPKEY] != 0) {
                    cxPopKey = (STRPTR)opts[OPT_POPKEY];
                }
                ok = TRUE;
            } else {
                PrintFault(IoErr(), (STRPTR)CE_NAME);
            }
        }

        if (ok && ProcessCustomArgs(WBenchMsg, diskObj, opts)) {
            if (CreateCx(cxPort)) {
                if (cxPopUp) {
                    CreateWindow();
                }

                while (ok) {
                    sigs = Wait(SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_E |
                        SIGBREAKF_CTRL_F | intuiMask | cxMask | cxSigMask);

                    if ((sigs & SIGBREAKF_CTRL_E) || (sigs & SIGBREAKF_CTRL_C)) {
                        break;
                    }

                    if (sigs & SIGBREAKF_CTRL_F) {
                        CreateWindow();
                    }

                    if (sigs & cxSigMask) {
                        ProcessCustomCxSig();
                    }

                    while (ok && (msg = GetMsg(cxPort)) != NULL) {
                        ok = ProcessCxMsg((CxMsg *)msg);
                    }

                    while (ok && intuiPort != NULL &&
                        (msg = GT_GetIMsg(intuiPort)) != NULL) {
                        ok = ProcessIntuiMsg((struct IntuiMessage *)msg);
                    }
                }

                DisposeCx();
                DisposeWindow();
            }
        }

        if (diskObj != NULL) {
            FreeDiskObject(diskObj);
        }
        if (oldCD != 0) {
            CurrentDir(oldCD);
        }
        if (rdargs != NULL) {
            FreeArgs(rdargs);
        }
    }

    if (cxSignal >= 0) {
        FreeSignal(cxSignal);
    }

    if (cxPort != NULL) {
        while ((msg = GetMsg(cxPort)) != NULL) {
            ReplyMsg((struct Message *)msg);
        }
        DeleteMsgPort(cxPort);
    }

    return 0;
}

/*****************************************************************************/

BOOL
ProcessCxMsg(CxMsg *msg)
{
    ULONG msgID;
    ULONG msgType;

    msgID   = CxMsgID(msg);
    msgType = CxMsgType(msg);
    ReplyMsg((struct Message *)msg);

    switch (msgType) {
    case CXM_IEVENT:
        switch (msgID) {
        case CE_POPKEYID:
            CreateWindow();
            break;
        default:
            ProcessCustomCxMsg(msgID);
            break;
        }
        break;

    case CXM_COMMAND:
        switch (msgID) {
        case CXCMD_DISABLE:
            ActivateCxObj(cxBroker, 0);
            break;
        case CXCMD_ENABLE:
            ActivateCxObj(cxBroker, 1);
            break;
        case CXCMD_APPEAR:
        case CXCMD_UNIQUE:
            CreateWindow();
            break;
        case CXCMD_DISAPPEAR:
            DisposeWindow();
            break;
        case CXCMD_KILL:
            return FALSE;
        default:
            ProcessCustomCxCmd(msgID);
            break;
        }
        break;
    }
    return TRUE;
}

/*****************************************************************************/

BOOL
CreateCx(struct MsgPort *port)
{
    struct NewBroker nb;
    CxObj *filter;

    DisposeCx();

    nb.nb_Version         = NB_VERSION;
    nb.nb_Name            = (STRPTR)CE_NAME;
    nb.nb_Title           = (STRPTR)CE_TITLE;
    nb.nb_Descr           = (STRPTR)CE_DESCRIPTION;
    nb.nb_Unique          = NBU_NOTIFY | NBU_UNIQUE;
    nb.nb_Pri             = cxPri;
    nb.nb_Port            = port;
    nb.nb_ReservedChannel = 0;
    nb.nb_Flags           = COF_SHOW_HIDE;

    cxBroker = CxBroker(&nb, NULL);
    if (cxBroker != NULL) {
        filter = CxFilter(cxPopKey);
        AttachCxObj(filter, CxSender(port, CE_POPKEYID));
        AttachCxObj(filter, CxTranslate(NULL));
        AttachCxObj(cxBroker, filter);

        if (CreateCustomCx(cxBroker)) {
            if (CxObjError(cxBroker) == 0) {
                ActivateCxObj(cxBroker, 1);
                return TRUE;
            }
            DisposeCustomCx();
        }
        DeleteCxObjAll(cxBroker);
        cxBroker = NULL;
    }
    return FALSE;
}

/*****************************************************************************/

VOID
DisposeCx(VOID)
{
    if (cxBroker != NULL) {
        DisposeCustomCx();
        DeleteCxObjAll(cxBroker);
        cxBroker = NULL;
    }
}

/*****************************************************************************/

BOOL
ProcessCustomArgs(struct WBStartup *wbMsg, struct DiskObject *diskObj,
    IPTR *cliOpts)
{
    (void)wbMsg;
    (void)diskObj;
    (void)cliOpts;
    BCMdnsInit(&g_MDNS);
    return BCMdnsOpen();
}

VOID
ProcessCustomCxMsg(ULONG cmd)
{
    (void)cmd;
}

VOID
ProcessCustomCxCmd(ULONG cmd)
{
    (void)cmd;
}

VOID
ProcessCustomCxSig(VOID)
{
}

BOOL
CreateCustomCx(CxObj *broker)
{
    (void)broker;
    return TRUE;
}

VOID
DisposeCustomCx(VOID)
{
    BCMdnsStopBrowse(&g_MDNS);
    BCMdnsUnshare(&g_MDNS);
    BCMdnsFree(&g_MDNS);
    BCMdnsClose();
}
