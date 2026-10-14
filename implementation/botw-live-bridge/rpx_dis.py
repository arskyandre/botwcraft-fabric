"""Read-only linear disassembly of the pinned US208 update RPX.

Unlike physics_inspect.py (fixed 128-byte windows) this walks a requested
range, so whole native functions can be read. It never writes the title.
Usage: python -B rpx_dis.py START [END|+LEN] [--output evidence.json]
"""
import argparse, hashlib, json, pathlib, struct, sys, zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / 'build/python'))
import capstone

import local
# The update's (v208) own code/U-King.rpx, as installed in Cemu's mlc01/usr/title/0005000e/101c9400: local.json "game_update_rpx".
RPX = local.required('game_update_rpx', "the v208 update's code/U-King.rpx")
SHA = 'ba58da5b95ce929e005d058ceb08b9b2788d1ab2bbc8a6c189bbadca0bb34d30'

def sections(data):
    assert data[:6] == b'\x7fELF\x01\x02'
    shoff = struct.unpack_from('>I', data, 32)[0]
    entsize, count = struct.unpack_from('>HH', data, 46)
    for i in range(count):
        _, _, flags, addr, offset, size, *_ = struct.unpack_from('>10I', data, shoff + i * entsize)
        if not size or not addr:
            continue
        body = data[offset:offset + size]
        if flags & 0x08000000:
            expected = struct.unpack_from('>I', body)[0]
            body = zlib.decompress(body[4:])
            assert len(body) == expected
        yield addr, body

def load():
    data = RPX.read_bytes()
    assert hashlib.sha256(data).hexdigest() == SHA, 'Unexpected update RPX'
    return list(sections(data))

def read(secs, address, size):
    for addr, body in secs:
        if addr <= address and address + size <= addr + len(body):
            return body[address - addr:address - addr + size]
    raise ValueError(f'unmapped {address:08x}')

def disassemble(secs, start, end):
    decoder = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    binary = read(secs, start, end - start)
    lines = []
    for offset in range(0, len(binary), 4):
        word = struct.unpack_from('>I', binary, offset)[0]
        decoded = list(decoder.disasm(binary[offset:offset + 4], start + offset))
        text = f'{decoded[0].mnemonic} {decoded[0].op_str}' if decoded else f'.long 0x{word:08x}'
        lines.append(f'{start + offset:08x} {word:08x} {text}')
    return lines

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('start', type=lambda v: int(v, 0))
    parser.add_argument('end', nargs='?', default='+0x100')
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    end = args.start + int(args.end[1:], 0) if args.end.startswith('+') else int(args.end, 0)
    assert args.start % 4 == 0 and end > args.start and end - args.start <= 0x4000
    lines = disassemble(load(), args.start, end)
    print('\n'.join(lines))
    if args.output:
        assert not args.output.exists(), 'Preserve previous evidence'
        args.output.write_text(json.dumps({'rpx': str(RPX), 'sha256': SHA, 'start': hex(args.start),
                                           'end': hex(end), 'disassembly': lines}, indent=1), encoding='utf-8')

if __name__ == '__main__':
    main()
