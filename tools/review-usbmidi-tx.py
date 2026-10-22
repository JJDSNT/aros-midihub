#!/usr/bin/env python3
"""Execute exact USB MIDI TX/parser function bodies with host USB/Exec shims."""
import argparse,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--tree',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
base=a.tree/'rom/usb/classes/camdmidi'
def block(text,needle):
    begin=text.index(needle);start=text.index('{',begin);depth=1;i=start+1
    while depth:
        depth+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[begin:i]
driver=(base/'camd/poseidonusb.c').read_text();cls=(base/'camdusbmidi.class.c').read_text();header=(base/'camdusbmidi.h').read_text()
code='#include "'+str((Path(__file__).resolve().parent.parent/'tests/review/usbmidi_tx_mock.h'))+'"\n'
code+=block(header,'struct CAMDAdapter\n')+';\n'
code+='struct CAMDPortSet {struct CAMDAdapter cps_Adapters[16];};\nstruct NepClassHid {UBYTE *nch_EPOutBuf; void *nch_EPOutPipe; struct CAMDPortSet *nch_Ports;};\n'
code+=block(driver,'static AROS_INTH1(GetFromCAMD')+'\n'
code+=block(cls,'void nParseMidiOut(struct NepClassHid *nch)\n')+'\n'
code+='#include "'+str((Path(__file__).resolve().parent.parent/'tests/review/usbmidi_tx_cases.c'))+'"\n'
src=a.out/'tx.c';src.write_text(code);binary=a.out/'tx'
subprocess.run(['cc','-std=c99','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-variable','-fsanitize=address,undefined','-fno-sanitize-recover=undefined',str(src),'-o',str(binary)],check=True)
r=subprocess.run([str(binary)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1'),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);(a.out/'result.log').write_text(r.stdout);print(r.stdout,end='');raise SystemExit(r.returncode)
