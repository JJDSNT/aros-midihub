/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * bonami_ce.h - Bonami commodity definitions (standalone, no locale catalog).
 */

#ifndef BONAMI_CE_H
#define BONAMI_CE_H

#include <exec/types.h>
#include <libraries/commodities.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>

/*****************************************************************************/

#define TEMPLATE        "CX_PRIORITY/N/K,CX_POPKEY/K,CX_POPUP/K"
#define OPT_PRIORITY    0
#define OPT_POPKEY      1
#define OPT_POPUP       2
#define OPT_COUNT       3

#define CE_NAME         "Bonami"
#define CE_TITLE        "Bonami"
#define CE_DESCRIPTION  "Browse, resolve and share mDNS services on the LAN"
#define CE_PRIORITY     0
#define CE_POPKEY       "control alt n"
#define CE_POPUP        FALSE
#define CE_WINDOW       1
#define CE_POPKEYID     1

#define WINDOW_LEFT     0
#define WINDOW_TOP      0
#define WINDOW_WIDTH    566
#define WINDOW_HEIGHT   190

#define IDCMPFLAGS  (BUTTONIDCMP | LISTVIEWIDCMP | IDCMP_CLOSEWINDOW | \
                     IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | IDCMP_INTUITICKS)
#define WINDOWFLAGS (WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET | \
                     WFLG_ACTIVATE | WFLG_SIMPLE_REFRESH)

/*****************************************************************************/
/* UI command IDs (gadget UserData and menu USERDATA). */

#define CMD_NOP             0
#define CMD_HIDE            1
#define CMD_QUIT            2
#define CMD_NEARBY_START    10
#define CMD_NEARBY_STOP     11
#define CMD_NEARBY_LIST     13
#define CMD_HANDSHAKE_GO    20
#define CMD_SHARE_START     30
#define CMD_SHARE_STOP      31

/*****************************************************************************/

BOOL CreateCustomCx(CxObj *broker);
VOID DisposeCustomCx(VOID);

BOOL ProcessCustomArgs(struct WBStartup *wbMsg, struct DiskObject *diskObj,
    IPTR *cliOpts);
VOID ProcessCustomCxMsg(ULONG cmd);
VOID ProcessCustomCxCmd(ULONG cmd);
VOID ProcessCustomCxSig(VOID);

BOOL CreateCustomGadgets(VOID);
BOOL CreateCustomMenus(VOID);
BOOL ProcessIntuiMsg(struct IntuiMessage *intuiMsg);
VOID RefreshWindow(BOOL refreshEvent);
VOID OnIntuiTick(VOID);

/*****************************************************************************/
/* Embedded English strings */

#define STR_WINDOW_TITLE    "Bonami  (%s)"
#define STR_MENU_PROJECT    "Project"
#define STR_MENU_HIDE       "H\0Hide"
#define STR_MENU_QUIT       "Q\0Quit"
#define STR_LBL_NEARBY      "Nearby - services on the network"
#define STR_LBL_HANDSHAKE   "Handshake - resolve a service"
#define STR_LBL_SHARE       "Share - services on this Amiga"
#define STR_GAD_BROWSE      "Browse"
#define STR_GAD_STOP        "Stop"
#define STR_GAD_ALL         "All"
#define STR_GAD_HANDSHAKE   "Handshake"
#define STR_GAD_SHARE       "Share"
#define STR_GAD_UNSHARE     "Stop sharing"
#define STR_GAD_TYPE        "Kind"
#define STR_GAD_PORT        "Port"
#define STR_GAD_INFO        "Extra info"
#define STR_GAD_RESULT      "Connection details"

#endif /* BONAMI_CE_H */
