# Run from the unchanged Midgard examples/hello_worker Mix project.
# This is an ordinary OTP oracle, never part of the Isolate process.
alias Midgard.{Protocol, Request}

for {label, request} <- [
      {"GET", %Request{method: "GET", path: "/hello"}},
      {"POST", %Request{method: "POST", path: "/json", body: ~s({"name":"Leo"})}}
    ] do
  {:ok, input} = Protocol.request(request)
  bytes = HelloWorker.__midgard_call__(input)
  {:ok, {200, _body}} = Protocol.decode_response(bytes)
  IO.puts("#{label}=#{Base.encode16(bytes, case: :lower)}")
end
