/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 *
 * bonami_window.c - Bonami commodity window, gadgets, and UI event handling.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <graphics/text.h>
#include <libraries/gadtools.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/dos.h>

#include <bonami/mdns.h>

#include "bonami_ce.h"
#include "bonami_window.h"
#include "bonami_mdns.h"
#include "bonami_types.h"

/*****************************************************************************/

extern struct Library       *GadToolsBase;
extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase       *GfxBase;
extern STRPTR                cxPopKey;
extern struct MsgPort       *intuiPort;
extern ULONG                 intuiMask;

struct Window   *cxWindow     = NULL;
struct Screen   *cxScreen     = NULL;
struct DrawInfo *cxDrawInfo   = NULL;
APTR             cxVisualInfo = NULL;
struct Menu     *cxMenus      = NULL;
struct Gadget   *cxGadgets    = NULL;

static WORD windowLeft   = WINDOW_LEFT;
static WORD windowTop    = WINDOW_TOP;
static WORD windowWidth  = WINDOW_WIDTH;
static WORD windowHeight = WINDOW_HEIGHT;
static char windowTitle[96];

/* Gadget pointers */
static struct Gadget *nearbyTypeGad;
static struct Gadget *nearbyBrowseGad;
static struct Gadget *nearbyStopGad;
static struct Gadget *nearbyListGad;
static struct Gadget *hsNameGad;
static struct Gadget *hsTypeGad;
static struct Gadget *hsResolveGad;
static struct Gadget *hsResultGad;
static struct Gadget *shareNameGad;
static struct Gadget *shareTypeGad;
static struct Gadget *sharePortGad;
static struct Gadget *shareTxtGad;
static struct Gadget *shareStartGad;
static struct Gadget *shareStopGad;

static ULONG imsgClass;
static UWORD imsgCode;
static struct Gadget *imsgGad;

static BOOL hsPending;
static LONG hsTries;
static BOOL nearbyBusy;
static BOOL shareBusy;

static struct TextFont *topazFont;
static struct TextAttr topazAttr =
{
    "topaz.font",
    8,
    FS_NORMAL,
    FPF_ROMFONT
};

static STRPTR *typeLabels;

static void updateNearbyBrowseButton(VOID);
static void updateShareButton(VOID);

/*****************************************************************************/

static LONG
parseNum(const char *s)
{
    LONG v;
    LONG i;

    v = 0;
    if (s != NULL) {
        for (i = 0; s[i] >= '0' && s[i] <= '9'; i++) {
            v = v * 10 + (s[i] - '0');
        }
    }
    return v;
}

static WORD
gadTextWidth(CONST_STRPTR s, WORD extra)
{
    WORD w;

    w = 0;
    if (cxWindow != NULL && cxWindow->RPort != NULL && s != NULL) {
        w = (WORD)TextLength(cxWindow->RPort, (STRPTR)s, (ULONG)strlen((const char *)s));
    }
    w = (WORD)(w + extra);
    if (w < 24) {
        w = 24;
    }
    return w;
}

static WORD
cycleMaxWidth(STRPTR *labels, WORD extra)
{
    WORD maxw;
    WORD w;
    LONG i;

    maxw = 0;
    if (labels == NULL) {
        return 80;
    }
    for (i = 0; labels[i] != NULL; i++) {
        w = gadTextWidth((CONST_STRPTR)labels[i], extra);
        if (w > maxw) {
            maxw = w;
        }
    }
    if (maxw < 80) {
        maxw = 80;
    }
    return maxw;
}

static void
refreshNearbyList(VOID)
{
    if (nearbyListGad == NULL || cxWindow == NULL) {
        return;
    }
    GT_SetGadgetAttrs(nearbyListGad, cxWindow, NULL,
        GTLV_Labels, &g_MDNS.bc_Nearby,
        TAG_DONE);
}

static void
refreshResultList(VOID)
{
    if (hsResultGad == NULL || cxWindow == NULL) {
        return;
    }
    GT_SetGadgetAttrs(hsResultGad, cxWindow, NULL,
        GTLV_Labels, &g_MDNS.bc_Results,
        TAG_DONE);
}

static void
bcWireTypeFromRow(struct BCNearbyEntry *row, char *buf, LONG max)
{
    LONG len;

    buf[0] = '\0';
    if (row == NULL || max < 2) {
        return;
    }
    if (row->bc_IsType) {
        strncpy(buf, row->bc_Inst.si_Name, (ULONG)(max - 1));
        buf[max - 1] = '\0';
        len = (LONG)strlen(buf);
        if (len + 2 < max) {
            buf[len] = '.';
            buf[len + 1] = '\0';
            strncat(buf, row->bc_Inst.si_Type, (ULONG)(max - len - 2));
        }
    } else {
        strncpy(buf, row->bc_Inst.si_Type, (ULONG)(max - 1));
        buf[max - 1] = '\0';
    }
}

static void
fillHandshakeFromNearby(UWORD index)
{
    struct BCNearbyEntry *row;
    char wiretype[MDNS_MAX_TYPE + 1];
    LONG cidx;
    LONG rc;

    row = BCMdnsNearbySelected(&g_MDNS, index);
    if (row == NULL || cxWindow == NULL) {
        return;
    }

    if (row->bc_IsType) {
        /*
         * Meta browse ("All") lists Kinds only.  Selecting one re-browses that
         * type for instances so the user can pick a service name next.
         */
        bcWireTypeFromRow(row, wiretype, (LONG)sizeof(wiretype));

        cidx = BCTypeCycleIndexForWire((CONST_STRPTR)wiretype);
        if (cidx >= 0) {
            GT_SetGadgetAttrs(nearbyTypeGad, cxWindow, NULL,
                GTCY_Active, cidx,
                TAG_DONE);
        }

        rc = BCMdnsStartBrowse(&g_MDNS, (CONST_STRPTR)wiretype, FALSE);
        if (rc == MDNS_OK) {
            refreshNearbyList();
        }
        updateNearbyBrowseButton();

        GT_SetGadgetAttrs(hsNameGad, cxWindow, NULL,
            GTST_String, "",
            TAG_DONE);
        if (cidx >= 0) {
            GT_SetGadgetAttrs(hsTypeGad, cxWindow, NULL,
                GTCY_Active, cidx,
                TAG_DONE);
        }
        return;
    }

    GT_SetGadgetAttrs(hsNameGad, cxWindow, NULL,
        GTST_String, row->bc_Inst.si_Name,
        TAG_DONE);
    cidx = BCTypeCycleIndexForWire(row->bc_Inst.si_Type);
    if (cidx < 0) {
        cidx = BCTypeCycleIndex(row->bc_Inst.si_Type);
    }
    GT_SetGadgetAttrs(hsTypeGad, cxWindow, NULL,
        GTCY_Active, cidx,
        TAG_DONE);
    ActivateGadget(hsResolveGad, cxWindow, NULL);
}

static void
updateNearbyBrowseButton(VOID)
{
    CONST_STRPTR label;
    ULONG disabled;

    if (cxWindow == NULL || nearbyBrowseGad == NULL) {
        return;
    }
    if (nearbyBusy) {
        label = (CONST_STRPTR)"...";
        disabled = TRUE;
    } else if (g_MDNS.bc_Browsing) {
        label = (CONST_STRPTR)"Stop";
        disabled = FALSE;
    } else {
        label = (CONST_STRPTR)"Browse";
        disabled = FALSE;
    }

    GT_SetGadgetAttrs(nearbyBrowseGad, cxWindow, NULL,
        GA_Text, (IPTR)label,
        GA_Disabled, disabled,
        TAG_DONE);
    RefreshGList(nearbyBrowseGad, cxWindow, NULL, 1);
}

static void
updateShareButton(VOID)
{
    CONST_STRPTR label;
    ULONG disabled;

    if (cxWindow == NULL || shareStartGad == NULL) {
        return;
    }
    if (shareBusy) {
        label = (CONST_STRPTR)"...";
        disabled = TRUE;
    } else if (g_MDNS.bc_Sharing) {
        label = (CONST_STRPTR)STR_GAD_UNSHARE;
        disabled = FALSE;
    } else {
        label = (CONST_STRPTR)STR_GAD_SHARE;
        disabled = FALSE;
    }
    GT_SetGadgetAttrs(shareStartGad, cxWindow, NULL,
        GA_Text, (IPTR)label,
        GA_Disabled, disabled,
        TAG_DONE);
    RefreshGList(shareStartGad, cxWindow, NULL, 1);
}

static BOOL
processCommand(UWORD cmd);

/*****************************************************************************/

VOID
CreateWindow(VOID)
{
    UWORD zoomSize[4];

    if (cxWindow != NULL) {
        WindowToFront(cxWindow);
        ActivateWindow(cxWindow);
        if (cxWindow->Flags & WFLG_ZOOMED) {
            ZipWindow(cxWindow);
        }
        return;
    }

    typeLabels = BCTypeCycleLabels();

    cxScreen = LockPubScreen(NULL);
    if (cxScreen == NULL) {
        return;
    }

    cxDrawInfo = GetScreenDrawInfo(cxScreen);
    if (cxDrawInfo == NULL) {
        UnlockPubScreen(NULL, cxScreen);
        cxScreen = NULL;
        return;
    }

    cxVisualInfo = GetVisualInfo(cxScreen, TAG_DONE);
    if (cxVisualInfo == NULL) {
        FreeScreenDrawInfo(cxScreen, cxDrawInfo);
        UnlockPubScreen(NULL, cxScreen);
        cxScreen = NULL;
        cxDrawInfo = NULL;
        return;
    }

    /* Keep title simple: avoid stdio sprintf dependency. */
    strcpy(windowTitle, "Bonami");

    zoomSize[0] = 0xFFFF;
    zoomSize[1] = 0xFFFF;
    zoomSize[2] = 200;
    zoomSize[3] = cxScreen->WBorTop + cxScreen->Font->ta_YSize + 1;

    cxWindow = OpenWindowTags(NULL,
        WA_Left, windowLeft,
        WA_Top, windowTop,
        WA_InnerWidth, windowWidth,
        WA_InnerHeight, windowHeight,
        WA_IDCMP, IDCMPFLAGS,
        WA_Flags, WINDOWFLAGS,
        WA_Title, windowTitle,
        WA_AutoAdjust, TRUE,
        WA_PubScreen, cxScreen,
        WA_NewLookMenus, TRUE,
        WA_Zoom, zoomSize,
        TAG_DONE);

    if (cxWindow == NULL) {
        DisposeWindow();
        return;
    }

    intuiPort = cxWindow->UserPort;
    intuiMask = 1L << intuiPort->mp_SigBit;

    if (!CreateCustomGadgets()) {
        DisposeWindow();
        return;
    }
    if (!CreateCustomMenus()) {
        DisposeWindow();
        return;
    }

    RefreshWindow(FALSE);
    updateNearbyBrowseButton();
    updateShareButton();
}

/*****************************************************************************/

VOID
DisposeWindow(VOID)
{
    if (cxWindow != NULL) {
        ClearMenuStrip(cxWindow);
        windowLeft = cxWindow->LeftEdge;
        windowTop  = cxWindow->TopEdge;
        if (!(cxWindow->Flags & WFLG_ZOOMED)) {
            windowWidth  = cxWindow->Width - cxWindow->BorderLeft -
                cxWindow->BorderRight;
            windowHeight = cxWindow->Height - cxWindow->BorderTop -
                cxWindow->BorderBottom;
        }
        CloseWindow(cxWindow);
    }

    if (cxScreen != NULL) {
        UnlockPubScreen(NULL, cxScreen);
        FreeScreenDrawInfo(cxScreen, cxDrawInfo);
    }

    FreeMenus(cxMenus);
    FreeGadgets(cxGadgets);
    FreeVisualInfo(cxVisualInfo);

    if (topazFont != NULL) {
        CloseFont(topazFont);
        topazFont = NULL;
    }

    cxMenus      = NULL;
    cxGadgets    = NULL;
    cxScreen     = NULL;
    cxWindow     = NULL;
    cxDrawInfo   = NULL;
    cxVisualInfo = NULL;
    intuiPort    = NULL;
    intuiMask    = 0;

    BCMdnsStopBrowse(&g_MDNS);
    hsPending = FALSE;
    nearbyBusy = FALSE;
    shareBusy = FALSE;
}

/*****************************************************************************/

BOOL
CreateCustomGadgets(VOID)
{
    struct NewGadget ng;
    struct Gadget *gad;
    WORD bl;
    WORD bt;
    WORD leftX;
    WORD rightX;
    WORD colGap;
    WORD leftW;
    WORD rightW;
    WORD rowY;
    WORD wBrowse;
    WORD wStop;
    WORD wResolve;
    WORD wAdvertise;
    WORD wUnshare;
    WORD wCycle;
    WORD btnPad;
    WORD vPad;

    bl = cxWindow->BorderLeft;
    bt = cxWindow->BorderTop;
    vPad = 4;
    colGap = 12;
    leftX = (WORD)(bl + 8);
    leftW = 260;
    rightX = (WORD)(leftX + leftW + colGap);
    rightW = (WORD)(WINDOW_WIDTH - (rightX - bl) - 8);
    if (rightW < 220) {
        rightW = 220;
    }

    topazFont = OpenFont(&topazAttr);
    if (topazFont == NULL) {
        return FALSE;
    }
    SetFont(cxWindow->RPort, topazFont);

    btnPad = 18; /* bevel + padding */
    wBrowse = gadTextWidth((CONST_STRPTR)STR_GAD_BROWSE, btnPad);
    wStop = gadTextWidth((CONST_STRPTR)STR_GAD_STOP, btnPad);
    wResolve = gadTextWidth((CONST_STRPTR)STR_GAD_HANDSHAKE, btnPad);
    wAdvertise = gadTextWidth((CONST_STRPTR)STR_GAD_SHARE, btnPad);
    wUnshare = gadTextWidth((CONST_STRPTR)STR_GAD_UNSHARE, btnPad);
    wCycle = cycleMaxWidth(typeLabels, 34); /* arrows + spacing */

    gad = CreateContext(&cxGadgets);
    if (gad == NULL) {
        return FALSE;
    }

    ng.ng_TextAttr   = &topazAttr;
    ng.ng_GadgetID   = 0;
    ng.ng_VisualInfo = cxVisualInfo;
    ng.ng_Flags      = 0;

    /* --- Nearby (left column) --- */
    rowY = (WORD)(bt + 20 + vPad);
    ng.ng_LeftEdge   = leftX;
    ng.ng_TopEdge    = rowY;
    ng.ng_Width      = (WORD)(wCycle > 92 ? wCycle : 92);
    ng.ng_Height     = 14;
    /* No "Kind" label; section header is enough. */
    ng.ng_GadgetText = NULL;
    ng.ng_UserData   = (APTR)CMD_NOP;
    nearbyTypeGad = CreateGadget(CYCLE_KIND, gad, &ng,
        GTCY_Labels, typeLabels,
        TAG_DONE);

    ng.ng_LeftEdge   = (WORD)(leftX + ng.ng_Width + 4);
    ng.ng_Width      = (WORD)((wBrowse > wStop) ? wBrowse : wStop);
    ng.ng_GadgetText = (STRPTR)STR_GAD_BROWSE; /* toggled at runtime */
    ng.ng_UserData   = (APTR)CMD_NEARBY_START; /* Browse/Stop */
    nearbyBrowseGad = CreateGadget(BUTTON_KIND, nearbyTypeGad, &ng, TAG_DONE);
    nearbyStopGad = nearbyBrowseGad;

    /* Move listview lower so its label isn't obscured by the top row. */
    ng.ng_LeftEdge   = leftX;
    /* One extra row: raise listview and increase height without hitting Share. */
    ng.ng_TopEdge    = (WORD)(bt + 50 + vPad);
    ng.ng_Width      = leftW;
    /* Match Handshake result list height; must not overlap Share. */
    ng.ng_Height     = 72;
    ng.ng_GadgetText = NULL;
    ng.ng_UserData   = (APTR)CMD_NEARBY_LIST;
    nearbyListGad = CreateGadget(LISTVIEW_KIND, nearbyBrowseGad, &ng,
        GTLV_Labels, &g_MDNS.bc_Nearby,
        GTLV_ShowSelected, NULL,
        LAYOUTA_Spacing, 1,
        GTLV_ScrollWidth, 16,
        TAG_DONE);

    /* --- Handshake (right top) --- */
    /* Handshake: full-width name, no 'Name' label. */
    ng.ng_LeftEdge   = rightX;
    ng.ng_TopEdge    = rowY;
    ng.ng_Width      = rightW;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = NULL;
    ng.ng_Flags      = 0;
    ng.ng_UserData   = (APTR)CMD_NOP;
    hsNameGad = CreateGadget(STRING_KIND, nearbyListGad, &ng,
        GTST_MaxChars, MDNS_MAX_NAME,
        TAG_DONE);

    /* Kind + Resolve below name. */
    ng.ng_LeftEdge   = rightX;
    ng.ng_TopEdge    = (WORD)(rowY + 18);
    ng.ng_Width      = (WORD)(wCycle > 120 ? wCycle : 120);
    ng.ng_GadgetText = NULL;
    hsTypeGad = CreateGadget(CYCLE_KIND, hsNameGad, &ng,
        GTCY_Labels, typeLabels,
        TAG_DONE);

    ng.ng_LeftEdge   = (WORD)(rightX + ng.ng_Width + 6);
    ng.ng_Width      = wResolve;
    ng.ng_GadgetText = (STRPTR)STR_GAD_HANDSHAKE;
    ng.ng_UserData   = (APTR)CMD_HANDSHAKE_GO;
    hsResolveGad = CreateGadget(BUTTON_KIND, hsTypeGad, &ng, TAG_DONE);

    ng.ng_LeftEdge   = rightX;
    ng.ng_TopEdge    = (WORD)(rowY + 36);
    ng.ng_Width      = rightW;
    ng.ng_Height     = 58;
    /* No list title: the section header is enough. */
    ng.ng_GadgetText = NULL;
    ng.ng_UserData   = (APTR)CMD_NOP;
    hsResultGad = CreateGadget(LISTVIEW_KIND, hsResolveGad, &ng,
        GTLV_Labels, &g_MDNS.bc_Results,
        GTLV_ShowSelected, NULL,
        LAYOUTA_Spacing, 1,
        GTLV_ScrollWidth, 16,
        TAG_DONE);

    /* --- Share (bottom, full width) --- */
    /* Single row: Name | Kind | Port | TXT | Share/Stop */
    ng.ng_LeftEdge   = leftX;
    ng.ng_TopEdge    = (WORD)(bt + 152);
    ng.ng_Width      = 160;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = NULL;
    ng.ng_Flags      = 0;
    ng.ng_UserData   = (APTR)CMD_NOP;
    shareNameGad = CreateGadget(STRING_KIND, hsResultGad, &ng,
        GTST_MaxChars, MDNS_MAX_NAME,
        GTST_String, "AmigaShare",
        TAG_DONE);

    ng.ng_LeftEdge   = (WORD)(leftX + 166);
    ng.ng_Width      = (WORD)(wCycle > 140 ? wCycle : 140);
    ng.ng_GadgetText = NULL;
    shareTypeGad = CreateGadget(CYCLE_KIND, shareNameGad, &ng,
        GTCY_Labels, typeLabels,
        TAG_DONE);

    ng.ng_LeftEdge   = (WORD)(leftX + 166 + ng.ng_Width + 6);
    ng.ng_Width      = 36;
    ng.ng_GadgetText = (STRPTR)STR_GAD_PORT;
    sharePortGad = CreateGadget(STRING_KIND, shareTypeGad, &ng,
        GTST_MaxChars, 6,
        GTST_String, "80",
        TAG_DONE);

    /* Small TXT field between Port and Share button. */
    ng.ng_LeftEdge   = (WORD)(ng.ng_LeftEdge + 36 + 6);
    ng.ng_GadgetText = NULL;
    ng.ng_Width      = (WORD)(WINDOW_WIDTH - (ng.ng_LeftEdge - bl) - wAdvertise - 16);
    /* Keep TXT inside the Share bevel: shave a character. */
    ng.ng_Width      = (WORD)(ng.ng_Width - gadTextWidth((CONST_STRPTR)"M", 0));
    if (ng.ng_Width < 80) {
        ng.ng_Width = 80;
    }
    shareTxtGad = CreateGadget(STRING_KIND, sharePortGad, &ng,
        GTST_MaxChars, 64,
        GTST_String, "vers=1",
        TAG_DONE);

    ng.ng_LeftEdge   = (WORD)(leftX + WINDOW_WIDTH - 8 - wAdvertise - 8);
    ng.ng_Width      = wAdvertise;
    ng.ng_GadgetText = (STRPTR)STR_GAD_SHARE; /* toggled at runtime */
    ng.ng_UserData   = (APTR)CMD_SHARE_START; /* Share/Stop */
    shareStartGad = CreateGadget(BUTTON_KIND, shareTxtGad, &ng, TAG_DONE);
    shareStopGad = shareStartGad;

    if (shareStopGad == NULL) {
        FreeGadgets(cxGadgets);
        cxGadgets = NULL;
        return FALSE;
    }

    AddGList(cxWindow, cxGadgets, -1, -1, NULL);
    RefreshGList(cxGadgets, cxWindow, NULL, -1);
    GT_RefreshWindow(cxWindow, NULL);
    return TRUE;
}

/*****************************************************************************/

static struct NewMenu NM[] =
{
    { NM_TITLE, (STRPTR)STR_MENU_PROJECT, 0, 0, 0, (APTR)CMD_NOP },
    { NM_ITEM,  "Hide", "H", 0, 0, (APTR)CMD_HIDE },
    { NM_ITEM,  "Quit", "Q", 0, 0, (APTR)CMD_QUIT },
    { NM_END,   0, 0, 0, 0, (APTR)CMD_NOP }
};

BOOL
CreateCustomMenus(VOID)
{
    cxMenus = CreateMenusA(NM, NULL);
    if (cxMenus == NULL) {
        return FALSE;
    }
    if (!LayoutMenus(cxMenus, cxVisualInfo, GTMN_NewLookMenus, TRUE, TAG_DONE)) {
        FreeMenus(cxMenus);
        cxMenus = NULL;
        return FALSE;
    }
    SetMenuStrip(cxWindow, cxMenus);
    return TRUE;
}

/*****************************************************************************/

VOID
RefreshWindow(BOOL refreshEvent)
{
    WORD bl;
    WORD bt;
    WORD leftX;
    WORD leftW;
    WORD colGap;
    WORD rightX;
    WORD rightW;
    WORD shareTop;
    WORD shareH;
    WORD shareW;

    if (cxWindow == NULL || cxDrawInfo == NULL) {
        return;
    }

    bl = cxWindow->BorderLeft;
    bt = cxWindow->BorderTop;
    colGap = 12;
    leftX = (WORD)(bl + 8);
    leftW = 260;
    rightX = (WORD)(leftX + leftW + colGap);
    rightW = (WORD)(WINDOW_WIDTH - (rightX - bl) - 8);
    if (rightW < 220) {
        rightW = 220;
    }
    shareTop = (WORD)(bt + 132);
    shareH = (WORD)(WINDOW_HEIGHT - (shareTop - bt) - 4);
    if (shareH < 44) {
        shareH = 44;
    }
    /* Match Handshake bevel right edge: rightX - 4 + rightW + 8. */
    shareW = (WORD)(rightX + rightW - leftX + 8);

    if (refreshEvent) {
        GT_BeginRefresh(cxWindow);
    }

    SetAPen(cxWindow->RPort, cxDrawInfo->dri_Pens[TEXTPEN]);
    SetBPen(cxWindow->RPort, 0);
    Move(cxWindow->RPort, leftX, bt + 10);
    Text(cxWindow->RPort, STR_LBL_NEARBY, strlen(STR_LBL_NEARBY));
    Move(cxWindow->RPort, rightX, bt + 10);
    Text(cxWindow->RPort, STR_LBL_HANDSHAKE, strlen(STR_LBL_HANDSHAKE));
    Move(cxWindow->RPort, leftX, (WORD)(shareTop + 14));
    Text(cxWindow->RPort, STR_LBL_SHARE, strlen(STR_LBL_SHARE));

    /* Simple separators to make the three sections obvious. */
    DrawBevelBox(cxWindow->RPort,
        leftX - 4, bt + 14,
        leftW + 8, 112,
        GT_VisualInfo, cxVisualInfo,
        TAG_DONE);
    DrawBevelBox(cxWindow->RPort,
        rightX - 4, bt + 14,
        rightW + 8, 112,
        GT_VisualInfo, cxVisualInfo,
        TAG_DONE);
    DrawBevelBox(cxWindow->RPort,
        leftX - 4, shareTop,
        shareW, shareH,
        GT_VisualInfo, cxVisualInfo,
        TAG_DONE);

    if (refreshEvent) {
        GT_EndRefresh(cxWindow, TRUE);
    }
}

/*****************************************************************************/

VOID
OnIntuiTick(VOID)
{
    BCMdnsDrain(&g_MDNS);
    refreshNearbyList();
    refreshResultList();

    if (hsPending) {
        if (g_MDNS.bc_Results.lh_Head != NULL &&
            g_MDNS.bc_Results.lh_Head->ln_Succ != NULL) {
            hsPending = FALSE;
        } else if (hsTries < 40) {
            STRPTR name;
            CONST_STRPTR type;
            IPTR active;

            name = ((struct StringInfo *)hsNameGad->SpecialInfo)->Buffer;
            active = 0;
            GT_GetGadgetAttrs(hsTypeGad, cxWindow, NULL,
                GTCY_Active, (IPTR)&active,
                TAG_DONE);
            type = BCTypeFromCycleIndex((LONG)active);
            BCMdnsResolve(&g_MDNS, name, type);
            hsTries++;
        } else {
            hsPending = FALSE;
            BCMdnsClearResults(&g_MDNS);
            refreshResultList();
        }
    }
}

/*****************************************************************************/

static BOOL
processCommand(UWORD cmd)
{
    CONST_STRPTR type;
    STRPTR name;
    STRPTR portStr;
    STRPTR txt;
    LONG rc;
    IPTR active;
    IPTR sel;

    switch (cmd) {
    case CMD_HIDE:
        DisposeWindow();
        break;

    case CMD_QUIT:
        return FALSE;

    case CMD_NEARBY_START:
    case CMD_NEARBY_STOP:
        /* Single stateful button: Browse when idle, Stop when browsing. */
        if (g_MDNS.bc_Browsing) {
            nearbyBusy = TRUE;
            updateNearbyBrowseButton();
            BCMdnsStopBrowse(&g_MDNS);
            nearbyBusy = FALSE;
            updateNearbyBrowseButton();
            break;
        }
        active = 0;
        GT_GetGadgetAttrs(nearbyTypeGad, cxWindow, NULL,
            GTCY_Active, (IPTR)&active,
            TAG_DONE);
        type = BCTypeFromCycleIndex((LONG)active);
        if (BCStrEqualCI((const char *)type, (const char *)MDNS_TYPE_SERVICES_DNS_SD)) {
            rc = BCMdnsStartBrowse(&g_MDNS, NULL, TRUE);
        } else {
            rc = BCMdnsStartBrowse(&g_MDNS, type, FALSE);
        }
        if (rc == MDNS_OK) {
            refreshNearbyList();
        }
        updateNearbyBrowseButton();
        break;

    case CMD_NEARBY_LIST:
        /*
         * For listview gadgets the IntuiMessage Code is not reliably the
         * selected row across all setups.  Ask GadTools for the selected index.
         */
        sel = (IPTR)~0UL;
        GT_GetGadgetAttrs(nearbyListGad, cxWindow, NULL,
            GTLV_Selected, (IPTR)&sel,
            TAG_DONE);
        if (sel != (IPTR)~0UL) {
            fillHandshakeFromNearby((UWORD)sel);
        }
        break;

    case CMD_HANDSHAKE_GO:
        name = ((struct StringInfo *)hsNameGad->SpecialInfo)->Buffer;
        active = 0;
        GT_GetGadgetAttrs(hsTypeGad, cxWindow, NULL,
            GTCY_Active, (IPTR)&active,
            TAG_DONE);
        type = BCTypeFromCycleIndex((LONG)active);
        if (name[0] == '\0') {
            break;
        }
        hsPending = TRUE;
        hsTries   = 0;
        BCMdnsClearResults(&g_MDNS);
        BCMdnsResolve(&g_MDNS, name, type);
        break;

    case CMD_SHARE_START:
    case CMD_SHARE_STOP:
        if (g_MDNS.bc_Sharing) {
            shareBusy = TRUE;
            updateShareButton();
            BCMdnsUnshare(&g_MDNS);
            shareBusy = FALSE;
            updateShareButton();
            break;
        }
        name = ((struct StringInfo *)shareNameGad->SpecialInfo)->Buffer;
        active = 0;
        GT_GetGadgetAttrs(shareTypeGad, cxWindow, NULL,
            GTCY_Active, (IPTR)&active,
            TAG_DONE);
        type = BCTypeFromCycleIndex((LONG)active);
        portStr = ((struct StringInfo *)sharePortGad->SpecialInfo)->Buffer;
        txt = ((struct StringInfo *)shareTxtGad->SpecialInfo)->Buffer;
        if (name[0] == '\0') {
            break;
        }
        shareBusy = TRUE;
        updateShareButton();
        rc = BCMdnsShare(&g_MDNS, name, type, (UWORD)parseNum(portStr), txt);
        shareBusy = FALSE;
        updateShareButton();
        break;

    default:
        break;
    }

    return TRUE;
}

/*****************************************************************************/

BOOL
ProcessIntuiMsg(struct IntuiMessage *intuiMsg)
{
    BOOL ok;
    UWORD menuNum;
    struct MenuItem *menuItem;

    imsgClass = intuiMsg->Class;
    imsgCode  = intuiMsg->Code;
    imsgGad   = intuiMsg->IAddress;
    GT_ReplyIMsg(intuiMsg);

    ok = TRUE;
    switch (imsgClass) {
    case IDCMP_CLOSEWINDOW:
        ok = processCommand(CMD_HIDE);
        break;

    case IDCMP_REFRESHWINDOW:
        RefreshWindow(TRUE);
        break;

    case IDCMP_INTUITICKS:
        OnIntuiTick();
        break;

    case IDCMP_MENUPICK:
        menuNum = imsgCode;
        while ((menuNum != MENUNULL) && ok) {
            menuItem = ItemAddress(cxMenus, menuNum);
            ok = processCommand((UWORD)(IPTR)GTMENUITEM_USERDATA(menuItem));
            if (cxMenus == NULL) {
                break;
            }
            menuNum = menuItem->NextSelect;
        }
        break;

    case IDCMP_GADGETUP:
    case IDCMP_GADGETDOWN:
        ok = processCommand((UWORD)(IPTR)imsgGad->UserData);
        break;
    }

    return ok;
}
