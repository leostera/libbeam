// SPDX-License-Identifier: Apache-2.0
// Local-only demonstration: ordinary HTTP framing around a real libbeam Isolate.
#include <libbeam/engine.hpp>

#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <netinet/in.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

namespace {
constexpr std::size_t max_headers = 8192;
constexpr std::size_t max_body = 32768; // midgard.bytes.v1 request limit
constexpr std::size_t max_wire_request = 65536;
constexpr auto call_timeout = std::chrono::seconds(10);

struct Socket {
    int fd = -1;
    explicit Socket(int descriptor): fd(descriptor) {}
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept: fd(std::exchange(other.fd, -1)) {}
    ~Socket() { if (fd >= 0) ::close(fd); }
};

struct Reader {
    std::string_view bytes;
    std::size_t offset = 0;

    std::string_view take(std::size_t size) {
        if (size > bytes.size() - offset) throw std::runtime_error("truncated bundle or response");
        auto part = bytes.substr(offset, size);
        offset += size;
        return part;
    }
    unsigned integer(std::size_t size) {
        unsigned result = 0;
        for (unsigned char byte : take(size)) result = (result << 8) | byte;
        return result;
    }
    bool done() const { return offset == bytes.size(); }
};

std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open fixture: " + path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::map<std::string, std::string> read_bundle(const std::string& path) {
    const auto bytes = read_file(path);
    Reader reader{bytes};
    if (reader.take(8) != "LBAPP001") throw std::runtime_error("not an LBAPP001 fixture");
    const auto manifest_size = reader.integer(4);
    if (manifest_size > 1024 * 1024) throw std::runtime_error("manifest too large");
    reader.take(manifest_size);
    const auto count = reader.integer(4);
    if (count == 0 || count > 4096) throw std::runtime_error("invalid module count");
    std::map<std::string, std::string> modules;
    for (unsigned i = 0; i < count; ++i) {
        const auto name = reader.take(reader.integer(2));
        const auto length = reader.integer(4);
        if (name.empty() || length < 12 || length > 64u * 1024u * 1024u)
            throw std::runtime_error("invalid module record");
        const auto beam = reader.take(length);
        if (beam.substr(0, 4) != "FOR1" || beam.substr(8, 4) != "BEAM")
            throw std::runtime_error("invalid BEAM container");
        if (!modules.emplace(std::string(name), std::string(beam)).second)
            throw std::runtime_error("duplicate module name");
    }
    if (!reader.done()) throw std::runtime_error("trailing fixture bytes");
    return modules;
}

unsigned decimal(std::string_view text, unsigned maximum) {
    if (text.empty()) throw std::runtime_error("empty decimal value");
    unsigned value = 0;
    for (char digit : text) {
        if (digit < '0' || digit > '9' || value > (maximum - (digit - '0')) / 10)
            throw std::runtime_error("invalid or oversized decimal value");
        value = value * 10 + (digit - '0');
    }
    return value;
}

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

std::string lowercase(std::string name) {
    for (char& ch : name) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return name;
}

// This demo intentionally handles one bounded, non-chunked origin-form request per connection.
HttpRequest parse_head(std::string_view head, unsigned& body_length) {
    const auto line_end = head.find("\r\n");
    if (line_end == std::string_view::npos) throw std::runtime_error("missing HTTP request line");
    const auto line = head.substr(0, line_end);
    const auto first = line.find(' ');
    const auto second = first == std::string_view::npos ? first : line.find(' ', first + 1);
    if (first == std::string_view::npos || second == std::string_view::npos ||
        line.find(' ', second + 1) != std::string_view::npos ||
        (line.substr(second + 1) != "HTTP/1.1" && line.substr(second + 1) != "HTTP/1.0"))
        throw std::runtime_error("unsupported HTTP request line");
    const auto target = line.substr(first + 1, second - first - 1);
    if (target.empty() || target[0] != '/' || target.find('#') != std::string_view::npos)
        throw std::runtime_error("only origin-form paths are accepted");
    const auto separator = target.find('?');
    HttpRequest request{std::string(line.substr(0, first)),
                        std::string(target.substr(0, separator)),
                        separator == std::string_view::npos ? "" : std::string(target.substr(separator + 1)), {}, ""};
    body_length = 0;
    bool has_length = false;
    std::size_t position = line_end + 2;
    while (position < head.size()) {
        const auto end = head.find("\r\n", position);
        if (end == std::string_view::npos) throw std::runtime_error("invalid HTTP header line");
        const auto header = head.substr(position, end - position);
        if (header.empty()) break;
        const auto colon = header.find(':');
        if (colon == std::string_view::npos || colon == 0 || request.headers.size() == 255)
            throw std::runtime_error("invalid HTTP header");
        const auto name = header.substr(0, colon);
        if (name.find_first_of(" \t\r\n") != std::string_view::npos)
            throw std::runtime_error("invalid HTTP header name");
        auto value = header.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
        const auto normalized = lowercase(std::string(name));
        if (normalized.empty() || normalized.size() > 128 || value.size() > 4096 ||
            !std::all_of(normalized.begin(), normalized.end(), [](char ch) {
                return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-';
            })) throw std::runtime_error("header outside worker protocol");
        if (normalized == "transfer-encoding") throw std::runtime_error("chunked requests are not supported");
        if (normalized == "content-length") {
            if (has_length) throw std::runtime_error("duplicate Content-Length");
            body_length = decimal(value, max_body);
            has_length = true;
        }
        request.headers.emplace_back(normalized, value);
        position = end + 2;
    }
    return request;
}

HttpRequest read_http(int fd) {
    std::string data;
    char buffer[4096];
    auto header_end = data.find("\r\n\r\n");
    while (header_end == std::string::npos) {
        const auto read = ::recv(fd, buffer, sizeof(buffer), 0);
        if (read < 0 && errno == EINTR) continue;
        if (read <= 0) throw std::runtime_error("HTTP request ended before headers");
        data.append(buffer, static_cast<std::size_t>(read));
        header_end = data.find("\r\n\r\n");
        if (header_end == std::string::npos && data.size() > max_headers)
            throw std::runtime_error("HTTP headers too large");
    }
    if (header_end + 4 > max_headers) throw std::runtime_error("HTTP headers too large");
    unsigned length;
    auto request = parse_head(std::string_view(data).substr(0, header_end + 4), length);
    const auto total = header_end + 4 + length;
    while (data.size() < total) {
        const auto read = ::recv(fd, buffer, std::min(sizeof(buffer), total - data.size()), 0);
        if (read < 0 && errno == EINTR) continue;
        if (read <= 0) throw std::runtime_error("HTTP request ended before body");
        data.append(buffer, static_cast<std::size_t>(read));
    }
    request.body.assign(data.data() + header_end + 4, length);
    return request;
}

void append16(std::string& data, std::size_t value) {
    if (value > 65535) throw std::runtime_error("wire field too large");
    data.push_back(static_cast<char>(value >> 8));
    data.push_back(static_cast<char>(value));
}

std::string encode_request(const HttpRequest& request) {
    if (request.method.empty() || request.method.size() > 16 ||
        !std::all_of(request.method.begin(), request.method.end(), [](char ch) {
            return ch >= 'A' && ch <= 'Z';
        }) || request.path.empty() || request.path.front() != '/' ||
        request.path.size() > 2048 || request.query.size() > 2048 ||
        request.body.size() > max_body || request.headers.size() > 32)
        throw std::runtime_error("request outside worker protocol");
    std::size_t header_bytes = 0;
    for (const auto& [name, value] : request.headers) {
        header_bytes += name.size() + value.size() + 3;
        if (header_bytes > 8192) throw std::runtime_error("worker headers too large");
    }
    std::string bytes(1, 1);
    bytes.push_back(static_cast<char>(request.method.size()));
    append16(bytes, request.path.size());
    append16(bytes, request.query.size());
    bytes.push_back(static_cast<char>(request.headers.size()));
    append16(bytes, request.body.size());
    bytes += request.method + request.path + request.query;
    for (const auto& [name, value] : request.headers) {
        if (name.size() > 255) throw std::runtime_error("HTTP header name too large");
        bytes.push_back(static_cast<char>(name.size()));
        append16(bytes, value.size());
        bytes += name + value;
    }
    bytes += request.body;
    if (bytes.size() > max_wire_request) throw std::runtime_error("worker request too large");
    return bytes;
}

struct WorkerResponse { unsigned status; std::string body; };
WorkerResponse decode_response(const libbeam::Bytes& bytes) {
    Reader reader{{reinterpret_cast<const char*>(bytes.data()), bytes.size()}};
    if (reader.integer(1) != 2) throw std::runtime_error("invalid worker response version");
    const auto status = reader.integer(2);
    const auto body = reader.take(reader.integer(2));
    if (status < 100 || status > 599 || !reader.done())
        throw std::runtime_error("invalid worker response framing");
    return {status, std::string(body)};
}

std::string_view reason(unsigned status) {
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 413: return "Content Too Large";
    case 500: return "Internal Server Error";
    default: return "Worker Response";
    }
}

void send_all(int fd, std::string_view bytes) {
    while (!bytes.empty()) {
        const auto written = ::send(fd, bytes.data(), bytes.size(), 0);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) throw std::runtime_error("HTTP client disconnected");
        bytes.remove_prefix(static_cast<std::size_t>(written));
    }
}

void send_http(int fd, const WorkerResponse& response, std::string_view path) {
    const auto content_type = response.status == 200 && path == "/json" ?
        "application/json; charset=utf-8" : "text/plain; charset=utf-8";
    const std::string header = "HTTP/1.1 " + std::to_string(response.status) + " " +
        std::string(reason(response.status)) + "\r\nContent-Type: " + content_type +
        "\r\nContent-Length: " + std::to_string(response.body.size()) +
        "\r\nConnection: close\r\n\r\n";
    send_all(fd, header);
    send_all(fd, response.body);
}

libbeam::Deadline deadline() { return std::chrono::steady_clock::now() + call_timeout; }

template<class T> T take(libbeam::Result<T> result) {
    if (auto* error = std::get_if<libbeam::Error>(&result))
        throw std::runtime_error(error->message);
    return std::get<T>(std::move(result));
}

std::string hex(std::string_view bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}

void serve(int fd, libbeam::Isolate& isolate, bool& started) {
    HttpRequest request;
    try {
        request = read_http(fd);
    } catch (const std::runtime_error& error) {
        send_http(fd, {400, "Bad Request\n"}, "");
        std::cerr << "rejected HTTP request: " << error.what() << '\n';
        return;
    }
    std::string wire;
    try {
        wire = encode_request(request);
    } catch (const std::runtime_error& error) {
        send_http(fd, {413, "Request Too Large\n"}, "");
        std::cerr << "rejected oversized request: " << error.what() << '\n';
        return;
    }
    const libbeam::ByteView input{reinterpret_cast<const std::uint8_t*>(wire.data()), wire.size()};
    auto call = take(started ? isolate.call("Elixir.HelloWorker", "__midgard_call__", input)
                             : isolate.start("Elixir.HelloWorker", "__midgard_call__", input));
    started = true;
    const auto wire_response = take(call.wait_until(deadline()));
    const auto response = decode_response(wire_response);
    send_http(fd, response, request.path);
    std::cerr << request.method << ' ' << request.path << " -> " << response.status;
    if (wire_response.size() <= 256)
        std::cerr << " wire_response_hex=" << hex({reinterpret_cast<const char*>(wire_response.data()), wire_response.size()});
    std::cerr << '\n';
}

Socket listen_local(unsigned port) {
    Socket server(::socket(AF_INET, SOCK_STREAM, 0));
    if (server.fd < 0) throw std::runtime_error("socket failed");
    const int reuse = 1;
    if (::setsockopt(server.fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0)
        throw std::runtime_error("setsockopt failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::bind(server.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(server.fd, 8) != 0) throw std::runtime_error("bind/listen failed");
    socklen_t length = sizeof(address);
    if (::getsockname(server.fd, reinterpret_cast<sockaddr*>(&address), &length) != 0)
        throw std::runtime_error("getsockname failed");
    std::cout << "READY http://127.0.0.1:" << ntohs(address.sin_port) << std::endl;
    return server;
}

void codec_self_check() {
    unsigned length = 0;
    const auto get = parse_head("GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n", length);
    if (length != 0 || get.headers != std::vector<std::pair<std::string, std::string>>{{"host", "localhost"}})
        throw std::runtime_error("header normalization regression");
    const auto encoded_bytes = encode_request(get);
    Reader encoded{encoded_bytes};
    if (encoded.integer(1) != 1 || encoded.integer(1) != 3 || encoded.integer(2) != 6 ||
        encoded.integer(2) != 0 || encoded.integer(1) != 1 || encoded.integer(2) != 0 ||
        encoded.take(3) != "GET" || encoded.take(6) != "/hello" ||
        encoded.integer(1) != 4 || encoded.integer(2) != 9 ||
        encoded.take(4) != "host" || encoded.take(9) != "localhost" || !encoded.done())
        throw std::runtime_error("request codec regression");
    auto post = parse_head("POST /json HTTP/1.1\r\nContent-Length: 14\r\n\r\n", length);
    if (length != 14 || post.path != "/json") throw std::runtime_error("POST framing regression");
    post.body = R"({"name":"Leo"})";
    const auto post_bytes = encode_request(post);
    Reader post_wire{post_bytes};
    if (post_wire.integer(1) != 1 || post_wire.integer(1) != 4 ||
        post_wire.integer(2) != 5 || post_wire.integer(2) != 0 ||
        post_wire.integer(1) != 1 || post_wire.integer(2) != 14 ||
        post_wire.take(4) != "POST" || post_wire.take(5) != "/json" ||
        post_wire.integer(1) != 14 || post_wire.integer(2) != 2 ||
        post_wire.take(14) != "content-length" || post_wire.take(2) != "14" ||
        post_wire.take(14) != post.body || !post_wire.done())
        throw std::runtime_error("POST request codec regression");
    libbeam::Bytes reply{2, 0, 200, 0, 14};
    for (unsigned char byte : std::string_view("Hello, world!\n")) reply.push_back(byte);
    const auto decoded = decode_response(reply);
    if (decoded.status != 200 || decoded.body != "Hello, world!\n")
        throw std::runtime_error("response codec regression");
    bool rejected_duplicate = false;
    try {
        parse_head("POST /json HTTP/1.1\r\nContent-Length: 0\r\nContent-Length: 1\r\n\r\n", length);
    } catch (const std::runtime_error&) { rejected_duplicate = true; }
    if (!rejected_duplicate) throw std::runtime_error("duplicate length accepted");
}

bool shutdown(std::optional<libbeam::Isolate>& isolate, std::optional<libbeam::Engine>& engine) {
    bool okay = true;
    if (isolate) {
        auto stopped = isolate->stop();
        if (auto* error = std::get_if<libbeam::Error>(&stopped)) {
            std::cerr << "stop failed: " << error->message << '\n';
            okay = false;
        } else if (auto error = std::get<libbeam::Reclamation>(std::move(stopped)).wait_until(deadline())) {
            std::cerr << "reclamation failed: " << error->message << '\n';
            okay = false;
        }
        auto usage = isolate->resource_usage();
        if (auto* record = std::get_if<libbeam::ResourceUsage>(&usage)) {
            std::cerr << "Isolate closed=" << record->closed << " memory_current=" << record->memory_current << '\n';
            okay &= record->closed && record->memory_current == 0;
        } else { okay = false; }
        isolate.reset();
    }
    if (engine) {
        if (auto error = engine->shutdown(deadline())) {
            std::cerr << "engine shutdown failed: " << error->message << '\n';
            okay = false;
        }
        engine.reset();
    }
    return okay;
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--self-check") {
        try {
            codec_self_check();
            std::cout << "codec self-check PASS (no Isolate was started)\n";
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "codec self-check failed: " << error.what() << '\n';
            return 1;
        }
    }
    if (argc != 4 && argc != 5) {
        std::cerr << "Usage: bridge HELLO_WORKER.bundle PORT REQUEST_COUNT [MEMORY_MiB]\n";
        return 2;
    }
    std::signal(SIGPIPE, SIG_IGN); // Client disconnects must not skip Isolate reclamation.
    std::optional<libbeam::Engine> engine;
    std::optional<libbeam::Isolate> isolate;
    bool okay = false;
    try {
        const unsigned port = decimal(argv[2], 65535);
        const unsigned requests = decimal(argv[3], 1000);
        const unsigned memory_mib = argc == 5 ? decimal(argv[4], 2048) : 256;
        if (requests == 0 || memory_mib < 256)
            throw std::runtime_error("REQUEST_COUNT must be positive and MEMORY_MiB at least 256");
        const auto modules = read_bundle(argv[1]);
        engine.emplace(take(libbeam::Engine::create({static_cast<std::size_t>(memory_mib) * 1024u * 1024u,
                                                     10'000'000})));
        isolate.emplace(take(engine->create_isolate()));
        std::vector<libbeam::ByteView> beams;
        for (const auto& [name, bytes] : modules)
            beams.push_back({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
        if (auto error = isolate->load_modules(beams)) {
            if (auto usage = isolate->resource_usage();
                auto* record = std::get_if<libbeam::ResourceUsage>(&usage))
                std::cerr << "Admission usage memory_current=" << record->memory_current
                          << " memory_peak=" << record->memory_peak
                          << " memory_limit=" << record->limits.memory_limit_bytes << '\n';
            throw std::runtime_error("whole-group admission: " + error->message);
        }
        auto server = listen_local(port);
        bool started = false;
        for (unsigned i = 0; i < requests; ++i) {
            Socket client(::accept(server.fd, nullptr, nullptr));
            if (client.fd < 0) {
                if (errno == EINTR) { --i; continue; }
                throw std::runtime_error("accept failed");
            }
            serve(client.fd, *isolate, started);
        }
        okay = true;
    } catch (const std::exception& error) {
        std::cerr << "bridge failed: " << error.what() << '\n';
    }
    const bool reclaimed = shutdown(isolate, engine);
    return okay && reclaimed ? 0 : 1;
}
