/* Native regression cases from #1734's maintainer review. No driver needed. */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/camd.h>
#include <midi/camd.h>
#include <stdio.h>
#include <string.h>
struct Library *CamdBase;
static int failures;
#define CHECK(e,s) do { if(e) printf("PASS %s\n",s); else { printf("FAIL %s\n",s); failures++; } } while(0)
static struct MidiNode *make_node(ULONG bytes)
{
    struct TagItem tags[]={{MIDI_MsgQueue,16},{bytes?MIDI_SysExSize:TAG_IGNORE,bytes},{TAG_DONE,0}};
    return CreateMidiA(tags);
}
static struct MidiLink *link_node(struct MidiNode *node,LONG type,const char *name)
{
    struct TagItem tags[]={{MLINK_Location,(IPTR)name},{TAG_DONE,0}};
    return AddMidiLinkA(node,type,tags);
}
static void retry(BOOL flush)
{
    UBYTE first[]={0xf0,1,2,3,4,0xf7}, next[]={0xf0,5,6,0xf7}, got[16];
    MidiMsg msg;
    struct MidiNode *a=make_node(0),*b=make_node(8);
    struct MidiLink *send=link_node(a,MLTYPE_Sender,"review.retry"),*recv=link_node(b,MLTYPE_Receiver,"review.retry");
    PutSysEx(send,first);
    PutSysEx(send,next);
    CHECK((GetMidiErr(b)&CMEF_SysExFull)!=0,"busy queue reports SysExFull");
    if(flush) FlushMidi(b);
    else { CHECK(GetMidi(b,&msg),"first SysEx notification remains"); CHECK(GetSysEx(b,got,sizeof(got))==sizeof(first),"first SysEx is readable"); }
    PutSysEx(send,next);
    CHECK(GetMidi(b,&msg) && msg.mm_Status==0xf0,flush?"FlushMidi allows a new SysEx":"next F0 ends discard after busy PutSysEx");
    CHECK(GetSysEx(b,got,sizeof(got))==sizeof(next) && !memcmp(got,next,sizeof(next)),"new SysEx is complete and exact");
    CHECK(GetMidiErr(b)==0,"retry raises no new error");
    RemoveMidiLink(recv);RemoveMidiLink(send);DeleteMidi(b);DeleteMidi(a);
}
static void ignored(BOOL masked)
{
    UBYTE bytes[]={0xf0,1,2,3,4,0xf7};
    MidiMsg msg;
    struct MidiNode *a=make_node(0),*b=make_node(masked?4:0);
    struct MidiLink *send=link_node(a,MLTYPE_Sender,"review.ignore"),*recv=link_node(b,MLTYPE_Receiver,"review.ignore");
    if(masked){struct TagItem tags[]={{MLINK_EventMask,(IPTR)~CMF_SysEx},{TAG_DONE,0}};SetMidiLinkAttrsA(recv,tags);}
    PutSysEx(send,bytes);
    CHECK(GetMidiErr(b)==0,masked?"masked SysEx raises no TooBig":"node without SysEx queue raises no TooBig");
    CHECK(!GetMidi(b,&msg),"ignored SysEx has no notification");
    PutMidi(send,0x903c6400UL);
    CHECK(GetMidi(b,&msg) && msg.mm_Status==0x90,"ordinary MIDI remains usable");
    RemoveMidiLink(recv);RemoveMidiLink(send);DeleteMidi(b);DeleteMidi(a);
}
int main(void)
{
    CamdBase=OpenLibrary((CONST_STRPTR)"camd.library",0);
    if(!CamdBase)return 20;
    retry(FALSE);retry(TRUE);ignored(FALSE);ignored(TRUE);
    CloseLibrary(CamdBase);
    printf("SYSEX-REVIEW: %d failures\n",failures);
    return failures?20:0;
}
