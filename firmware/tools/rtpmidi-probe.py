#!/usr/bin/env python3
"""Minimal RTP-MIDI (AppleMIDI) initiator used to prove the pedal's network session.

    python rtpmidi-probe.py 192.168.1.75 [COM6]

It invites the pedal on its control and data ports, answers the clock sync, then listens
for RTP-MIDI packets. If a serial port is given it simulates a press of switch 1 so there
is something to receive. Needs only the standard library.
"""
import os
import random
import socket
import struct
import sys
import time

SIG = b"\xff\xff"
HOST = sys.argv[1] if len(sys.argv) > 1 else "midipedal.local"
PORT = sys.argv[2] if len(sys.argv) > 2 else None
CTRL, DATA = 5004, 5005
LOCAL_CTRL = 15004

token = random.getrandbits(32)
ssrc = random.getrandbits(32)
name = b"rtpmidi-probe\x00"

ctrl = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
data = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
ctrl.bind(("", LOCAL_CTRL))
data.bind(("", LOCAL_CTRL + 1))
ctrl.settimeout(3)
data.settimeout(3)


def invite(sock, port):
    sock.sendto(SIG + b"IN" + struct.pack(">III", 2, token, ssrc) + name, (HOST, port))
    pkt, addr = sock.recvfrom(1500)
    if pkt[:2] != SIG or pkt[2:4] != b"OK":
        raise SystemExit(f"port {port}: unexpected reply {pkt.hex()}")
    _, rtoken, rssrc = struct.unpack(">III", pkt[4:16])
    rname = pkt[16:].split(b"\x00")[0].decode(errors="replace")
    print(f"  port {port}: invitation accepted by '{rname}' (ssrc {rssrc:08x})")
    return rssrc


print(f"inviting {HOST} ...")
remote_ssrc = invite(ctrl, CTRL)
invite(data, DATA)

# Clock sync: CK0 from us, CK1 from the pedal, CK2 from us.
def now_us():
    return int(time.monotonic() * 1_000_000) & 0xFFFFFFFFFFFFFFFF

ck0 = SIG + b"CK" + struct.pack(">IBxxxQQQ", ssrc, 0, now_us(), 0, 0)
data.sendto(ck0, (HOST, DATA))
pkt, _ = data.recvfrom(1500)
if pkt[2:4] == b"CK" and pkt[8] == 1:
    t1, t2, _ = struct.unpack(">QQQ", pkt[12:36])
    data.sendto(SIG + b"CK" + struct.pack(">IBxxxQQQ", ssrc, 2, t1, t2, now_us()), (HOST, DATA))
    print(f"  clock sync done (pedal clock {t2} us)")
else:
    print(f"  unexpected during sync: {pkt.hex()}")

# Ask the pedal to press switch 1 so it emits MIDI into the session.
if PORT:
    try:
        import serial  # pyserial, optional
        with serial.Serial(PORT, 115200, timeout=1, dsrdtr=False) as s:
            s.dtr = True
            s.rts = True
            time.sleep(0.3)
            s.write(b'{"cmd":"press","i":0}\n')
            time.sleep(0.15)
            s.write(b'{"cmd":"release","i":0}\n')
            print("  simulated press of switch 1 over", PORT)
    except ImportError:
        print("  (pyserial not installed; press a switch on the pedal now)")
else:
    print("  press a switch on the pedal now ...")

print("listening for RTP-MIDI for 6 s ...")
data.settimeout(6)
deadline = time.time() + 6
got = 0
while time.time() < deadline:
    try:
        pkt, _ = data.recvfrom(1500)
    except socket.timeout:
        break
    if pkt[:2] == SIG:
        if pkt[2:4] == b"CK":  # pedal-initiated sync: answer it
            cnt = pkt[8]
            if cnt == 0:
                t1 = struct.unpack(">Q", pkt[12:20])[0]
                data.sendto(SIG + b"CK" + struct.pack(">IBxxxQQQ", ssrc, 1, t1, now_us(), 0), (HOST, DATA))
        continue
    if len(pkt) >= 13 and (pkt[0] >> 6) == 2:
        flags = pkt[12]
        if flags & 0x80:  # 12-bit length
            length = ((flags & 0x0F) << 8) | pkt[13]
            body = pkt[14:14 + length]
        else:
            length = flags & 0x0F
            body = pkt[13:13 + length]
        if flags & 0x20 or not body:
            midi = body
        else:
            midi = body  # first command has no delta time when Z bit is clear
        got += 1
        print(f"  RTP-MIDI seq {struct.unpack('>H', pkt[2:4])[0]} payload: {midi.hex(' ')}")

# Leave politely.
bye = SIG + b"BY" + struct.pack(">III", 2, token, ssrc)
ctrl.sendto(bye, (HOST, CTRL))
print("done:", "received MIDI over the network" if got else "no MIDI received")
