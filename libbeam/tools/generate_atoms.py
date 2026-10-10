#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Run the admitted OTP atom generator; compile only its immutable atom outputs."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--perl', default='perl')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    inputs = {
        root / 'tools/otp/make_tables': '4503d68bd345492db8127aad8c3ce584319a8355e3e1dafbbed2b680eb20d953',
        root / 'core/otp/atom.names': '4e6da90ce1effb6d922e01da32dcefd399366fb287b13d04924504d90e4fd309',
        root / 'core/otp/bif.tab': '39f32210feb58f9c60482d7b97db10d21c53a988207fd7a81221ff3506331564',
    }
    for path, digest in inputs.items():
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'Admitted generator input changed; review provenance: {path}')
    output = args.output.resolve()
    upstream = output / 'upstream'
    upstream.mkdir(parents=True, exist_ok=True)
    subprocess.run([args.perl, str(root / 'tools/otp/make_tables'), '-src', str(upstream),
                    '-include', str(upstream), str(root / 'core/otp/atom.names'),
                    str(root / 'core/otp/bif.tab')], check=True)
    source = (upstream / 'erl_atom_table.c').read_text()
    header = (upstream / 'erl_atom_table.h').read_text()
    declaration = 'char* erl_atom_names[]'
    external = 'extern char* erl_atom_names[];'
    if source.count(declaration) != 1 or header.count(external) != 1:
        raise RuntimeError('Unexpected upstream output; do not silently rewrite it')
    source = source.replace(declaration, 'static const char *const lb_predefined_names[]')
    header = header.replace(external, '/* Names are private immutable data in atoms.c. */')
    header = header.replace('__ERL_ATOM_TABLE_H__', 'LIBBEAM_GENERATED_ATOMS_H')
    # Input license/attribution banners remain intact in the admitted files;
    # upstream generated output only has a do-not-edit warning. Add attribution.
    notice = ('/* SPDX-License-Identifier: Apache-2.0\n'
              ' * Copyright Ericsson AB 1996-2026. All Rights Reserved.\n'
              ' * Copyright 2026 Leandro Ostera <leandro@ostera.io>\n'
              ' * Generated from admitted OTP naming inputs; not a BIF capability list.\n'
              ' */\n')
    # Only numeric identities, not BIF implementations or permission grants.
    bif_header = (upstream / 'erl_bif_table.h').read_text()
    ids = re.findall(r'^#define BIF_\w+ \d+$', bif_header, re.M)
    if not ids: raise RuntimeError('Missing BIF identities')
    (output / 'lb_bif_ids_generated.h').write_text(notice + '\n'.join(ids) + '\n')
    (output / 'lb_atoms_generated.inc').write_text(notice + source)
    (output / 'lb_atoms_generated.h').write_text(notice + header)


if __name__ == '__main__':
    main()
