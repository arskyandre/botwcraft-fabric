"""Read-only queries on the pinned RPX. Usage:
  rpx_q.py start ADDR            function start (scan back for stwu r1,-N(r1) after a blr)
  rpx_q.py word VALUE            every aligned occurrence of a 32-bit value in any section
  rpx_q.py words ADDR COUNT      dump COUNT words
  rpx_q.py calls TARGET          every `bl TARGET`
  rpx_q.py str ADDR              C string
"""
import pathlib, struct, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import rpx_dis
secs = rpx_dis.load()
cmd = sys.argv[1]; arg = int(sys.argv[2], 0)
def word(a): return struct.unpack('>I', rpx_dis.read(secs, a, 4))[0]
if cmd == 'start':
    a = arg & ~3
    while True:
        w = word(a)
        if w >> 16 == 0x9421 and word(a - 4) in (0x4E800020, 0x4E800420) or (w >> 16 == 0x9421 and (word(a - 4) >> 26) == 18): break
        a -= 4
    print(hex(a))
elif cmd == 'word':
    target = struct.pack('>I', arg)
    for addr, body in secs:
        at = body.find(target)
        while at >= 0:
            if at % 4 == 0: print(hex(addr + at))
            at = body.find(target, at + 1)
elif cmd == 'words':
    for i in range(int(sys.argv[3], 0)): print(f'{arg + 4 * i:08x} +{4 * i:03x} {word(arg + 4 * i):08x}')
elif cmd == 'calls':
    for addr, body in secs:
        if not (0x02000000 <= addr < 0x10000000): continue
        for off in range(0, len(body) - 3, 4):
            w = struct.unpack_from('>I', body, off)[0]
            if w >> 26 == 18 and w & 3 == 1:
                d = w & 0x03FFFFFC
                if d & 0x02000000: d -= 0x04000000
                if addr + off + d == arg: print(hex(addr + off))
elif cmd == 'str':
    out = b''
    while True:
        c = rpx_dis.read(secs, arg, 1)
        if c == b'\0': break
        out += c; arg += 1
    print(out.decode('utf-8', 'replace'))
