#!/usr/bin/env python3
"""Send msh commands over COM7 and print the reply. Usage: msh_cmd.py "cmd1" "cmd2" ..."""
import sys, time, serial

PORT = 'COM7'
BAUD = 115200

def main():
    s = serial.Serial(PORT, BAUD, timeout=0.5)
    time.sleep(0.2)
    s.reset_input_buffer()

    for cmd in sys.argv[1:]:
        print(f'>>> {cmd}')
        s.write((cmd + '\r\n').encode())
        t0 = time.time()
        out = b''
        while time.time() - t0 < 3.0:
            if s.in_waiting:
                out += s.read(s.in_waiting)
            time.sleep(0.05)
        print(out.decode('utf-8', errors='replace'))
    s.close()

if __name__ == '__main__':
    main()
