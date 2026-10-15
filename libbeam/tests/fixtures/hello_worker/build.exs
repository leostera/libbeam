# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
# Build inputs only: application requests are executed by the native tests.
defmodule HelloWorkerFixture do
  def build(source, output) do
    # OTP's filename-based disassembler rejects long sandbox paths. Keep its
    # scratch artifact path short without changing any application/tool source.
    workspace = Path.join(System.tmp_dir!(), "hello-worker-#{System.pid()}-#{System.unique_integer([:positive])}")
    File.mkdir!(workspace)
    Code.compiler_options(ignore_module_conflict: true, relative_paths: true)

    try do
      compile(Path.wildcard(source <> "/packer/*.ex"), workspace <> "/packer")
      modules =
        for owner <- ["midgard", "jason", "app"],
            file <- compile(Path.wildcard(source <> "/#{owner}/*.ex"), workspace <> "/#{owner}") do
          %{name: Atom.to_string(file), owner: if(owner == "app", do: "hello_worker", else: owner),
            bytes: File.read!(workspace <> "/#{owner}/#{file}.beam")}
        end

      artifact = workspace <> "/hello_worker.midgard"
      dependencies = for {owner, version} <- [{"jason", "1.4.5"}, {"midgard", "0.1.0"}] do
        %{app: owner, version: version, modules: for(module <- modules, module.owner == owner, do: module.name)}
      end
      {:ok, ^artifact} = Packer.Artifact.build(artifact, "hello_worker", "Elixir.HelloWorker", modules, dependencies)
      manifest = File.read!(artifact <> "/manifest.json")
      roots = Map.new(modules, &{String.to_atom(&1.name), &1.bytes})
      closure = closure(Map.keys(roots), roots, %{}) |> Enum.sort_by(fn {name, _} -> Atom.to_string(name) end)
      if length(closure) > 4096, do: raise("closure exceeds batch module bound")

      # Length-delimited original BEAMs. Never rename modules or remove chunks,
      # functions, imports or dependencies to make admission succeed.
      records = for {name, beam} <- closure do
        name = Atom.to_string(name)
        <<byte_size(name)::16, name::binary, byte_size(beam)::32, beam::binary>>
      end
      if IO.iodata_length(records) > 64 * 1024 * 1024, do: raise("closure exceeds batch byte bound")
      File.write!(output, ["LBAPP001", <<byte_size(manifest)::32>>, manifest, <<length(closure)::32>>, records])
    after
      File.rm_rf!(workspace)
    end
  end

  defp compile(files, destination) do
    File.mkdir_p!(destination)
    # Preserve the declared source's real identity rather than embedding the
    # temporary sandbox symlink name in executable literals and debug chunks.
    sources = Enum.map(files, &source_path/1)
    {:ok, modules, _warnings} = Kernel.ParallelCompiler.compile_to_path(sources, destination, return_diagnostics: true)
    true = Code.prepend_path(destination)
    modules
  end

  defp source_path(file) do
    case File.read_link(file) do
      {:ok, target} -> source_path(Path.expand(target, Path.dirname(file)))
      {:error, :einval} -> Path.expand(file)
      {:error, reason} -> raise File.Error, reason: reason, action: "resolve declared source", path: file
    end
  end

  defp closure([], _roots, found), do: found
  defp closure([:erlang | rest], roots, found), do: closure(rest, roots, found)
  defp closure([name | rest], roots, found) when is_map_key(found, name), do: closure(rest, roots, found)
  defp closure([name | rest], roots, found) do
    bytes = case Map.fetch(roots, name) do
      {:ok, bytes} -> bytes
      :error ->
        case :code.where_is_file(Atom.to_charlist(name) ++ ~c".beam") do
          path when is_list(path) -> File.read!(path)
          _ -> raise("missing dependency #{name}")
        end
    end
    {:ok, {^name, [imports: imports]}} = :beam_lib.chunks(bytes, [:imports])
    {:beam_file, ^name, _, _, _, code} = :beam_disasm.file(bytes)
    dependencies = Enum.map(imports, fn {module, _, _} -> module end)
    external_funs = external_fun_modules(code)
    closure(external_funs ++ dependencies ++ rest, roots, Map.put(found, name, bytes))
  end

  defp external_fun_modules(term) when is_function(term) do
    case :erlang.fun_info(term, :type) do
      {:type, :external} ->
        case :erlang.fun_info_mfa(term) do
          {module, _function, _arity} -> [module]
        end
      _ -> []
    end
  end
  defp external_fun_modules(term) when is_tuple(term),
    do: term |> Tuple.to_list() |> external_fun_modules()
  defp external_fun_modules([head | tail]),
    do: external_fun_modules(head) ++ external_fun_modules(tail)
  defp external_fun_modules(term) when is_map(term),
    do: term |> Map.to_list() |> external_fun_modules()
  defp external_fun_modules(_term), do: []
end

[source, output] = System.argv()
HelloWorkerFixture.build(source, output)
