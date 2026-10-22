/* Review reproducer for AROS PR #1588, NM2-001.
 * Run each zero-capacity case separately under AddressSanitizer.
 * A zero-capacity output must either be rejected or left untouched.
 */
#include "netmidi2/netmidi2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    static const uint8_t payload[] = {'A', 0, 0, 0, 'B', 0, 0, 0};
    struct netmidi2_command_packet command;
    char name[8], product[8];
    char *guard;
    size_t capacity;
    int result;

    if (argc != 3 || (strcmp(argv[1], "name") && strcmp(argv[1], "product")))
        return 2;
    capacity = (size_t)strtoul(argv[2], NULL, 10);
    if (capacity > sizeof(name))
        return 2;
    guard = malloc(1);
    if (!guard)
        return 2;
    memset(&command, 0, sizeof(command));
    command.payload = payload;
    command.payload_words = 2;
    command.csd1 = 1;
    memset(name, 0x55, sizeof(name));
    memset(product, 0x55, sizeof(product));

    /* One-past an allocation is valid to pass as an empty range;
     * dereferencing it is an ASan-detectable overflow.
     */
    if (!strcmp(argv[1], "name"))
        result = netmidi2_identity(&command,
            capacity ? name : guard + 1, capacity, product, sizeof(product));
    else
        result = netmidi2_identity(&command, name, sizeof(name),
            capacity ? product : guard + 1, capacity);

    free(guard);
    if (capacity) {
        const char *output = !strcmp(argv[1], "name") ? name : product;
        const char *expected = capacity == 1 ? "" :
            !strcmp(argv[1], "name") ? "A" : "B";
        if (result != 0 || strcmp(output, expected) ||
            strcmp(!strcmp(argv[1], "name") ? product : name,
                   !strcmp(argv[1], "name") ? "B" : "A"))
            return 1;
    }
    printf("%s capacity %zu: returned %d without overflow\n", argv[1], capacity, result);
    return 0;
}
