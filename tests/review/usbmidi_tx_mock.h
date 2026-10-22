#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t UBYTE;
typedef uint16_t UWORD;
typedef uint32_t ULONG;
typedef int BOOL;
typedef void *APTR;
#define TRUE 1
#define FALSE 0
struct Hook { void *h_Entry,*h_SubEntry,*h_Data; };
struct Interrupt { void *is_Code,*is_Data; int pending; };
struct Task { int unused; };
struct MsgPort { struct Task *mp_SigTask; unsigned mp_SigBit; };
#define AROS_INTH1(name,type,arg) BOOL name(type arg)
#define AROS_INTFUNC_INIT
#define AROS_INTFUNC_EXIT
#define KPRINTF(level,args) ((void)0)
typedef ULONG (*camdTransmitFunc)(APTR);
static int wake;
static void Signal(struct Task *task,ULONG mask) { (void)task;(void)mask;wake=1; }
static inline void Cause(struct Interrupt *irq) { irq->pending=1; }
static void psdDoPipe(void *pipe, UBYTE *data, ULONG size);
