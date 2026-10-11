/*
 * NetReload: asks AROSTCP to reconfigure in place, as its preferences
 * program does: it stops the services, reads the interface database again,
 * removes interfaces that are gone, adds new ones and starts the services.
 * Used by tools/igmp-reload-hosted.sh.
 *
 * Returns 0, or 10 when the stack is not running or refused.
 */

#include <bsdsocket/netcontrol.h>
#include <dos/dos.h>
#include <exec/ports.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    struct NetControlMsg msg;
    struct MsgPort *reply, *port;
    int status = RETURN_ERROR;

    if (!(reply = CreateMsgPort())) return RETURN_ERROR;
    memset(&msg, 0, sizeof(msg));
    msg.ncm_Msg.mn_ReplyPort = reply;
    msg.ncm_Msg.mn_Length = sizeof(msg);
    msg.ncm_Command = NCMD_RELOAD;
    msg.ncm_Result = -1;
    Forbid();
    port = FindPort((CONST_STRPTR)AROSTCP_CTRLPORT_NAME);
    if (port) PutMsg(port, &msg.ncm_Msg);
    Permit();
    if (port) {
        WaitPort(reply);
        GetMsg(reply);
        printf("reload: %ld\n", (long)msg.ncm_Result);
        if (msg.ncm_Result == 0) status = RETURN_OK;
    } else {
        puts("NetReload: AROSTCP is not running");
    }
    DeleteMsgPort(reply);
    return status;
}
