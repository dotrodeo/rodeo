#!/usr/bin/env python3
"""Checks rodeo-demo's MIDI protocol (planning/RODEO_DEMO_MIDI_PROTOCOL.md: the
SDK's host protocol, version 2, and rodeo-demo's own commands) against a Rodeo
plugged into this computer, the way the console and the phone app use it.

    pip3 install mido python-rtmidi
    python3 instruments/rodeo-demo/firmware/tools/midi_protocol_test.py          # the checks
    python3 instruments/rodeo-demo/firmware/tools/midi_protocol_test.py --watch  # print the stream

The checks leave the synth much as they found it: they reload the preset that was
loaded and put back the shape and detune they change.
"""
import json
import os
import sys
import time

import mido

HEADER = [0x7D, 0x52, 0x44, 0x02]  # mido leaves out F0 and F7

# rodeo-demo's own commands; a reply is the command plus 0x40
PRESETS, LOAD, SOUNDS = 0x20, 0x21, 0x22


def port_name(names):
    for n in names:
        if 'rodeo' in n.lower() or 'daisy' in n.lower():
            return n
    sys.exit(f'no Rodeo among the MIDI ports: {names}')


def send(out, cmd, data=()):
    out.send(mido.Message('sysex', data=HEADER + [cmd] + list(data)))


def replies(inp, want=None, timeout=1.0):
    """(cmd, payload) for every message of ours until timeout, or the first `want`."""
    got, end = [], time.time() + timeout
    while time.time() < end:
        for m in inp.iter_pending():
            if m.type != 'sysex' or list(m.data[:4]) != HEADER:
                continue
            cmd, body = m.data[4], bytes(m.data[5:]).decode('ascii')
            got.append((cmd, json.loads(body)))
            if cmd == want:
                return got
        time.sleep(0.005)
    return got


def flush(inp):
    """Drop what's already in: another program's stream (the app, --watch) reaches
    every listener, so a state message there may be older than the request."""
    for _ in inp.iter_pending():
        pass


def one(inp, want, timeout=1.0):
    for cmd, body in replies(inp, want, timeout):
        if cmd == want:
            return body
    sys.exit(f'no reply 0x{want:02X} within {timeout} s')


def u14(v):
    return [v & 0x7F, (v >> 7) & 0x7F]


def main():
    inp = mido.open_input(port_name(mido.get_input_names()))
    out = mido.open_output(port_name(mido.get_output_names()))
    flush(inp)

    send(out, 0x01)
    hello = one(inp, 0x41)
    params = {p['key']: p for p in hello['params']}
    print(f"hello: {hello['fw']} protocol {hello['proto']} build {hello['build']}, "
          f"sdk {hello['sdk']}, {len(params)} parameters")
    assert hello['proto'] == 2 and hello['fw'] == 'rodeo-demo', hello
    assert all('def' in p for p in params.values()), 'a parameter without a default'
    assert params['cutoff']['def'] == 600 and params['fx']['def'] == 2, 'defaults: synth.h'

    if '--watch' in sys.argv:
        print('streaming; turn things on the Rodeo (ctrl-C to stop)')
        last = 0
        while True:
            if time.time() - last > 2:
                send(out, 0x03, [1])
                last = time.time()
            for cmd, body in replies(inp, timeout=0.1):
                if cmd == 0x42:
                    p = body['p']
                    print(f"preset {body['preset']} {body['name']!r}  cutoff {p['cutoff']} res "
                          f"{p['resonance']} env {p['env']} rel {p['release']} atk {p['attack']} "
                          f"hold {body['hold']} voices {body['voices']} cpu {body['cpu']}")
                elif cmd == 0x43:
                    print('  said:', body['said'])

    flush(inp)
    send(out, 0x02)
    state = one(inp, 0x42)
    before = state['p']
    print(f"state: preset {state['preset']} {state['name']!r}, panel {state['panel']}, "
          f"card {state['card']}, {state['sounds']} sounds")
    assert set(before) == set(params), 'the state and the table disagree'

    send(out, PRESETS)
    presets = one(inp, PRESETS + 0x40)
    print(f"presets: {presets['presets']} (current {presets['current']})")
    send(out, SOUNDS)
    sounds = one(inp, SOUNDS + 0x40)
    print(f"sounds: {[s['name'] for s in sounds['sounds']]}")

    send(out, 0x08)
    chain = one(inp, 0x48)
    print(f"chain: {chain['state']}, {chain['sats']} satellites, kinds "
          f"{[s['kind'] for s in chain['s']]}, {chain['good']} good scans, {chain['bad']} bad")
    assert chain['sats'] == len(chain['s'])

    # Set a pot's parameter and a choice in one message, over the stream
    send(out, 0x03, [1])
    cut, shape = params['cutoff'], params['shape']
    want_cut, want_shape = 123, (before['shape'] + 1) % (shape['max'] + 1)
    send(out, 0x04, [cut['id']] + u14(want_cut - cut['min'])
         + [shape['id']] + u14(want_shape - shape['min']))
    said, state = [], None
    for cmd, body in replies(inp, timeout=0.5):
        if cmd == 0x43:
            said.append(body['said'])
        if cmd == 0x42:
            state = body
    assert state, 'no state on the stream'
    assert state['p']['cutoff'] == want_cut, state['p']['cutoff']
    assert state['p']['shape'] == want_shape, state['p']['shape']
    assert state['hold'] & 1, 'cutoff should be held from the pot'
    print(f'set: {said}; cutoff held: ok')

    # A CC, as any controller would send it
    out.send(mido.Message('control_change', control=94, value=127))
    time.sleep(0.2)
    flush(inp)
    send(out, 0x02)
    got = one(inp, 0x42)['p']['detune']
    assert got == 50, got
    print('CC 94 (detune) 127 -> 50 cents: ok')

    # Notes and Program Change, as a keyboard would send them
    out.send(mido.Message('note_on', note=60, velocity=90))
    time.sleep(0.15)
    flush(inp)
    send(out, 0x02)
    assert one(inp, 0x42)['voices'] >= 1, 'a held note should sound'
    out.send(mido.Message('note_off', note=60))
    time.sleep(1.0 + before['release'] / 1000)
    flush(inp)
    send(out, 0x02)
    assert one(inp, 0x42)['voices'] == 0, 'the note should have ended'
    if len(presets['presets']) > 1:
        other = (max(state['preset'], 0) + 1) % len(presets['presets'])
        out.send(mido.Message('program_change', program=other))
        time.sleep(0.2)
        flush(inp)
        send(out, 0x02)
        got = one(inp, 0x42)['preset']
        assert got == other, got
    print('note on/off, Program Change: ok')

    # Errors (not 0x09: that's dfu, which reboots into the bootloader)
    for cmd in (0x10, 0x30):
        send(out, cmd)
        assert any(c == 0x7F for c, _ in replies(inp, 0x7F)), f'no error for 0x{cmd:02X}'
    print('unknown commands, the SDK\'s and the firmware\'s -> errors: ok')

    # Put it all back. Reloading the preset hands cutoff back to pot 1 (or to the
    # preset's value): setting it again would hold it from the pot.
    if state['preset'] >= 0:
        send(out, LOAD, u14(state['preset']))
        time.sleep(0.2)
    restore = []
    for key in ('shape', 'detune'):
        p = params[key]
        restore += [p['id']] + u14(before[key] - p['min'])
    send(out, 0x04, restore)
    send(out, 0x03, [0])
    print('put back; all checks passed')


if __name__ == '__main__':
    code = 0
    try:
        main()
    except KeyboardInterrupt:
        pass
    except SystemExit as e:  # a failed check: say why
        if e.code not in (None, 0):
            print(e.code)
            code = 1
    # Closing mido's rtmidi ports can hang on macOS; nothing is left to flush
    sys.stdout.flush()
    os._exit(code)
