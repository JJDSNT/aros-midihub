#include <proto/exec.h>
#include <dos/dos.h>
#include <exec/ports.h>
#include <stdio.h>

int main(void)
{
    struct MsgPort *port;
    Forbid();
    port = FindPort((STRPTR)"BonAmi");
    if (port) Signal(port->mp_SigTask, SIGBREAKF_CTRL_C);
    Permit();
    puts(port ? "mdnsd signalled" : "mdnsd absent");
    return port ? 0 : 5;
}
