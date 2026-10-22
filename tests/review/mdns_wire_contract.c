/* Review the exact #1591 DNS helpers, with host types only. */
#include "mdns_dns.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    if (!strcmp(argv[1], "txt-spaces")) {
        UBYTE item[255];
        LONG position = 0;
        LONG length = mdnsTxtNext("UMPEndpointName=Studio\\ Piano ProductInstanceId=unit1",
                                  &position, item, sizeof(item));
        const char expected[] = "UMPEndpointName=Studio Piano";
        printf("wire TXT item: %.*s\n", (int)length, item);
        return length != (LONG)strlen(expected) || memcmp(item, expected, length);
    }
    if (!strcmp(argv[1], "literal-dot")) {
        /* The instance is one valid label containing a dot, not two labels. */
        const UBYTE wire[] = {8, 'S','t','u','d','i','o','.','1',
            6, '_','m','i','d','i','2', 4, '_','u','d','p', 5, 'l','o','c','a','l', 0};
        UBYTE encoded[256];
        char name[256];
        LONG length;
        if (mdnsWireToName(wire, sizeof(wire), 0, wire, sizeof(wire), name, sizeof(name)) < 0 ||
            mdnsNameToWire(name, encoded, sizeof(encoded), &length))
            return 2;
        printf("decoded='%s'; original first label=%u, re-encoded first label=%u\n",
               name, wire[0], encoded[0]);
        return length != (LONG)sizeof(wire) || memcmp(wire, encoded, sizeof(wire)) != 0;
    }
    return 2;
}
