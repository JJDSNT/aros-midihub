/* Real CAMD/class/driver and soft interrupts, with a simulated USB sink.
 * The generated parser body below is copied unchanged from the tested tree. */
#include <exec/types.h>
#include <exec/ports.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/camd.h>
#include <midi/camd.h>
#include "camdusbmidi.h"
#include <stdio.h>
#include <string.h>
struct Library *CamdBase;
static struct CAMDPortSet *ports;
static volatile LONG ready;
static ULONG worker_id;
static volatile ULONG received,bad;
static UBYTE expected[65536];
static ULONG expected_size;
static void fake_usb(void *pipe,UBYTE *bytes,ULONG size)
{
    ULONG i,j;
    (void)pipe;
    for(i=0;i<size;i+=4){
        UBYTE cin=bytes[i]&15;
        ULONG n=cin==4||cin==7?3:cin==6?2:cin==5?1:0;
        for(j=0;j<n;j++){
            if(received>=expected_size || bytes[i+1+j]!=expected[received])bad++;
            received++;
        }
    }
}
#define KPRINTF(level,args) ((void)0)
#define psdDoPipe fake_usb
#include "usb_parser_body.h"
#undef psdDoPipe
static void consumer(void)
{
    static UBYTE buffer[8192];
    struct NepClassHid nch={0};
    struct MsgPort *mp=CreateMsgPort();
    ULONG sig;
    worker_id=GetETaskID(FindTask(NULL));
    if(!mp){ready=-1;return;}
    nch.nch_EPOutBuf=buffer;nch.nch_Ports=ports;
    Disable();ports->cps_Adapters[0].ca_MsgPort=mp;Enable();
    ready=1;
    do{
        sig=Wait((1UL<<mp->mp_SigBit)|SIGBREAKF_CTRL_C);
        nParseMidiOut(&nch);
    }while(!(sig&SIGBREAKF_CTRL_C));
    Disable();ports->cps_Adapters[0].ca_MsgPort=NULL;Enable();
    DeleteMsgPort(mp);ready=-1;
}
int main(void)
{
    struct Library *cls;
    struct MidiNode *node;
    struct MidiLink *sender;
    struct Process *worker;
    ULONG sizes[]={101,4096,65536},i,j,wait;
    int failures=0;
    struct TagItem nt[]={{MIDI_MsgQueue,16},{TAG_DONE,0}};
    struct TagItem lt[]={{MLINK_Location,(IPTR)"usbmidi.out.0"},{TAG_DONE,0}};
    CamdBase=OpenLibrary((CONST_STRPTR)"camd.library",0);
    cls=OpenLibrary((CONST_STRPTR)"camdusbmidi.class",0);
    if(!CamdBase||!cls)return 20;
    node=CreateMidiA(nt);sender=node?AddMidiLinkA(node,MLTYPE_Sender,lt):NULL;
    if(!sender)return 20;
    ports=(struct CAMDPortSet *)((struct NepHidBase *)cls)->nh_PortSets.lh_Head;
    if(!ports->cps_Node.ln_Succ || !ports->cps_Adapters[0].ca_IsOpen)return 20;
    worker=CreateNewProcTags(NP_Entry,(IPTR)consumer,NP_Name,(IPTR)"USB simulated sink",NP_StackSize,65536,TAG_DONE);
    if(!worker)return 20;
    for(wait=0;wait<100 && !ready;wait++)Delay(1);
    if(ready!=1)return 20;
    for(i=0;i<3;i++){
        received=bad=0;expected_size=sizes[i];
        for(j=0;j<expected_size;j++)expected[j]=j&0x7f;
        expected[0]=0xf0;expected[expected_size-1]=0xf7;
        printf("START SysEx %lu\n",(unsigned long)expected_size);fflush(stdout);
        PutSysEx(sender,expected);
        for(wait=0;wait<250 && received<expected_size;wait++)Delay(1);
        if(received==expected_size && !bad)printf("PASS SysEx %lu completed exactly\n",(unsigned long)expected_size);
        else{printf("FAIL got %lu of %lu, bad %lu\n",(unsigned long)received,(unsigned long)expected_size,(unsigned long)bad);failures++;}
    }
    Signal(&worker->pr_Task,SIGBREAKF_CTRL_C);
    while(ready!=-1)Delay(1);
    while(FindTaskByPID(worker_id)!=NULL)Delay(1);
    RemoveMidiLink(sender);DeleteMidi(node);CloseLibrary(cls);CloseLibrary(CamdBase);
    printf("USB-NATIVE-TX: %d failures\n",failures);return failures?20:0;
}
