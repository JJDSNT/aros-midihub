struct Source { UBYTE *bytes; size_t length,at; };
static UBYTE *received[16];
static size_t received_length[16],received_capacity[16];
static int failures;
static ULONG transmit(APTR data)
{
    struct Source *s=data;
    return s->at<s->length?s->bytes[s->at++]:0x100;
}
static void psdDoPipe(void *pipe, UBYTE *bytes, ULONG size)
{
    unsigned i;
    (void)pipe;
    if(size>8192 || size%4) abort();
    for(i=0;i<size;i+=4){
        unsigned port=bytes[i]>>4,cin=bytes[i]&15;
        unsigned n=cin==4||cin==7||cin==9?3:cin==6?2:cin==5?1:0;
        if(received_length[port]+n>received_capacity[port])abort();
        memcpy(received[port]+received_length[port],bytes+i+1,n);
        received_length[port]+=n;
    }
}
static void check(int ok,const char *label)
{printf("%s %s\n",ok?"PASS":"FAIL",label);failures+=!ok;}
static void run(size_t length,BOOL notes,BOOL detached,BOOL fill,BOOL two)
{
    struct CAMDPortSet ports={0};
    struct NepClassHid nch;
    struct Source source[16]={{0}};
    UBYTE usb[8192];
    struct Task task={0};struct MsgPort mp={&task,3};
    unsigned i,step;
    char label[100];
    memset(received_length,0,sizeof(received_length));wake=0;
    nch.nch_Ports=&ports;nch.nch_EPOutBuf=usb;nch.nch_EPOutPipe=NULL;
    for(i=0;i<(two?2u:1u);i++){
        struct CAMDAdapter *ca=&ports.cps_Adapters[i];
        size_t j;
        source[i].length=length+i*3;source[i].bytes=malloc(source[i].length);
        for(j=0;j<source[i].length;j++)source[i].bytes[j]=notes?(j%3==0?0x90:(UBYTE)(j&0x7f)):(UBYTE)(j&0x7f);
        if(!notes){source[i].bytes[0]=0xf0;source[i].bytes[source[i].length-1]=0xf7;}
        received_capacity[i]=source[i].length;received[i]=malloc(source[i].length);
        ca->ca_IsOpen=TRUE;ca->ca_PortNum=i;ca->ca_TXFunc=(APTR)transmit;ca->ca_UserData=&source[i];
        ca->ca_TXBufSize=4096;ca->ca_TXBuffer=calloc(4096,1);ca->ca_MsgPort=detached?NULL:&mp;
        ca->ca_CAMDTXFunc.pending=1;
    }
    if(fill)for(i=0;i<80;i++)GetFromCAMD(&ports.cps_Adapters[0]);
    for(step=0;step<10000;step++){
        BOOL active=FALSE;
        for(i=0;i<(two?2u:1u);i++)if(ports.cps_Adapters[i].ca_CAMDTXFunc.pending){
            ports.cps_Adapters[i].ca_CAMDTXFunc.pending=0;GetFromCAMD(&ports.cps_Adapters[i]);active=TRUE;
        }
        if(wake){wake=0;nParseMidiOut(&nch);active=TRUE;}
        if(!active)break;
    }
    snprintf(label,sizeof(label),"%s %lu bytes%s%s%s",notes?"notes":"SysEx",(unsigned long)length,detached?" detached":" attached",fill?" full ring":"",two?" two ports":"");
    for(i=0;i<(two?2u:1u);i++){
        check(step<10000 && source[i].at==source[i].length &&
              (detached || (received_length[i]==source[i].length && !memcmp(source[i].bytes,received[i],source[i].length))),label);
        free(source[i].bytes);free(received[i]);free(ports.cps_Adapters[i].ca_TXBuffer);
    }
}
int main(void)
{
    run(99,FALSE,FALSE,FALSE,FALSE);run(100,FALSE,FALSE,FALSE,FALSE);
    run(101,FALSE,FALSE,FALSE,FALSE);run(4096,FALSE,FALSE,FALSE,FALSE);
    run(65536,FALSE,FALSE,FALSE,FALSE);run(153,TRUE,FALSE,FALSE,FALSE);
    run(12000,FALSE,FALSE,TRUE,FALSE);run(8192,FALSE,FALSE,FALSE,TRUE);
    run(65536,FALSE,TRUE,FALSE,FALSE);
    printf("USB-TX: %d failures\n",failures);return failures?1:0;
}
