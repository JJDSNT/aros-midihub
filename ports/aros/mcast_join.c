/*
 * McastJoin GROUP INTERFACE-ADDRESS SECONDS
 *
 * Joins an IPv4 multicast group on the interface that has the address and
 * stays a member for that long. Used by tools/igmp-reload-hosted.sh, which
 * needs a membership it controls on a given interface.
 *
 * Returns 0, or 10 when the group could not be joined.
 */

#include <dos/dos.h>
#include <exec/libraries.h>
#include <proto/bsdsocket.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *SocketBase;

int main(int argc, char **argv)
{
    struct ip_mreq mreq;
    int status = RETURN_ERROR;
    LONG sock;

    if (argc != 4) {
        puts("Usage: McastJoin GROUP INTERFACE-ADDRESS SECONDS");
        return RETURN_ERROR;
    }
    if (!(SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4))) {
        puts("McastJoin: no bsdsocket.library");
        return RETURN_ERROR;
    }
    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr(argv[1]);
    mreq.imr_interface.s_addr = inet_addr(argv[2]);
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == 0) {
            printf("joined %s on %s\n", argv[1], argv[2]);
            Delay(50 * atoi(argv[3]));
            status = RETURN_OK;
        } else {
            printf("McastJoin: IP_ADD_MEMBERSHIP failed (%ld)\n", (long)Errno());
        }
        CloseSocket(sock);
    }
    CloseLibrary(SocketBase);
    return status;
}
