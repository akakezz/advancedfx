#!/usr/bin/env python3
"""Offline regression for the TF2 demo scheduling A/B probe."""
import struct, sys, unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from tf2_demo_serverclock_probe import parse, schedule, DemoError

def fixture(n=140):
    demo=bytearray(1072)
    demo[:8]=b'HL2DEMO\0'
    struct.pack_into('<ii',demo,8,3,24)
    struct.pack_into('<i',demo,1060,n+10)
    demo.extend(struct.pack('<Bi',1,62669)+bytes(88))
    demo.extend(struct.pack('<Bi',3,0))
    for i in range(n):
        tick=i+1+(i%2==0)
        demo.extend(struct.pack('<Bi',5,tick))
        demo.extend(struct.pack('<ii',i+1,1)+b'Q')
        payload=(((1001+i)<<6)|3).to_bytes(6,'little')+b'\x99\xaa'
        demo.extend(struct.pack('<Bi',2,tick)+bytes(84)+struct.pack('<i',len(payload))+payload)
    demo.extend(struct.pack('<Bi',7,n+10))
    return bytes(demo)

class ProbeTests(unittest.TestCase):
    def test_monotonic_and_payload_preserving(self):
        old=fixture()
        commands,declared,truncated=parse(old)
        self.assertFalse(truncated)
        ticks,stats=schedule(commands,declared)
        self.assertEqual(stats['trusted_net_tick_anchors'],140)
        self.assertGreater(stats['total_commands_changed'],0)
        pos=next(i for i,c in enumerate(commands) if c.kind==3)
        self.assertTrue(all(a<=b for a,b in zip(ticks[pos:],ticks[pos+1:])))
        changed=bytearray(old)
        for c,t in zip(commands,ticks):
            if c.tick!=t:
                struct.pack_into('<i',changed,c.offset+1,t)
        new,header,short=parse(changed)
        self.assertEqual((header,short),(declared,truncated))
        self.assertEqual(old[:1072],changed[:1072])
        for a,b in zip(commands,new):
            self.assertEqual((a.offset,a.kind,a.end,a.server_tick),
                             (b.offset,b.kind,b.end,b.server_tick))
            self.assertEqual(old[a.offset+5:a.end],changed[a.offset+5:a.end])
        self.assertEqual(commands[0].tick,ticks[0])

    def test_reject_invalid_input(self):
        data=bytearray(fixture())
        struct.pack_into('<i',data,8,4)
        with self.assertRaises(DemoError):
            parse(data)
        data=fixture()[:-2]
        with self.assertRaises(DemoError):
            parse(data)

    def test_existing_short_stop_preserved(self):
        commands,declared,short=parse(fixture()[:-1])
        self.assertTrue(short)
        _,stats=schedule(commands,declared)
        self.assertGreater(stats['total_commands_changed'],0)

if __name__ == '__main__':
    unittest.main()
