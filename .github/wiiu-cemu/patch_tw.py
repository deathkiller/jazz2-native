# Test-only: replaces conditional trap instructions (tw with TO != 31, twi) in .text with nop so Cemu 2.6,
# which never moves past them (it does not evaluate the condition), can run the build. usage: patch_tw.py in.elf out.elf
import struct,sys
d=bytearray(open(sys.argv[1],'rb').read())
shoff,=struct.unpack('>I',d[0x20:0x24]); shentsize,shnum,shstrndx=struct.unpack('>HHH',d[0x2e:0x34])
secs=[struct.unpack('>IIIIIIIIII',d[shoff+i*shentsize:shoff+i*shentsize+40]) for i in range(shnum)]
strtab=secs[shstrndx]; name=lambda o: d[strtab[4]+o:d.index(0,strtab[4]+o)].decode()
n=0
for s in secs:
    if name(s[0])!='.text': continue
    off,size=s[4],s[5]
    for p in range(off,off+size,4):
        w,=struct.unpack('>I',d[p:p+4])
        op=w>>26; to=(w>>21)&31
        if (op==31 and (w&0x7FE)==8 and to!=31) or (op==3 and to!=31):
            d[p:p+4]=struct.pack('>I',0x60000000); n+=1
open(sys.argv[2],'wb').write(d); print('patched',n)
