/* Review reproducer for AROS PR #1588: a malformed identity must not
 * establish a client session. Exit 1 demonstrates the defect.
 */
#include "netmidi2/netmidi2.h"
#include <stdio.h>

int main(void)
{
    struct netmidi2_session client;
    struct netmidi2_writer invite, malformed, reply;
    uint8_t invite_bytes[NETMIDI2_MAX_PACKET];
    uint8_t malformed_bytes[NETMIDI2_MAX_PACKET];
    uint8_t reply_bytes[NETMIDI2_MAX_PACKET];
    struct netmidi2_command_packet command;
    char name[8], product[8];
    int rejected, result;

    netmidi2_session_init(&client, 0, "Client", "client-1", NULL, NULL);
    if (netmidi2_writer_init(&invite, invite_bytes, sizeof(invite_bytes)) ||
        netmidi2_invite(&client, 1000, &invite) ||
        netmidi2_writer_init(&malformed, malformed_bytes, sizeof(malformed_bytes)) ||
        netmidi2_writer_init(&reply, reply_bytes, sizeof(reply_bytes)))
        return 2;
    /* Name length claims one word; the command carries zero words. */
    if (netmidi2_add(&malformed, NETMIDI2_INVITATION_ACCEPTED, 1, 0, NULL, 0) ||
        netmidi2_parse(malformed.data, malformed.length, &command, 1) != 1)
        return 2;
    rejected = netmidi2_identity(&command, name, sizeof(name), product, sizeof(product));
    if (rejected != -1)
        return 2;
    result = netmidi2_receive(&client, malformed.data, malformed.length, 1001, &reply);
    printf("identity parser=%d; receive=%d; client established=%d\n", rejected, result,
        client.state == NETMIDI2_ESTABLISHED);
    return client.state == NETMIDI2_ESTABLISHED ? 1 : 0;
}
