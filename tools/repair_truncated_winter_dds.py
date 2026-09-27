"""Repair the known incomplete last BC3 mip; never synthesize missing texels."""
from pathlib import Path
import struct,hashlib,argparse,json
ap=argparse.ArgumentParser();ap.add_argument('source',type=Path);ap.add_argument('output',type=Path)
args=ap.parse_args();data=args.source.read_bytes()
assert data[:4]==b'DDS ' and data[84:88]==b'DXT5'
header=struct.unpack('<31I',data[4:128])
assert header[0]==124 and header[2:4]==(2048,2048) and header[6]==12 and header[27]==0
assert len(data)==5592547,'Only the known 13-byte truncated winter DDS is supported'
end=128;complete=0;w,h=header[3],header[2]
for mip in range(header[6]):
    size=max(1,(w+3)//4)*max(1,(h+3)//4)*16
    if end+size>len(data):break
    complete+=1;end+=size;w=max(1,w//2);h=max(1,h//2)
assert complete==11 and end==5592544
output=bytearray(data[:end]);struct.pack_into('<I',output,28,complete)
assert output[128:]==data[128:end],'Every complete mip must remain byte-identical'
args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(output)
print(json.dumps(dict(source_sha256=hashlib.sha256(data).hexdigest(),output_sha256=hashlib.sha256(output).hexdigest(),
    source_bytes=len(data),output_bytes=len(output),complete_mips=complete,discarded_incomplete_bytes=len(data)-end)))
