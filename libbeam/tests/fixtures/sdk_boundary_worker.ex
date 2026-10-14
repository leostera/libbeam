# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
defmodule BoundaryWorker do
  use Midgard.Worker

  @impl true
  def handle_request(%Midgard.Request{path: "/raise"}), do: raise("ordinary callback failure")
  def handle_request(%Midgard.Request{path: "/invalid"}), do: :not_a_response
  def handle_request(%Midgard.Request{path: "/deny"}), do: :erlang.load_nif(~c"must-not-load", 0)
  def handle_request(_request), do: {200, "still usable\n"}
end
