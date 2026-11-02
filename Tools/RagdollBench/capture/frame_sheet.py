# usage: sheet.py dir out.png columns crop_w crop_h [cx cy] -- contact sheet of captured frames
import sys,glob,os,struct,zlib
d,out,cols,cw,ch=sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4]),int(sys.argv[5])
files=sorted(glob.glob(os.path.join(d,'f*.bgra')))
step=int(sys.argv[6]) if len(sys.argv)>6 else 1
files=files[::step]
rows=(len(files)+cols-1)//cols
W,H=cols*cw//2,rows*ch//2
canvas=bytearray(W*H*3)
for n,f in enumerate(files):
    data=open(f,'rb').read(); w,h=struct.unpack('<II',data[:8]); px=data[8:]
    x0=(w-cw)//2; y0=(h-ch)//2
    ox=(n%cols)*cw//2; oy=(n//cols)*ch//2
    for y in range(0,ch,2):
        src=(y0+y)*w*4+x0*4
        line=px[src:src+cw*4]
        dst=((oy+y//2)*W+ox)*3
        for x in range(0,cw,2):
            b,g,r=line[x*4],line[x*4+1],line[x*4+2]
            i=dst+(x//2)*3
            canvas[i]=r;canvas[i+1]=g;canvas[i+2]=b
raw=bytearray()
for y in range(H):
    raw.append(0);raw+=canvas[y*W*3:(y+1)*W*3]
def chunk(t,p):
    b=t+p;return struct.pack('>I',len(p))+b+struct.pack('>I',zlib.crc32(b))
open(out,'wb').write(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',W,H,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(bytes(raw),6))+chunk(b'IEND',b''))
print(out,W,H,len(files),'frames, source',w,h)
