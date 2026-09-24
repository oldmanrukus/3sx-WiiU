import re,struct
def load(path):
    s=open(path).read(); body=s[s.index('{')+1:s.rindex('}')]
    return bytes((int(x)&0xFF) for x in re.findall(r'-?\d+',body))
def ref(path,name,nprog):
    d=load(path)
    cs,ver,hs,bs,po,so,vo=struct.unpack_from('<7I',d,4)
    print(f"{name} headerSize={hs} prog@{po} smpl@{so} vagi@{vo}")
    _,maxProg,_=struct.unpack_from('<3I',d,po+4)
    _,maxSmpl,_=struct.unpack_from('<3I',d,so+4)
    _,maxVag,_=struct.unpack_from('<3I',d,vo+4)
    print(f"  maxProg={maxProg} maxSmpl={maxSmpl} maxVag={maxVag}")
    for p in range(nprog):
        ppo=struct.unpack_from('<I',d,po+16+4*p)[0]
        print(f"  GetNumSplit(prog={p})={d[po+ppo]}")
    ppo=struct.unpack_from('<I',d,po+16)[0]; base=po+ppo
    ns=d[base]; sb=base+8
    low,high=d[sb+2],d[sb+3]
    bl,bh=struct.unpack_from('<2H',d,sb+4)
    si=struct.unpack_from('<H',d,sb+10)[0]
    print(f"  prog0 nSplit={ns} split[0]: low={low} high={high} sampleIndex={si} bendLow={bl} bendHigh={bh}")
    sp=so+16+12*si
    a1,a2=struct.unpack_from('<2H',d,sp); bse=d[sp+5]
    vi=struct.unpack_from('<H',d,sp+10)[0]
    print(f"  smpl[{si}]: ADSR1=0x{a1:04X} ADSR2=0x{a2:04X} vagiIndex={vi} base={bse}")
    vp=vo+16+16*vi
    vofs,vsz,lf,rate=struct.unpack_from('<4i',d,vp)
    print(f"  vagi[{vi}]: vagOffset={vofs} vagSize={vsz} loop={lf} rate={rate}")
ref('src/sf33rd/Source/PS2/cseDataFiles/PHD_SE.c','PHD_SE',3)
ref('src/sf33rd/Source/PS2/cseDataFiles/PHD_PL00.c','PHD_PL00',1)
