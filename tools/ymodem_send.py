#!/usr/bin/env python3
"""Minimal YMODEM sender for RT-Thread 'ry' command over COM7."""
import sys, time, serial

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

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else r'E:/test/Git/miditones-master/Melody.mid'
    data = open(path, 'rb').read()
    name = path.replace('\\', '/').rsplit('/', 1)[-1]
    # RT-Thread 'ry' treats argv[1] as a directory prefix and appends the
    # filename from the ymodem header. Force the on-board name to music.mid.
    remote_name = 'music.mid'

    s = serial.Serial(PORT, BAUD, timeout=0.5)
    time.sleep(0.2)
    s.reset_input_buffer()

    # issue ry command with root directory
    s.write(b'ry /\r\n')
    time.sleep(0.3)

    log = []
    def wait_for(chars, timeout=20):
        t0 = time.time()
        buf = b''
        while time.time() - t0 < timeout:
            if s.in_waiting:
                b = s.read(1)
                log.append(b)
                if b in chars:
                    return b
                buf += b
        return None

    # handshake: wait for 'C'
    c = wait_for(b'C')
    if c is None:
        print("no handshake C, aborting")
        s.close(); return
    print("got C, sending header")

    # block 0: filename + size + padding
    hdr = remote_name.encode() + b'\0' + str(len(data)).encode() + b'\0'
    blk = hdr + b'\0' * (128 - len(hdr))
    packet = b'\x01' + b'\x00' + b'\xff' + blk
    crc = crc16_ccitt(blk)
    packet += bytes([crc >> 8, crc & 0xFF])
    s.write(packet)
    resp = wait_for(b'\x06', 10)
    if resp is None:
        print("no ACK after header; received:", bytes(log).hex()); s.close(); return
    print("header ACKed, waiting C")
    if wait_for(b'C', 10) is None:
        print("no second C"); s.close(); return

    # data blocks (128 bytes, SOH)
    seq = 1
    off = 0
    while off < len(data):
        chunk = data[off:off+128]
        blk = chunk + b'\x1a' * (128 - len(chunk))
        packet = b'\x01' + bytes([seq & 0xFF, (~seq) & 0xFF]) + blk
        crc = crc16_ccitt(blk)
        packet += bytes([crc >> 8, crc & 0xFF])
        s.write(packet)
        if wait_for(b'\x06', 10) is None:
            print("no ACK at block", seq); s.close(); return
        off += 128
        seq = (seq + 1) & 0xFF
    print("sent", seq-1, "data blocks")

    # EOT: RTT asks for a second EOT with NAK, then ACKs
    s.write(b'\x04')
    r1 = wait_for(b'\x15\x06', 10)
    if r1 is None:
        print("no response after 1st EOT; received:", bytes(log).hex()); s.close(); return
    if r1 == b'\x15':          # NAK -> send second EOT
        s.write(b'\x04')
        r2 = wait_for(b'\x06', 10)
        if r2 is None:
            print("no ACK after 2nd EOT; received:", bytes(log).hex()); s.close(); return
    print("transfer complete")

    # drain final output
    time.sleep(0.5)
    out = b''
    t0 = time.time()
    while time.time() - t0 < 1:
        if s.in_waiting:
            out += s.read(s.in_waiting)
        time.sleep(0.05)
    s.close()
    print(out.decode('utf-8', errors='replace'))

if __name__ == '__main__':
    main()
