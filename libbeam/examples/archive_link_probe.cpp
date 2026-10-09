// %CopyrightBegin%
//
// SPDX-License-Identifier: Apache-2.0
//
// Copyright 2026 Leandro Ostera <leandro@ostera.io>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// %CopyrightEnd%

#include <cstdio>

// Existing OTP entry point, not a new embedding API. Calling it would boot the
// global Erlang world and may take over/terminate this host. Do not call it here.
extern "C" void erl_start(int argc, char** argv);

using Start = void (*)(int, char**);
// Force a real relocation against the emulator archive; do not let optimization
// turn the probe into an executable that never links any emulator code.
static Start volatile linked_start = &erl_start;

int main() {
    if (linked_start == nullptr) {
        return 1;
    }
    std::puts("LIBBEAM_ARCHIVE_LINK_OK engine_started=false isolates_created=0");
    return 0;
}
