#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Compile selected complete pinned preloaded functions as ordinary BEAM."""
import argparse
import hashlib
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--erlc', default='erlc')
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2] / 'beam/erts/preloaded/src/erlang.erl'
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != '81dacbaae40928bb5fcda57024b66af4a059e68b536aede0c891a5dd868c5ecd':
        raise RuntimeError('Preloaded source changed; review provenance')
    text = data.decode()
    functions = []
    for start in ('spawn(F) when erlang:is_function(F) ->',
                  'spawn_monitor(F) when erlang:is_function(F, 0) ->',
                  'badarg_with_info(Args) ->'):
        offset = text.index('\n' + start) + 1
        end = text.index('.\n', offset) + 2
        functions.append(text[offset:end])
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    guest = output / 'erlang.erl'
    guest.write_text('%% SPDX-License-Identifier: Apache-2.0\n'
                     '%% Copyright Ericsson AB 1996-2026. All Rights Reserved.\n'
                     '%% Complete functions extracted without body changes.\n'
                     '-module(erlang).\n-compile(no_auto_import).\n'
                     '-export([spawn/1,spawn_monitor/1]).\n' + '\n'.join(functions))
    subprocess.run([args.erlc, '-o', str(output), str(guest)], check=True)
    beam = (output / 'erlang.beam').read_bytes()
    if beam[:4] != b'FOR1' or beam[8:12] != b'BEAM':
        raise RuntimeError('Compiler did not produce ordinary BEAM')
    body = '\n'.join(','.join(str(byte) for byte in beam[i:i+24]) + ','
                     for i in range(0, len(beam), 24))
    (output / 'lb_preloaded_generated.inc').write_text(
        '/* SPDX-License-Identifier: Apache-2.0\n'
        ' * Copyright Ericsson AB 1996-2026. All Rights Reserved.\n'
        ' * Compiler-produced BEAM; not an alternate instruction encoding. */\n'
        'static const unsigned char lb_preloaded_erlang[] = {\n' + body + '\n};\n')


if __name__ == '__main__':
    main()
