#!/usr/bin/env python3
"""Upload a MIDI file via YMODEM, run 'midiplay', capture conversion output."""
import sys, time, serial, os

PORT = 'COM7'
BAUD = 115200

def crc16_ccitt(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc

def drain(s, seconds=1.0):
    out = b''
    t0 = time.time()
    while time.time() - t0 < seconds:
        if s.in_waiting:
            out += s.read(s.in_waiting)
        time.sleep(0.05)
    return out

def main():
    path = sys.argv[1]
    data = open(path, 'rb').read()
    base = os.path.basename(path)
    remote_name = 'music.mid'

    s = serial.Serial(PORT, BAUD, timeout=0.5)
    time.sleep(0.2)
    s.reset_input_buffer()

    def wait_for(chars, timeout=20):
        t0 = time.time()
        while time.time() - t0 < timeout:
            if s.in_waiting:
                b = s.read(1)
                print(' <-', repr(b), end='', flush=True)
                if b in chars:
                    return b
        return None

    print(f'=== Testing {base} ({len(data)} bytes) ===')
    s.write(b'ry /\r\n')
    time.sleep(0.3)
    if wait_for(b'C') is None:
        print('\nno handshake C'); s.close(); return

    hdr = remote_name.encode() + b'\0' + str(len(data)).encode() + b'\0'
    blk = hdr + b'\0' * (128 - len(hdr))
    packet = b'\x01\x00\xff' + blk
    crc = crc16_ccitt(blk)
    s.write(packet + bytes([crc >> 8, crc & 0xFF]))
    if wait_for(b'\x06', 10) is None:
        print('\nno ACK after header'); s.close(); return
    if wait_for(b'C', 10) is None:
        print('\nno second C'); s.close(); return

    seq = 1; off = 0
    while off < len(data):
        chunk = data[off:off+128]
        blk = chunk + b'\x1a' * (128 - len(chunk))
        packet = b'\x01' + bytes([seq & 0xFF, (~seq) & 0xFF]) + blk
        crc = crc16_ccitt(blk)
        s.write(packet + bytes([crc >> 8, crc & 0xFF]))
        if wait_for(b'\x06', 10) is None:
            print(f'\nno ACK at block {seq}'); s.close(); return
        off += 128; seq = (seq + 1) & 0xFF
    print(f'\nsent {seq-1} blocks')

    s.write(b'\x04')
    r1 = wait_for(b'\x15\x06', 10)
    if r1 == b'\x15':
        s.write(b'\x04')
        wait_for(b'\x06', 10)
    print('\nupload done')

    drain(s, 0.5)
    s.write(b'midiplay\r\n')
    print('--- midiplay output ---')
    out = drain(s, 5.0)
    print(out.decode('utf-8', errors='replace'))
    s.close()

if __name__ == '__main__':
    main()
