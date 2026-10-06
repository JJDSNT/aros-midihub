/* MIDIHub preferences shell. Pages and logic live in MHPrefsClass, following
 * the Bluetooth Preferences and Trident application structure. */
#include <dos/dos.h>
#include <exec/libraries.h>
#include <intuition/classusr.h>
#include <libraries/mui.h>

#include <proto/camd.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>

#include "MIDIHubPrefsClass.h"

#pragma GCC diagnostic ignored "-Wint-conversion"
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
#pragma GCC diagnostic ignored "-Wpointer-sign"

#define VERSION "$VER: MIDIHub 0.2 (05.10.2026)"

struct Library *CamdBase;
struct Library *MUIMasterBase;
struct MUI_CustomClass *MHPrefsClass;

static void close_libraries(void)
{
    if (MHPrefsClass) MUI_DeleteCustomClass(MHPrefsClass);
    if (MUIMasterBase) CloseLibrary(MUIMasterBase);
    if (CamdBase) CloseLibrary(CamdBase);
}

int main(void)
{
    Object *app = NULL, *win, *prefs;
    IPTR args[1] = { 0 };
    struct RDArgs *rdargs = ReadArgs((CONST_STRPTR)"PAGE/K", args, NULL);
    ULONG signals = 0;
    IPTR return_id;
    int result = RETURN_FAIL;

    CamdBase = OpenLibrary((CONST_STRPTR)"camd.library", 40);
    MUIMasterBase = OpenLibrary(MUIMASTER_NAME, MUIMASTER_VMIN);
    if (!CamdBase || !MUIMasterBase)
        goto done;
    MHPrefsClass = MUI_CreateCustomClass(NULL, MUIC_Group, NULL,
                                         sizeof(struct MHPrefsData),
                                         MHPrefsDispatcher);
    if (!MHPrefsClass)
        goto done;

    app = ApplicationObject,
        MUIA_Application_Title, "MIDIHub",
        MUIA_Application_Version, VERSION,
        MUIA_Application_Copyright, "(c) 2026 The AROS Development Team",
        MUIA_Application_Author, "The AROS Development Team",
        MUIA_Application_Description, "MIDI environment preferences",
        MUIA_Application_Base, "MIDIHUBPREFS",
        MUIA_Application_SingleTask, TRUE,
        SubWindow, win = WindowObject,
            MUIA_Window_Title, "MIDIHub",
            MUIA_Window_ID, MAKE_ID('M','H','P','R'),
            MUIA_Window_SizeGadget, TRUE,
            WindowContents, prefs = NewObject(MHPrefsClass->mcc_Class, NULL, TAG_END),
            End,
        End;
    if (!app)
        goto done;

    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             app, 2, MUIM_Application_ReturnID,
             MUIV_Application_ReturnID_Quit);

    set(win, MUIA_Window_Open, TRUE);
    /* PAGE=Routing, Diagnostics, ...: open on that page */
    if (args[0])
        DoMethod(prefs, MUIM_MHP_ShowPage, args[0]);
    for (;;) {
        return_id = DoMethod(app, MUIM_Application_NewInput, &signals);
        if (return_id == MUIV_Application_ReturnID_Quit)
            break;
        if (signals) {
            signals = Wait(signals | SIGBREAKF_CTRL_C);
            if (signals & SIGBREAKF_CTRL_C)
                break;
        }
    }
    set(win, MUIA_Window_Open, FALSE);
    MUI_DisposeObject(app);
    app = NULL;
    result = RETURN_OK;
done:
    if (app) MUI_DisposeObject(app);
    if (rdargs) FreeArgs(rdargs);
    close_libraries();
    return result;
}
