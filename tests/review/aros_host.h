/* Minimal host types for deterministic discovery/codec review tests.
 * This does not model AROS scheduling, interrupt exclusion, or library ABI.
 */
#ifndef REVIEW_AROS_HOST_H
#define REVIEW_AROS_HOST_H
#include <stdint.h>
#include <stddef.h>
typedef uint8_t UBYTE;
typedef uint16_t UWORD;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef int BOOL;
typedef uintptr_t IPTR;
typedef void *APTR;
typedef const void *CONST_APTR;
typedef char *STRPTR;
typedef const char *CONST_STRPTR;
struct Node {
    struct Node *ln_Succ, *ln_Pred;
    uint8_t ln_Type;
    int8_t ln_Pri;
    char *ln_Name;
};
struct List { struct Node *lh_Head, *lh_Tail, *lh_TailPred; };
struct Task { int unused; };
struct MsgPort { int unused; };
struct Message {
    struct Node mn_Node;
    struct MsgPort *mn_ReplyPort;
    UWORD mn_Length;
};
struct Library { int unused; };
struct TagItem { IPTR ti_Tag, ti_Data; };
struct Hook;
typedef IPTR (*HOOKFUNC)(struct Hook *, APTR, APTR);
struct Hook { HOOKFUNC h_Entry; APTR h_SubEntry, h_Data; };
#define TAG_DONE 0
#define TAG_IGNORE 1
#define TAG_USER 0x80000000u
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define TRUE 1
#define FALSE 0
#define AROS_UFH3(result, name, a, b, c) result name(a, b, c)
#define AROS_UFHA(type, name, reg) type name
#define AROS_USERFUNC_INIT
#define AROS_USERFUNC_EXIT
#define AROS_ASMSYMNAME(name) name
#endif
