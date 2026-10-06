/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 Jaime Dias
 *
 * Small native diagnostic for the incubating Bonami AROS port.
 */

#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bonami.h>
#include <bonami/mdns.h>

struct Library *BonamiBase;

int main(void)
{
    struct Library *socketBase;
    struct TagItem status[] = {
        { MDNS_StatusRxPackets, 0 },
        { MDNS_StatusTxPackets, 0 },
        { MDNS_StatusRxAnswers, 0 },
        { TAG_DONE, 0 }
    };
    ULONG received = 0;
    ULONG sent = 0;
    ULONG answers = 0;
    LONG result;

    status[0].ti_Data = (IPTR)&received;
    status[1].ti_Data = (IPTR)&sent;
    status[2].ti_Data = (IPTR)&answers;

    BonamiBase = OpenLibrary("bonami.library", 40);
    if (BonamiBase == NULL) {
        Printf("BonamiProbe: cannot open bonami.library version 40\n");
        return RETURN_FAIL;
    }

    socketBase = OpenLibrary("bsdsocket.library", 4);
    if (socketBase == NULL) {
        Printf("BonamiProbe: bsdsocket.library is unavailable\n");
    } else {
        Printf("BonamiProbe: bsdsocket.library is available\n");
        CloseLibrary(socketBase);
    }

    result = OpenMDNSEngine();
    if (result != MDNS_OK) {
        Printf("BonamiProbe: OpenMDNSEngine failed: %ld\n", result);
        CloseLibrary(BonamiBase);
        BonamiBase = NULL;
        return RETURN_ERROR;
    }

    result = GetMDNSStatusA(status);
    if (result == MDNS_OK) {
        Printf("BonamiProbe: engine ready, rx=%lu tx=%lu answers=%lu\n",
            received, sent, answers);
    } else {
        Printf("BonamiProbe: status failed: %ld\n", result);
    }

    CloseMDNSEngine();
    CloseLibrary(BonamiBase);
    BonamiBase = NULL;
    return result == MDNS_OK ? RETURN_OK : RETURN_ERROR;
}
