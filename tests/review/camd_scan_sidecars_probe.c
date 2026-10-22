#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/camd.h>
#include <midi/camd.h>
#include <stdio.h>
#include <string.h>

struct Library *CamdBase;
static int checks, failures;
#define CHECK(e, label) do { ++checks; printf("%s %s\n", (e) ? "PASS" : (++failures, "FAIL"), label); } while (0)

static BOOL dummy(CONST_STRPTR path)
{
    BPTR file = Open(path, MODE_NEWFILE);
    LONG written;
    if (!file) return FALSE;
    written = Write(file, "not a driver", 12);
    Close(file);
    return written == 12;
}

static BOOL copy_driver(void)
{
    static UBYTE buffer[4096];
    BPTR from = Open((CONST_STRPTR)"SYS:watch-debugdriver", MODE_OLDFILE);
    BPTR to;
    LONG got;
    BOOL ok = TRUE;
    if (!from) return FALSE;
    to = Open((CONST_STRPTR)"DEVS:Midi/debugdriver", MODE_NEWFILE);
    if (!to) { Close(from); return FALSE; }
    while ((got = Read(from, buffer, sizeof(buffer))) > 0)
        if (Write(to, buffer, got) != got) ok = FALSE;
    Close(to);
    Close(from);
    return ok && got == 0;
}

int main(void)
{
    static CONST_STRPTR ignored[] = {
        (CONST_STRPTR)"DEVS:Midi/device.info", (CONST_STRPTR)"DEVS:Midi/device.dbg",
        (CONST_STRPTR)"DEVS:Midi/upper.INFO", (CONST_STRPTR)"DEVS:Midi/mixed.DbG",
        (CONST_STRPTR)"DEVS:Midi/.info"
    };
    static CONST_STRPTR candidates[] = {
        (CONST_STRPTR)"DEVS:Midi/notdriver", (CONST_STRPTR)"DEVS:Midi/device.info.driver",
        (CONST_STRPTR)"DEVS:Midi/device.dbg.bak"
    };
    unsigned int i;
    APTR lock;
    struct MidiCluster *cluster;
    BPTR directory;

    CamdBase = OpenLibrary((CONST_STRPTR)"camd.library", 0);
    if (!CamdBase) return 20;
    CHECK(RethinkCAMD() == 0, "empty directory succeeds");
    for (i = 0; i < sizeof(ignored)/sizeof(ignored[0]); ++i) {
        CHECK(dummy(ignored[i]), "create icon/debug fixture");
        CHECK(RethinkCAMD() == 0, (const char *)ignored[i]);
    }
    directory = CreateDir((CONST_STRPTR)"DEVS:Midi/subdir");
    if (directory) UnLock(directory);
    CHECK(directory != BNULL && RethinkCAMD() == 0, "subdirectories remain ignored");
    for (i = 0; i < sizeof(candidates)/sizeof(candidates[0]); ++i) {
        CHECK(dummy(candidates[i]), "create invalid candidate");
        CHECK(RethinkCAMD() == -1, (const char *)candidates[i]);
        DeleteFile(candidates[i]);
        CHECK(RethinkCAMD() == 0, "removing invalid candidate restores success");
    }
    CHECK(copy_driver(), "install actual driver beside sidecars");
    CHECK(RethinkCAMD() == 0, "valid driver loads with icons/debug files present");
    lock = LockCAMD(CD_Linkages);
    cluster = FindCluster((STRPTR)"debugdriver.in.0");
    CHECK(cluster != NULL, "actual driver publishes its port");
    UnlockCAMD(lock);
    CHECK(RethinkCAMD() == 0, "repeat scan succeeds without duplicate load");
    for (i = 0; i < sizeof(ignored)/sizeof(ignored[0]); ++i) DeleteFile(ignored[i]);
    DeleteFile((CONST_STRPTR)"DEVS:Midi/subdir");
    DeleteFile((CONST_STRPTR)"DEVS:Midi/debugdriver");
    CloseLibrary(CamdBase);
    printf("SIDECAR-SCAN %d checks, %d failures\n", checks, failures);
    return failures ? 20 : 0;
}
