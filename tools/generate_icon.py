import math, struct, zlib
from pathlib import Path

def png(n):
    rows=[]
    scale=4
    for y in range(n):
        row=bytearray(b'\0')
        for x in range(n):
            sums=[0.,0.,0.]; alpha=0
            for sy in range(scale):
                for sx in range(scale):
                    dx=(x+(sx+.5)/scale)/n-.5
                    dy=(y+(sy+.5)/scale)/n-.5
                    qx=max(abs(dx)-.31,0); qy=max(abs(dy)-.31,0)
                    if qx*qx+qy*qy>.16*.16:continue
                    color=(16,34,29)
                    ax=abs(dx);ay=abs(dy)
                    if (.14<=ax<=.34 and ay<=.035) or (.14<=ay<=.34 and ax<=.035): color=(104,239,187)
                    if ax<=.036 and ay<=.036:color=(242,255,249)
                    for k in range(3):sums[k]+=color[k]
                    alpha+=1
            if alpha: row+=bytes(round(c/alpha) for c in sums)+bytes([round(alpha/(scale*scale)*255)])
            else: row+=b'\0\0\0\0'
        rows.append(bytes(row))
    def chunk(name,data):return struct.pack('>I',len(data))+name+data+struct.pack('>I',zlib.crc32(name+data)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',n,n,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows),9))+chunk(b'IEND',b'')

sizes=(16,32,48,64)
imgs=[png(n) for n in sizes]
header=struct.pack('<HHH',0,1,len(imgs));offset=6+16*len(imgs);entries=[]
for n,img in zip(sizes,imgs):
 entries.append(struct.pack('<BBBBHHII',n,n,0,0,1,32,len(img),offset));offset+=len(img)
p=Path(__file__).resolve().parent.parent/'src'/'app.ico'
p.write_bytes(header+b''.join(entries)+b''.join(imgs))
print(p, p.stat().st_size)
