/*
 * CAMDIdentities: the stored endpoint IDs of camd.library 43.
 *
 *   CAMDIdentities                  the store's state and every stored ID
 *   CAMDIdentities FORGET <id>      remove one, given as 32 hex digits
 *
 * A stored ID is what lets a saved route find a driver's port or a published
 * endpoint again. Forgetting one makes CAMD give its owner a new ID the next
 * time it appears; an ID that is in use cannot be forgotten.
 *
 * Returns 0, 5 when the store is unreadable or absent, 10 on an error.
 */

#include <dos/dos.h>
#include <exec/libraries.h>
#include <midi/camd.h>
#include <midi/camdendpoint.h>
#include <proto/camd.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>

struct Library *CamdBase;

static const char *state_name(ULONG state)
{
    switch (state) {
    case CAMD_IDENTITY_STORE_COMPLETE:   return "complete";
    case CAMD_IDENTITY_STORE_RECOVERED:  return "recovered from a backup copy";
    case CAMD_IDENTITY_STORE_UNREADABLE: return "unreadable: every ID is temporary, the files are untouched";
    case CAMD_IDENTITY_STORE_ABSENT:     return "absent: every ID is temporary";
    }
    return "unknown";
}

static int parse_id(const char *text, struct CAMDEndpointIDV1 *id)
{
    int i;

    if (strlen(text) != 32)
        return 0;
    memset(id, 0, sizeof(*id));
    for (i = 0; i < 32; i++) {
        char c = text[i];
        ULONG digit;

        if (c >= '0' && c <= '9') digit = (ULONG)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (ULONG)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = (ULONG)(c - 'A' + 10);
        else return 0;
        id->word[i / 8] = (id->word[i / 8] << 4) | digit;
    }
    return 1;
}

static int list(void)
{
    struct CAMDIdentityStoreInfoV1 store;
    struct CAMDStoredIdentityV1 identity;
    char what[CAMD_ENDPOINT_NAME_BYTES + 16];
    ULONG index;

    memset(&store, 0, sizeof(store));
    store.Size = sizeof(store);
    store.Version = 1;
    if (GetEndpointIdentityStore(&store) != CAMD_RESULT_OK) {
        puts("CAMDIdentities: the store cannot be read");
        return RETURN_ERROR;
    }
    printf("store: %s, %lu of %lu IDs\n", state_name(store.State),
           (unsigned long)store.Stored, (unsigned long)store.Capacity);
    for (index = 0;; index++) {
        memset(&identity, 0, sizeof(identity));
        identity.Size = sizeof(identity);
        identity.Version = 1;
        if (GetStoredEndpointIdentity(index, &identity) != CAMD_RESULT_OK)
            break;
        if (identity.Kind == CAMD_IDENTITY_STORED_BY_PATH &&
            !(identity.Flags & CAMD_STORED_IDENTITY_PROVIDER))
            sprintf(what, "%s.%lu", identity.Name, (unsigned long)identity.Port);
        else
            strcpy(what, identity.Name);
        printf("%08lx%08lx%08lx%08lx  %-9s %s%s\n",
               (unsigned long)identity.ID.word[0], (unsigned long)identity.ID.word[1],
               (unsigned long)identity.ID.word[2], (unsigned long)identity.ID.word[3],
               identity.Kind == CAMD_IDENTITY_STORED_BY_PATH
                   ? ((identity.Flags & CAMD_STORED_IDENTITY_PROVIDER) ? "driver" : "port")
                   : ((identity.Flags & CAMD_STORED_IDENTITY_PROVIDER) ? "publisher" : "endpoint"),
               what, (identity.Flags & CAMD_STORED_IDENTITY_IN_USE) ? "  (in use)" : "");
    }
    return store.State == CAMD_IDENTITY_STORE_COMPLETE ||
           store.State == CAMD_IDENTITY_STORE_RECOVERED ? RETURN_OK : RETURN_WARN;
}

static int forget(const char *text)
{
    struct CAMDEndpointIDV1 id;
    LONG result;

    if (!parse_id(text, &id)) {
        puts("CAMDIdentities: an ID is 32 hex digits");
        return RETURN_ERROR;
    }
    result = ForgetEndpointIdentity(&id);
    switch (result) {
    case CAMD_RESULT_OK:
        puts("forgotten");
        return RETURN_OK;
    case CAMD_RESULT_BUSY:
        puts("CAMDIdentities: that ID is in use");
        break;
    case CAMD_RESULT_STALE:
        puts("CAMDIdentities: no such ID is stored");
        break;
    case CAMD_RESULT_WRONG_STATE:
        puts("CAMDIdentities: the store is unreadable; nothing was changed");
        break;
    case CAMD_RESULT_FAILED:
        puts("CAMDIdentities: the store could not be written; the ID stays");
        break;
    default:
        printf("CAMDIdentities: refused (%ld)\n", (long)result);
        break;
    }
    return RETURN_ERROR;
}

int main(int argc, char **argv)
{
    int status = RETURN_ERROR;

    if (!(argc == 1 || (argc == 3 && !stricmp(argv[1], "FORGET")))) {
        puts("Usage: CAMDIdentities [FORGET <id>]");
        return RETURN_ERROR;
    }
    CamdBase = OpenLibrary((CONST_STRPTR)"camd.library", 43);
    if (!CamdBase) {
        puts("CAMDIdentities: needs camd.library 43");
        return RETURN_ERROR;
    }
    status = argc == 1 ? list() : forget(argv[2]);
    CloseLibrary(CamdBase);
    return status;
}
