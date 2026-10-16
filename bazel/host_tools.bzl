"""Local generator toolchain. No hidden PATH lookup inside build actions."""

def _host_tools(ctx):
    perl = ctx.which("perl")
    erl = ctx.which("erl")
    nm = ctx.which("nm")
    elixir = ctx.which("elixir")
    bazel = ctx.which("bazel")
    if not perl or not erl or not nm or not elixir or not bazel:
        fail("Install Bazel, Perl, Erlang/OTP 29 or newer, Elixir >= 1.19 and native binutils, then rerun Bazel")
    otp = ctx.execute([erl, "+S", "1:1", "+SDcpu", "1:1", "+SDio", "1", "+A", "0", "-noshell", "-eval", 'io:format("~s~n~s",[erlang:system_info(otp_release),code:root_dir()]),halt().'])
    if otp.return_code:
        fail("Cannot locate OTP: " + otp.stderr)
    otp_identity = otp.stdout.split("\n")
    if len(otp_identity) != 2 or int(otp_identity[0]) < 29:
        fail("libbeam requires Erlang/OTP 29 or newer; found " + otp.stdout)
    # Declare the complete installed compiler/runtime tree, including compiler
    # BEAMs, rather than caching erlc outputs based only on a launcher script.
    otp_version = otp_identity[0]
    otp_root = otp_identity[1]
    ctx.symlink(otp_root, "otp")
    elixir_root = ctx.execute([elixir, "--erl", "+S 1:1 +SDcpu 1:1 +SDio 1 +A 0", "-e", 'IO.write(Path.expand("../..", to_string(:code.lib_dir(:elixir))))'])
    if elixir_root.return_code:
        fail("Cannot locate Elixir: " + elixir_root.stderr)
    ctx.symlink(elixir_root.stdout, "elixir")
    ctx.symlink(perl, "perl")
    ctx.symlink(nm, "nm")
    ctx.symlink(bazel, "bazel")
    ctx.file("identity.txt", "OTP=" + otp_version + "\nROOT=" + otp_root + "\n")
    ctx.file("BUILD.bazel", '''
package(default_visibility = ["//visibility:public"])
exports_files(["perl", "nm", "bazel", "identity.txt", "otp/bin/erlc", "otp/bin/erl", "otp/bin/escript", "elixir/bin/elixir"])
filegroup(name = "otp", srcs = glob(["otp/**"]))
filegroup(name = "elixir", srcs = glob(["elixir/**"]))
''')

host_tools = repository_rule(implementation = _host_tools, local = True, environ = ["PATH"])
