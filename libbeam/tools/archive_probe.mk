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

# Internal component tests must use the same private headers/defines as ERTS.
.PHONY: libbeam-print-compile-settings
libbeam-print-compile-settings:
	@printf '%s\n' 'CC=$(CC)' 'CFLAGS=$(CFLAGS)' 'INCLUDES=$(INCLUDES)'

# Read after OTP's generated emulator Makefile. Use its dependency/link settings,
# rather than guessing libraries or copying an arbitrary set of emulator objects.
.PHONY: libbeam-print-link-settings
libbeam-print-link-settings:
	@printf '%s\n' 'ARCHIVE=$(BINDIR)/$(EMULATOR_LIB)' 'CXX=$(CXX)' 'FLAGS=$(PROFILE_LDFLAGS) $(LDFLAGS) $(EMU_LDFLAGS) $(DEXPORT)' 'LIBS=$(STATIC_NIF_LIBS) $(STATIC_DRIVER_LIBS) $(LIBS)'
