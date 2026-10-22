/* Valid MIDI TXT fields fit on the wire but exceed Bonami's escaped form. */
#include <bonami/mdns.h>
#include "mdns_dns.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char endpoint_key[] = "UMPEndpointName=";
    const char product_key[] = "ProductInstanceId=";
    UBYTE endpoint[sizeof(endpoint_key) - 1 + 98];
    UBYTE product[sizeof(product_key) - 1 + 42];
    char text[MDNS_MAX_TXT + 1] = {0};
    BOOL first, second;

    memcpy(endpoint, endpoint_key, sizeof(endpoint_key) - 1);
    memset(endpoint + sizeof(endpoint_key) - 1, '\\', 98);
    memcpy(product, product_key, sizeof(product_key) - 1);
    memset(product + sizeof(product_key) - 1, '\\', 42);
    first = mdnsTxtAppend(text, sizeof(text), endpoint, sizeof(endpoint));
    second = mdnsTxtAppend(text, sizeof(text), product, sizeof(product));
    printf("wire bytes=%zu, escaped bytes needed=%zu, local capacity=%u, appended=%d/%d\n",
        sizeof(endpoint) + sizeof(product) + 2,
        sizeof(endpoint) + sizeof(product) + 98 + 42 + 1,
        MDNS_MAX_TXT, first, second);
    return !first || !second;
}
