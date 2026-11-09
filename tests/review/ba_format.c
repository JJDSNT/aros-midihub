#include <stdio.h>
#include <string.h>

#include "ba_format.h"

static int failures;

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); failures++; \
} } while (0)

static void
address(UWORD family, const UBYTE *bytes, ULONG link, const char *expected)
{
    struct MDNSDiscoveryAddress item = {0};
    char out[80];
    LONG length = family == MDNS_DISCOVERY_ADDR_IPV4 ? 4 : 16;

    item.da_Family = family;
    memcpy(item.da_Address, bytes, length);
    CHECK(BAFormatAddress(&item, link, out, sizeof(out)));
    CHECK(strcmp(out, expected) == 0);
}

static void
addresses(void)
{
    static const UBYTE ipv4[] = {192, 0, 2, 17};
    static const UBYTE global[] =
        {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    static const UBYTE linklocal[] =
        {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    static const UBYTE zero[16] = {0};
    static const UBYTE single[] =
        {0x20, 1, 0x0d, 0xb8, 0, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5};
    static const UBYTE tie[] =
        {0x20, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 2, 0, 3};
    struct MDNSDiscoveryAddress bad = {0};
    char shortOut[4];

    address(MDNS_DISCOVERY_ADDR_IPV4, ipv4, 3, "192.0.2.17");
    address(MDNS_DISCOVERY_ADDR_IPV6, global, 7, "2001:db8::1");
    address(MDNS_DISCOVERY_ADDR_IPV6, linklocal, 7, "fe80::2%7");
    address(MDNS_DISCOVERY_ADDR_IPV6, zero, 0, "::");
    address(MDNS_DISCOVERY_ADDR_IPV6, single, 0,
        "2001:db8:0:1:2:3:4:5");
    address(MDNS_DISCOVERY_ADDR_IPV6, tie, 0, "2001::1:0:0:2:3");
    bad.da_Family = 99;
    CHECK(!BAFormatAddress(&bad, 0, shortOut, sizeof(shortOut)));
    bad.da_Family = MDNS_DISCOVERY_ADDR_IPV4;
    memcpy(bad.da_Address, ipv4, sizeof(ipv4));
    CHECK(!BAFormatAddress(&bad, 0, shortOut, sizeof(shortOut)));
}

static void
names(void)
{
    static const UBYTE host[] =
        {4, 'h', 'o', 's', 't', 5, 'l', 'o', 'c', 'a', 'l', 0};
    static const UBYTE escaped[] =
        {3, 'a', '.', 'b', 3, 'c', '\\', 'd', 0};
    static const UBYTE root[] = {0};
    static const UBYTE missingRoot[] = {1, 'x'};
    static const UBYTE pointer[] = {0xc0, 0};
    static const UBYTE trailing[] = {0, 0};
    char out[64];

    CHECK(BAFormatWireName(host, sizeof(host), out, sizeof(out)));
    CHECK(strcmp(out, "host.local.") == 0);
    CHECK(BAFormatWireName(escaped, sizeof(escaped), out, sizeof(out)));
    CHECK(strcmp(out, "a\\.b.c\\\\d.") == 0);
    CHECK(BAFormatWireName(root, sizeof(root), out, sizeof(out)));
    CHECK(strcmp(out, ".") == 0);
    CHECK(!BAFormatWireName(missingRoot, sizeof(missingRoot), out, sizeof(out)));
    CHECK(!BAFormatWireName(pointer, sizeof(pointer), out, sizeof(out)));
    CHECK(!BAFormatWireName(trailing, sizeof(trailing), out, sizeof(out)));
    CHECK(!BAFormatWireName(host, sizeof(host), out, 5));
}

int
main(void)
{
    addresses();
    names();
    printf("ba_format: %s (%d failures)\n", failures ? "FAIL" : "PASS",
        failures);
    return failures != 0;
}
