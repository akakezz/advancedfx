#!/usr/bin/env python3
"""Experimental Source 1/TF2 demo server-clock A/B probe.

Copies a protocol-3/network-24 demo, changing only outer command tick fields.
NOT a verified playback fix: demo and network clocks are intentionally distinct,
so use only for comparisons and never overwrite the original.
"""
from __future__ import annotations
import argparse, collections, hashlib, json, pathlib, struct, sys
from dataclasses import dataclass

@dataclass(frozen=True)
class Command:
    offset: int
    kind: int
    tick: int
    end: int
    server_tick: int | None = None

class DemoError(Exception):
    pass

def parse(data: bytes):
    if len(data) < 1072 or data[:8] != b'HL2DEMO\0':
        raise DemoError("Invalid Source demo header")
    version, network = struct.unpack_from('<ii', data, 8)
    if (version, network) != (3, 24):
        raise DemoError(f"Only protocol 3 / network 24 supported; got {version}/{network}")
    declared_ticks = struct.unpack_from('<i', data, 1060)[0]
    if declared_ticks < 0:
        raise DemoError("Negative playback length")
    pos, commands, truncated_stop = 1072, [], False
    while pos < len(data):
        start = pos
        if pos + 5 > len(data):
            if pos + 4 == len(data) and data[pos] == 7:
                truncated_stop = True
                pos = len(data)
                break
            raise DemoError(f"Truncated command header at {pos}")
        kind, tick = struct.unpack_from('<Bi', data, pos)
        pos += 5
        server_tick = None
        if kind in (1, 2):
            if pos + 88 > len(data):
                raise DemoError(f"Truncated packet header at {start}")
            length = struct.unpack_from('<i', data, pos + 84)[0]
            pos += 88
            if length < 0 or pos + length > len(data):
                raise DemoError(f"Invalid packet length at {start}")
            if kind == 2 and length >= 5 and (data[pos] & 63) == 3:
                server_tick = (int.from_bytes(data[pos:pos+6], 'little') >> 6) & 0xffffffff
            pos += length
        elif kind in (4, 6, 8):
            if pos + 4 > len(data):
                raise DemoError(f"Missing blob length at {start}")
            length = struct.unpack_from('<i', data, pos)[0]
            pos += 4
            if length < 0 or pos + length > len(data):
                raise DemoError(f"Invalid blob at {start}")
            pos += length
        elif kind == 5:
            if pos + 8 > len(data):
                raise DemoError(f"Truncated usercmd at {start}")
            length = struct.unpack_from('<i', data, pos + 4)[0]
            pos += 8
            if length < 0 or pos + length > len(data):
                raise DemoError(f"Invalid usercmd length at {start}")
            pos += length
        elif kind not in (3, 7):
            raise DemoError(f"Unknown demo command {kind} at {start}")
        commands.append(Command(start, kind, tick, pos, server_tick))
        if kind == 7:
            if pos != len(data):
                raise DemoError("Trailing bytes after stop")
            break
    if len(commands) < 2 or (not truncated_stop and commands[-1].kind != 7):
        raise DemoError("Missing valid command stream or final stop")
    return commands, declared_ticks, truncated_stop

def schedule(commands, declared_ticks):
    indices = [i for i, c in enumerate(commands) if c.kind == 2 and c.server_tick is not None]
    if len(indices) < 100:
        raise DemoError("Not enough embedded NET_Tick messages")
    first = commands[indices[0]]
    offset = first.server_tick - first.tick
    targets = [c.tick for c in commands]
    anchors = []
    for i in indices:
        corrected = commands[i].server_tick - offset
        if corrected < 0 or corrected > declared_ticks:
            raise DemoError("Server clock outside demo timeline")
        if anchors and corrected <= anchors[-1][1]:
            raise DemoError("Server ticks are not strictly increasing")
        targets[i] = corrected
        anchors.append((i, corrected))
    # Interleave other commands in file order without causing tick inversions.
    # Signon and commands before the first NET_Tick are left untouched.
    for (left, left_tick), (right, right_tick) in zip(anchors, anchors[1:]):
        next_original = commands[right].tick
        for j in range(left + 1, right):
            desired = right_tick - (next_original - commands[j].tick)
            targets[j] = max(left_tick, min(right_tick, desired))
    last_index, last_tick = anchors[-1]
    for j in range(last_index + 1, len(commands)):
        targets[j] = max(last_tick, commands[j].tick)
    playback_start = next((i for i, c in enumerate(commands) if c.kind == 3), 0)
    if any(a > b for a, b in zip(targets[playback_start:], targets[playback_start+1:])):
        raise DemoError("Retiming would violate chronological command order")
    if any(t > declared_ticks or t < 0 for t in targets[playback_start:]):
        raise DemoError("Retiming outside declared demo duration")
    changed = collections.Counter(c.kind for c, t in zip(commands, targets) if c.tick != t)
    packet_offsets = collections.Counter(t-c.tick for c,t in zip(commands,targets) if c.kind==2)
    return targets, {
        "mode": "serverclock-retime-v1",
        "clock_offset": offset,
        "trusted_net_tick_anchors": len(anchors),
        "first_anchor": {"demo": first.tick, "server": first.server_tick},
        "last_anchor": {"demo": commands[anchors[-1][0]].tick,
                        "server": commands[anchors[-1][0]].server_tick},
        "changed_command_counts": {str(k):v for k,v in sorted(changed.items())},
        "packet_retime_ticks": {str(k):v for k,v in sorted(packet_offsets.items())},
        "total_commands_changed": sum(changed.values()),
        "declared_playback_ticks": declared_ticks,
        "timing_only": True,
        "network_payloads_modified": False,
        "input_demo_untouched": True,
        "experimental_unverified_in_engine": True,
    }

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('demo', type=pathlib.Path)
    p.add_argument('--output', type=pathlib.Path, help='New .dem; must not already exist')
    p.add_argument('--report', type=pathlib.Path, help='Optional new JSON summary')
    p.add_argument('--dry-run', action='store_true')
    args=p.parse_args()
    if not args.dry_run and args.output is None:
        p.error("Specify --output or --dry-run")
    if args.output and args.demo.resolve() == args.output.resolve():
        p.error("Input and output must differ")
    source=args.demo.read_bytes()
    commands, declared, truncated=parse(source)
    targets, info=schedule(commands, declared)
    info.update(original_sha256=hashlib.sha256(source).hexdigest(),
                original_size=len(source), preserved_truncated_stop=truncated)
    if not args.dry_run:
        patched=bytearray(source)
        for c,t in zip(commands,targets):
            if c.tick != t:
                struct.pack_into('<i', patched, c.offset+1, t)
        check, declared2, truncated2 = parse(patched)
        if (declared,truncated) != (declared2,truncated2):
            raise DemoError("Header or stop changed")
        if len(check) != len(commands) or any(
                (a.offset,a.kind,a.end,a.server_tick) !=
                (b.offset,b.kind,b.end,b.server_tick)
                for a,b in zip(commands,check)):
            raise DemoError("Payload or command structure changed")
        # Verify every byte outside the explicitly edited tick fields.
        mask=bytearray(len(source))
        for c,t in zip(commands,targets):
            if c.tick != t:
                mask[c.offset+1:c.offset+5]=b'\x01'*4
        if any(a!=b for a,b,m in zip(source,patched,mask) if not m):
            raise DemoError("Non-timestamp byte unexpectedly changed")
        with args.output.open('xb') as f:
            f.write(patched)
        info.update(patched_sha256=hashlib.sha256(patched).hexdigest(),
                    patched_size=len(patched), patched_file=str(args.output))
    if args.report:
        with args.report.open('x',encoding='utf-8') as f:
            json.dump(info,f,indent=2);f.write('\n')
    print(json.dumps(info,indent=2))
    return 0

if __name__ == '__main__':
    try:
        sys.exit(main())
    except (DemoError,OSError,struct.error) as e:
        print(f"Server-clock probe error: {e}",file=sys.stderr)
        sys.exit(2)
