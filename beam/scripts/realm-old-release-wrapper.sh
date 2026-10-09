#!/bin/sh
# %CopyrightBegin%
#
# SPDX-License-Identifier: Apache-2.0
#
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# %CopyrightEnd%

# Invoke through temporary erl/erlc symlinks for CT's installed-release discovery.
# This is a test adapter, not a global change to the operator's shell environment.
case "${0##*/}" in
    erl|erlc) tool=${0##*/} ;;
    *) echo 'Invoke through an erl or erlc symlink' >&2; exit 2 ;;
esac
case "${REALM_OLD_RELEASE_BIN:-}" in
    /*) ;;
    *) echo 'REALM_OLD_RELEASE_BIN must name an absolute installed bin directory' >&2; exit 2 ;;
esac
target="$REALM_OLD_RELEASE_BIN/$tool"
if [ "$target" -ef "$0" ]; then
    echo 'Old-release target resolves to this wrapper' >&2
    exit 2
fi
unset ERL_AFLAGS ERL_FLAGS ERL_ZFLAGS ERL_LIBS
exec "$target" "$@"
