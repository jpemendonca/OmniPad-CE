#!/usr/bin/env python3
"""Puts each Bluetooth bug seen on the PS5 back into a copy of the code and runs the simulator.

A bug the simulator does not catch means the test protects nothing: it counts as a failure here.
Run it from the repository root, after changing tests/test_bt_sim.c or the rules it holds.
"""
import os
import shutil
import subprocess
import sys
import tempfile

SOURCES = ['tests/test_bt_sim.c', 'src/bt_sched.c', 'src/bt/host.c', 'src/bt/le.c', 'src/bt/smp_crypto.c',
           'src/bt/bt_profiles.c', 'src/bt/generic.c', 'src/bt/crc32.c']

# name, file, text in the code, the same code with the bug back
BUGS = [
    ('commands sent without waiting for the turn', 'src/bt_sched.c',
     'while (q->count && q->credits > 0) {', 'while (q->count) {'),
    ('a controller that never answers stops the queue', 'src/bt_sched.c',
     'q->credits = 1;\n    }\n    while', '}\n    while'),
    ('a clone gets the short look with a pad connected', 'src/bt_sched.c',
     'return fake_csr ? BT_AUTO_OFF : BT_AUTO_PEEK;', 'return BT_AUTO_PEEK;'),
    ('full LE scan with a stored pad off: the DS4 falls to 28 reports/s', 'src/bt/le.c',
     'int light = linked && now >= h->pair_until;', 'int light = 0;'),
    ('LE scan stays on a clone with a pad connected', 'src/bt/le.c',
     'if (light && h->le_quiet_linked) want = 0;', ''),
    ('LE scan enable repeated every 5 s, as upstream', 'src/bt/le.c',
     'if (want != h->le_scanning) {', 'if (want != h->le_scanning || now - h->t_scan > 5000) {'),
    ('LE scan runs during an inquiry', 'src/bt/le.c',
     'if (h->inquiring) want = 0;', ''),
    ('no LE turn between inquiries', 'src/bt/host.c',
     'if (h->was_inquiring && !h->inquiring) h->le_turn_until = now + T_LE_TURN;', ''),
    ('LE connection attempted during an inquiry', 'src/bt/le.c',
     'if (h->inquiring) {\n        send_cmd(h, OP_INQUIRY_CANCEL, NULL, 0);\n        h->inquiring = 0;\n    }\n    le_create(h, l);',
     'le_create(h, l);'),
    ('LE scan left on during LE Create Connection', 'src/bt/le.c',
     'send_cmd(h, OP_LE_SET_SCAN_ENABLE, off, 2);\n    h->le_scanning = 0;\n\n    put16(p, 0x0060);',
     'put16(p, 0x0060);'),
    ('inquiry restarted while an LE pad links', 'src/bt/host.c',
     'if (h->links[i].used && !h->links[i].ready && (h->links[i].pairing || h->links[i].le)) linking = 1;',
     'if (h->links[i].used && !h->links[i].ready && h->links[i].pairing) linking = 1;'),
    ('Xbox subscribed without its values read: silent for 61 s', 'src/bt/le.c',
     'gatt_go(h, l, G_READS);\n            } else {', 'gatt_go(h, l, G_CCCD);\n            } else {'),
    ('page scan looked at and changed during play', 'src/bt/host.c',
     'tune_t g_tune = { 0, PS_PAIR_INTERVAL', 'tune_t g_tune = { 1, PS_PAIR_INTERVAL'),
    ('pad traffic does not count as life: probes during play', 'src/bt/host.c',
     'on_acl(h, buf, n);', 'h->last_rx = now_ms() - 21000; on_acl(h, buf, n);'),
    ('the DS4 is never asked for its full report', 'src/bt/host.c',
     'if (l->init_done || !l->prof->init_report) return;', 'return;'),
    ('a deaf chip is never given up on', 'src/bt/host.c',
     'if (!h->dead) log_line("controller silent after %d probes: transport lost", h->probes);\n            h->dead = 1;',
     'h->probes = 0;'),
]


def read(path):
    with open(path, encoding='utf-8', newline='') as f:
        return f.read().replace('\r\n', '\n')


def run_with(tmp, bug):
    work = os.path.join(tmp, 'tree')
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree('src', os.path.join(work, 'src'))
    shutil.copytree('tests', os.path.join(work, 'tests'))
    os.mkdir(os.path.join(work, 'build'))
    if bug:
        _, path, old, new = bug
        text = read(path)
        if text.count(old) != 1:
            return None, f'the code moved: {path} has {text.count(old)} matches'
        with open(os.path.join(work, path), 'w', encoding='utf-8', newline='') as f:
            f.write(text.replace(old, new))
    built = subprocess.run(['clang', '-O1', '-w', '-Isrc', '-Isrc/bt'] + SOURCES + ['-o', 'build/sim'],
                           cwd=work, capture_output=True, text=True)
    if built.returncode:
        return None, built.stderr[-400:]
    try:
        ran = subprocess.run(['./build/sim'], cwd=work, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return 1, 'hung'
    fails = [line for line in ran.stdout.splitlines() if line.startswith('FAIL')]
    return ran.returncode, fails[0] if fails else ran.stdout.strip()[-120:]


def main():
    missed = 0
    with tempfile.TemporaryDirectory() as tmp:
        code, why = run_with(tmp, None)
        if code != 0:
            print(f'the simulator fails on the code as it is: {why}')
            return 1
        for bug in BUGS:
            code, why = run_with(tmp, bug)
            if code is None:
                print(f'SKIPPED  {bug[0]}: {why}')
                missed += 1
            elif code == 0:
                print(f'MISSED   {bug[0]}')
                missed += 1
            else:
                print(f'caught   {bug[0]}\n           {why[:110]}')
    print(f'{len(BUGS) - missed} of {len(BUGS)} bugs caught')
    return 1 if missed else 0


if __name__ == '__main__':
    sys.exit(main())
