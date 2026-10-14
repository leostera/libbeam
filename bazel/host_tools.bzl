"""Local generator toolchain. No hidden PATH lookup inside build actions."""

def _host_tools(ctx):
    perl = ctx.which("perl")
    erl = ctx.which("erl")
    nm = ctx.which("nm")
    elixir = ctx.which("elixir")
    if not perl or not erl or not nm or not elixir:
        fail("Install Perl, Erlang/OTP 28, Elixir >= 1.19 and native binutils, then rerun Bazel")
    otp = ctx.execute([erl, "+S", "1:1", "+SDcpu", "1:1", "+SDio", "1", "+A", "0", "-noshell", "-eval", 'io:format("~s",[code:root_dir()]),halt().'])
    if otp.return_code:
        fail("Cannot locate OTP: " + otp.stderr)
    # Declare the complete installed compiler/runtime tree, including compiler
    # BEAMs, rather than caching erlc outputs based only on a launcher script.
    ctx.symlink(otp.stdout, "otp")
    elixir_root = ctx.execute([elixir, "--erl", "+S 1:1 +SDcpu 1:1 +SDio 1 +A 0", "-e", 'IO.write(Path.expand("../..", to_string(:code.lib_dir(:elixir))))'])
    if elixir_root.return_code:
        fail("Cannot locate Elixir: " + elixir_root.stderr)
    ctx.symlink(elixir_root.stdout, "elixir")
    ctx.symlink(perl, "perl")
    ctx.symlink(nm, "nm")
    ctx.file("identity.txt", "OTP=" + otp.stdout + "\n")
    ctx.file("BUILD.bazel", '''
package(default_visibility = ["//visibility:public"])
exports_files(["perl", "nm", "identity.txt", "otp/bin/erlc", "otp/bin/erl", "otp/bin/escript", "elixir/bin/elixir"])
filegroup(name = "otp", srcs = glob(["otp/**"]))
filegroup(name = "elixir", srcs = glob(["elixir/**"]))
''')

host_tools = repository_rule(implementation = _host_tools, local = True, environ = ["PATH"])
