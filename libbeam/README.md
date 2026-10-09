<!--
%CopyrightBegin%

SPDX-License-Identifier: Apache-2.0

Copyright 2026 Leandro Ostera <leandro@ostera.io>

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

%CopyrightEnd%
-->

# C++ embedding layer (planned)

This directory will contain libbeam's public host interface, implementation,
example executable and integration tests. Core emulator changes belong in
[`../beam/`](../beam/), not in a second copy of the VM here.

[RFD 0002](../docs/rfds/0002-libbeam-isolates.md) defines the first proof and its
acceptance criteria. No engine header, CMake target or runnable example exists yet.
The initial interface is intended to manage one engine and multiple freshly created
isolates with bounded binary requests/results and explicit stop/reclamation.

Keep HTTP routing and deployment orchestration above this library. Do not implement
"isolates" by spawning `erl` subprocesses, assigning prebooted instances or renaming
application modules.
