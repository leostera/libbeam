%%
%% %CopyrightBegin%
%%
%% SPDX-License-Identifier: Apache-2.0
%%
%% Copyright Ericsson AB 1997-2026. All Rights Reserved.
%%
%% Licensed under the Apache License, Version 2.0 (the "License");
%% you may not use this file except in compliance with the License.
%% You may obtain a copy of the License at
%%
%%     http://www.apache.org/licenses/LICENSE-2.0
%%
%% Unless required by applicable law or agreed to in writing, software
%% distributed under the License is distributed on an "AS IS" BASIS,
%% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
%% See the License for the specific language governing permissions and
%% limitations under the License.
%%
%% %CopyrightEnd%
%%
-module(os).
-moduledoc """
Operating system-specific functions.

The functions in this module are operating system-specific. Careless use of
these functions results in programs that will only run on a specific platform.
On the other hand, with careful use, these functions can be of help in enabling
a program to run on most platforms.

> #### Note {: .info }
>
> The functions in this module will raise a `badarg` exception if their
> arguments contain invalid characters according to the description in the "Data
> Types" section.
""".

%% Provides a common operating system interface.

-export([type/0, version/0, find_executable/1, find_executable/2]).

-include("file.hrl").

-export_type([env_var_name/0, env_var_value/0, env_var_name_value/0]).

-export([getenv/0, getenv/1, getenv/2, putenv/2, unsetenv/1,
         getpid/0, env/0, perf_counter/0,
         perf_counter/1, system_time/0,
         system_time/1, timestamp/0]).

-doc """
A string containing valid characters on the specific OS for environment variable
names using [`file:native_name_encoding()`](`file:native_name_encoding/0`)
encoding.

Null characters (integer value zero) are not allowed. On Unix, `=`
characters are not allowed. On Windows, a `=` character is only allowed as the
very first character in the string.
""".
-type env_var_name() :: nonempty_string().

-doc """
A string containing valid characters on the specific OS for environment variable
values using [`file:native_name_encoding()`](`file:native_name_encoding/0`)
encoding.

Null characters (integer value zero) are not allowed.
""".
-type env_var_value() :: string().

-doc """
Assuming that environment variables has been correctly set, a strings containing
valid characters on the specific OS for environment variable names and values
using [`file:native_name_encoding()`](`file:native_name_encoding/0`) encoding.

The first `=` characters appearing in the string separates environment variable
name (on the left) from environment variable value (on the right).
""".
-type env_var_name_value() :: nonempty_string().

%% We must inline these functions so that the stacktrace points to
%% the correct function.
-compile({inline, [badarg_with_info/1]}).

-doc """
Returns a list of all environment variables. Each environment variable is
expressed as a tuple `{VarName,Value}`, where `VarName` is the name of the
variable and `Value` its value.

If Unicode filename encoding is in effect (see the
[`erl` manual page](`e:erts:erl_cmd.md#file_name_encoding`)), the strings can
contain characters with codepoints > 255.
""".
-doc(#{since => <<"OTP 24.0">>}).
-spec env() -> [{env_var_name(), env_var_value()}].
env() ->
    erlang:nif_error(undef).

-doc """
Returns the `Value` of the environment variable `VarName`, or `false` if the
environment variable is undefined.

If Unicode filename encoding is in effect (see the
[`erl` manual page](`e:erts:erl_cmd.md#file_name_encoding`)), the strings
`VarName` and `Value` can contain characters with codepoints > 255.
""".
-spec getenv(VarName) -> Value | false when
      VarName :: env_var_name(),
      Value :: env_var_value().
getenv(_VarName) ->
    erlang:nif_error(undef).

-doc """
Returns the process identifier of the current Erlang emulator in the format most
commonly used by the OS environment.

Returns `Value` as a string containing the (usually) numerical identifier for a process.

- On Unix, this is typically the return value of the `getpid/0` system call.
- On Windows, the process id as returned by the `GetCurrentProcessId()` system call
  is used.
""".
-spec getpid() -> Value when
      Value :: string().

getpid() ->
    erlang:nif_error(undef).

-doc """
Returns the current performance counter value in `perf_counter`
[time unit](`t:erlang:time_unit/0`). This is a highly optimized call that
might not be traceable.
""".
-doc(#{since => <<"OTP 19.0">>}).
-spec perf_counter() -> Counter when
      Counter :: integer().

perf_counter() ->
    erlang:nif_error(undef).

-doc """
Returns a performance counter that can be used as a very fast and high
resolution timestamp.

This counter is read directly from the hardware or operating system with the
same guarantees. This means that two consecutive calls to the function are not
guaranteed to be monotonic, though it most likely will be. The performance
counter will be converted to the resolution passed as an argument.

```erlang
1> T1 = os:perf_counter(1000),receive after 10000 -> ok end,T2 = os:perf_counter(1000).
176525861
2> T2 - T1.
10004
```
""".
-doc(#{since => <<"OTP 19.0">>}).
-spec perf_counter(Unit) -> integer() when
      Unit :: erlang:time_unit().

perf_counter(Unit) ->
    try
        erlang:convert_time_unit(os:perf_counter(), perf_counter, Unit)
    catch
        error:_ ->
            badarg_with_info([Unit])
    end.

-doc """
Sets a new `Value` for environment variable `VarName`.

If Unicode filename encoding is in effect (see the
[`erl` manual page](`e:erts:erl_cmd.md#file_name_encoding`)), the strings
`VarName` and `Value` can contain characters with codepoints > 255.

On Unix platforms, the environment is set using UTF-8 encoding if Unicode
filename translation is in effect. On Windows, the environment is set using wide
character interfaces.
""".
-spec putenv(VarName, Value) -> true when
      VarName :: env_var_name(),
      Value :: env_var_value().
putenv(_VarName, _Value) ->
    erlang:nif_error(undef).

-doc """
Returns the current [OS system time](`e:erts:time_correction.md#os-system-time`)
in `native` [time unit](`t:erlang:time_unit/0`).

> #### Note {: .info }
>
> This time is _not_ a monotonically increasing time.
""".
-doc(#{since => <<"OTP 18.0">>}).
-spec system_time() -> integer().

system_time() ->
    erlang:nif_error(undef).

-doc """
Returns the current [OS system time](`e:erts:time_correction.md#os-system-time`)
converted into the `Unit` passed as argument.

Calling `os:system_time(Unit)` is equivalent to
[`erlang:convert_time_unit`](`erlang:convert_time_unit/3`)([`os:system_time()`](`system_time/0`)`, native, Unit)`.

> #### Note {: .info }
>
> This time is _not_ a monotonically increasing time.
""".
-doc(#{since => <<"OTP 18.0">>}).
-spec system_time(Unit) -> integer() when
      Unit :: erlang:time_unit().

system_time(_Unit) ->
    erlang:nif_error(undef).

-doc """
Returns the current [OS system time](`e:erts:time_correction.md#os-system-time`)
in the same format as `erlang:timestamp/0`.

The tuple can be used together with function `calendar:now_to_universal_time/1`
or `calendar:now_to_local_time/1` to get calendar time. Using the calendar time,
together with the `MicroSecs` part of the return tuple from this function,
allows you to log time stamps in high resolution and consistent with the time in
 the rest of the OS.

Example of code formatting a string in format "DD Mon YYYY HH:MM:SS.mmmmmm",
where DD is the day of month, Mon is the textual month name, YYYY is the year,
HH:MM:SS is the time, and mmmmmm is the microseconds in six positions:

```erlang
-module(print_time).
-export([format_utc_timestamp/0]).
format_utc_timestamp() ->
    TS = {_,_,Micro} = os:timestamp(),
    {{Year,Month,Day},{Hour,Minute,Second}} =
calendar:now_to_universal_time(TS),
    Mstr = element(Month,{"Jan","Feb","Mar","Apr","May","Jun","Jul",
    "Aug","Sep","Oct","Nov","Dec"}),
    io_lib:format("~2w ~s ~4w ~2w:~2..0w:~2..0w.~6..0w",
    [Day,Mstr,Year,Hour,Minute,Second,Micro]).
```

This module can be used as follows:

```erlang
1> io:format("~s~n",[print_time:format_utc_timestamp()]).
29 Apr 2009  9:55:30.051711
```

OS system time can also be retrieved by `system_time/0` and `system_time/1`.
""".
-spec timestamp() -> Timestamp when
      Timestamp :: erlang:timestamp().

timestamp() ->
    erlang:nif_error(undef).

-doc """
Deletes the environment variable `VarName`.

If Unicode filename encoding is in effect (see the
[`erl` manual page](`e:erts:erl_cmd.md#file_name_encoding`)), the string
`VarName` can contain characters with codepoints > 255.
""".
-doc(#{since => <<"OTP R16B03">>}).
-spec unsetenv(VarName) -> true when
      VarName :: env_var_name().
unsetenv(_VarName) ->
    erlang:nif_error(undef).

%%% End of BIFs

-doc """
Returns a list of all environment variables. Each environment variable is
expressed as a single string on the format `"VarName=Value"`, where `VarName` is
the name of the variable and `Value` its value.

If Unicode filename encoding is in effect (see the
[`erl` manual page](`e:erts:erl_cmd.md#file_name_encoding`)), the strings can
contain characters with codepoints > 255.

Consider using `env/0` for a nicer 2-tuple format.
""".
-spec getenv() -> [env_var_name_value()].
getenv() ->
    [lists:flatten([Key, $=, Value]) || {Key, Value} <- os:env() ].

-doc """
Returns the `Value` of the environment variable `VarName`, or `DefaultValue` if
the environment variable is undefined.

If Unicode filename encoding is in effect (see the
[`erl` manual page](`e:erts:erl_cmd.md#file_name_encoding`)), the strings
`VarName` and `Value` can contain characters with codepoints > 255.
""".
-doc(#{since => <<"OTP 18.0">>}).
-spec getenv(VarName, DefaultValue) -> Value when
      VarName :: env_var_name(),
      DefaultValue :: env_var_value(),
      Value :: env_var_value().
getenv(VarName, DefaultValue) ->
    try os:getenv(VarName) of
        false ->
           DefaultValue;
        Value ->
            Value
    catch
        error:_ ->
            badarg_with_info([VarName, DefaultValue])
    end.

-doc """
Returns the `Osfamily` and, in some cases, the `Osname` of the current OS.

On Unix, `Osname` has the same value as `uname -s` returns, but in lower case.
For example, on Solaris 1 and 2, it is `sunos`.

On Windows, `Osname` is `nt`.

> #### Note {: .info }
>
> Think twice before using this function. Use module `m:filename` if you want to
> inspect or build filenames in a portable way. Avoid matching on atom `Osname`.
""".
-spec type() -> {Osfamily, Osname} when
      Osfamily :: unix | win32,
      Osname :: atom().

type() ->
    erlang:system_info(os_type).

-doc """
Returns the OS version. On most systems, this function returns a tuple, but a
string is returned instead if the system has versions that cannot be expressed
as three numbers.

> #### Note {: .info }
>
> Think twice before using this function. If you still need to use it, always
> `call os:type()` first.
""".
-spec version() -> VersionString | {Major, Minor, Release} when
      VersionString :: string(),
      Major :: non_neg_integer(),
      Minor :: non_neg_integer(),
      Release :: non_neg_integer().
version() ->
    erlang:system_info(os_version).

-doc """
Equivalent to [`find_executable(Name, Path)`](`find_executable/2`) where
`Path` is the current execution path (that is, the environment variable `PATH`
on Unix and Windows).
""".
-spec find_executable(Name) -> Filename | 'false' when
      Name :: string(),
      Filename :: string().
find_executable(Name) ->
    find_executable(Name, os:getenv("PATH", "")).

-doc """
Look up an executable program, with the specified name and a search path, in the
same way as the underlying OS.

`Path` is to conform to the syntax of execution paths on the OS.
Returns the absolute filename of the executable program `Name`, or `false` if
the program is not found.
""".
-spec find_executable(Name, Path) -> Filename | 'false' when
      Name :: string(),
      Path :: string(),
      Filename :: string().
find_executable(Name, Path) ->
    Extensions = extensions(),
    case filename:pathtype(Name) of
	relative ->
	    find_executable1(Name, split_path(Path), Extensions);
	_ ->
	    case verify_executable(Name, Extensions, Extensions) of
		{ok, Complete} ->
		    Complete;
		error ->
		    false
	    end
    end.

find_executable1(Name, [Base|Rest], Extensions) ->
    Complete0 = filename:join(Base, Name),
    case verify_executable(Complete0, Extensions, Extensions) of
	{ok, Complete} ->
	    Complete;
	error ->
	    find_executable1(Name, Rest, Extensions)
    end;
find_executable1(_Name, [], _Extensions) ->
    false.

verify_executable(Name0, [Ext|Rest], OrigExtensions) ->
    Name1 = Name0 ++ Ext,
    case file:read_file_info(Name1) of
	{ok, #file_info{type=regular,mode=Mode}}
	when Mode band 8#111 =/= 0 ->
	    %% XXX This test for execution permission is not fool-proof
	    %% on Unix, since we test if any execution bit is set.
	    {ok, Name1};
	_ ->
	    verify_executable(Name0, Rest, OrigExtensions)
    end;
verify_executable(Name, [], OrigExtensions) when OrigExtensions =/= [""] -> %% Windows
    %% Will only happen on windows, hence case insensitivity
    case can_be_full_name(string:lowercase(Name),OrigExtensions) of
	true ->
	    verify_executable(Name,[""],[""]);
	_ ->
	    error
    end;
verify_executable(_, [], _) ->
    error.

can_be_full_name(_Name,[]) ->
    false;
can_be_full_name(Name,[H|T]) ->
    case lists:suffix(H,Name) of %% Name is in lowercase, cause this is a windows thing
	true ->
	    true;
	_ ->
	    can_be_full_name(Name,T)
    end.

split_path(Path) ->
    case type() of
	{win32, _} ->
	    {ok,Curr} = file:get_cwd(),
	    split_path(Path, $;, [], [Curr]);
	_ ->
	    split_path(Path, $:, [], [])
    end.

split_path([Sep|Rest], Sep, Current, Path) ->
    split_path(Rest, Sep, [], [reverse_element(Current)|Path]);
split_path([C|Rest], Sep, Current, Path) ->
    split_path(Rest, Sep, [C|Current], Path);
split_path([], _, Current, Path) ->
    lists:reverse(Path, [reverse_element(Current)]).

reverse_element([]) -> ".";
reverse_element([$"|T]) ->	%"
    case lists:reverse(T) of
	[$"|List] -> List;	%"
	List -> List ++ [$"]	%"
    end;
reverse_element(List) ->
    lists:reverse(List).

-spec extensions() -> [string(),...].
%% Extensions in lower case
extensions() ->
    case type() of
	{win32, _} -> [".exe",".com",".cmd",".bat"];
	{unix, _} -> [""]
    end.

badarg_with_info(Args) ->
    erlang:error(badarg, Args, [{error_info, #{module => erl_kernel_errors}}]).
