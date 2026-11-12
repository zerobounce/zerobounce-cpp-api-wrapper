// The real RequestHandler against a local socket server: redirects are not
// followed, and the total and connect timeouts are set on every request. These
// fail if a cpr::Redirect{false}, cpr::Timeout or cpr::ConnectTimeout is
// dropped from any of the handler's methods. A download whose body is cut
// short after a 200 header is an error, and nothing is saved.

#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <cpr/cpr.h>
#include <gtest/gtest.h>

#include "ZeroBounce/ZeroBounce.h"
#include "ZeroBounce/ZeroBounceWarmup.h"

#ifndef _WIN32

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

/**
 * A loopback server. In Redirect mode it answers every request with a 302 to
 * /landed and records the paths it saw. In Stall mode it accepts connections
 * and never answers, closing each one itself after kStallMs so that a client
 * without a timeout still comes back (late) instead of hanging the suite.
 * In Truncate mode it answers 200 with a Content-Length of 1000, sends only
 * kPartialBody, and closes the connection; TruncateStall sends the same and
 * then holds the connection open like Stall. TruncateJson is Truncate with
 * kPartialJson, a JSON object that parses on its own, as the body.
 */
class LocalServer {
public:
    enum class Mode { Redirect, Stall, Truncate, TruncateStall, TruncateJson };
    static constexpr int kStallMs = 4000;

    /** stallMs: how long Stall and TruncateStall hold a connection. */
    explicit LocalServer(Mode mode, int stallMs = kStallMs) : mode_(mode), stallMs_(stallMs) {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int yes = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            ::listen(listenFd_, 16) != 0) {
            ADD_FAILURE() << "could not listen on 127.0.0.1";
            return;
        }
        // Non-blocking, so a client that gives up between poll() and accept()
        // cannot leave the server thread stuck in accept().
        ::fcntl(listenFd_, F_SETFL, ::fcntl(listenFd_, F_GETFL, 0) | O_NONBLOCK);
        socklen_t len = sizeof(addr);
        ::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] { run(); });
    }

    ~LocalServer() {
        stop_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
        ::close(listenFd_);
    }

    std::string url(const std::string& scheme, const std::string& path) const {
        return scheme + "://127.0.0.1:" + std::to_string(port_) + path;
    }

    std::vector<std::string> paths() {
        std::lock_guard<std::mutex> lock(mu_);
        return paths_;
    }

private:
    struct Held {
        int fd;
        std::chrono::steady_clock::time_point until;
    };

    void run() {
        std::vector<Held> held;
        // The loop polls with a short timeout rather than blocking in accept(),
        // so the destructor never waits on a socket to wake it.
        while (!stop_) {
            pollfd pfd{listenFd_, POLLIN, 0};
            if (::poll(&pfd, 1, 20) > 0 && (pfd.revents & POLLIN)) {
                int fd = ::accept(listenFd_, nullptr, nullptr);
                if (fd >= 0) {
                    if (mode_ == Mode::Redirect) {
                        answerRedirect(fd);
                    } else if (mode_ == Mode::Truncate) {
                        answerPartially(fd, "text/csv", kPartialBody);
                        ::close(fd);
                    } else if (mode_ == Mode::TruncateJson) {
                        answerPartially(fd, "application/json", kPartialJson);
                        ::close(fd);
                    } else if (mode_ == Mode::TruncateStall) {
                        answerPartially(fd, "text/csv", kPartialBody);
                        held.push_back({fd, std::chrono::steady_clock::now() + std::chrono::milliseconds(stallMs_)});
                    } else {
                        held.push_back({fd, std::chrono::steady_clock::now() + std::chrono::milliseconds(stallMs_)});
                    }
                }
            }
            auto now = std::chrono::steady_clock::now();
            for (auto it = held.begin(); it != held.end();) {
                if (now >= it->until) {
                    ::close(it->fd);
                    it = held.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& h : held) {
            ::close(h.fd);
        }
    }

    void answerPartially(int fd, const std::string& contentType, const std::string& body) {
        readRequest(fd);
        const std::string response = "HTTP/1.1 200 OK\r\nContent-Type: " + contentType +
            "\r\nContent-Length: 1000\r\nConnection: close\r\n\r\n" + body;
        ::send(fd, response.data(), response.size(), 0);
    }

    void answerRedirect(int fd) {
        std::string request = readRequest(fd);
        auto firstSpace = request.find(' ');
        auto secondSpace = request.find(' ', firstSpace + 1);
        if (firstSpace != std::string::npos && secondSpace != std::string::npos) {
            std::lock_guard<std::mutex> lock(mu_);
            paths_.push_back(request.substr(firstSpace + 1, secondSpace - firstSpace - 1));
        }
        const std::string response =
            "HTTP/1.1 302 Found\r\nLocation: /landed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        ::send(fd, response.data(), response.size(), 0);
        ::close(fd);
    }

    /** Reads one request, its body included, or gives up after 5 s. */
    static std::string readRequest(int fd) {
        std::string request;
        char buffer[4096];
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        size_t headerEnd = std::string::npos;
        size_t contentLength = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            if (headerEnd != std::string::npos && request.size() >= headerEnd + 4 + contentLength) {
                break;
            }
            pollfd pfd{fd, POLLIN, 0};
            if (::poll(&pfd, 1, 50) <= 0) {
                continue;
            }
            ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
            if (n <= 0) {
                break;
            }
            request.append(buffer, static_cast<size_t>(n));
            if (headerEnd == std::string::npos) {
                headerEnd = request.find("\r\n\r\n");
                if (headerEnd != std::string::npos) {
                    std::string lower;
                    for (char c : request.substr(0, headerEnd)) {
                        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                    }
                    auto at = lower.find("content-length:");
                    if (at != std::string::npos) {
                        contentLength = std::stoul(lower.substr(at + 15));
                    }
                }
            }
        }
        return request;
    }

public:
    static constexpr const char* kPartialBody = "email\npartial@example.com\n";
    static constexpr const char* kPartialJson = R"({"Credits":"5","status":"valid","address":"a@example.com"})";

private:

    Mode mode_;
    int stallMs_;
    int listenFd_ = -1;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    std::mutex mu_;
    std::vector<std::string> paths_;
};

/** Every request method RequestHandler implements, one call each. */
std::vector<std::pair<std::string, std::function<cpr::Response(BaseRequestHandler&, const std::string&)>>>
everyHandlerMethod() {
    const cpr::Header header{{"Accept", "application/json"}};
    return {
        {"Get(url)", [](BaseRequestHandler& h, const std::string& u) { return h.Get(cpr::Url{u}); }},
        {"Get(url, header)", [header](BaseRequestHandler& h, const std::string& u) {
            return h.Get(cpr::Url{u}, header);
        }},
        {"getWithParameters", [](BaseRequestHandler& h, const std::string& u) {
            return h.getWithParameters(cpr::Url{u}, cpr::Parameters{{"file_id", "fid"}});
        }},
        {"Post(body)", [header](BaseRequestHandler& h, const std::string& u) {
            return h.Post(cpr::Url{u}, header, cpr::Body{"a=b"});
        }},
        {"Post(multipart)", [header](BaseRequestHandler& h, const std::string& u) {
            return h.Post(cpr::Url{u}, header, cpr::Multipart{{"a", "b"}});
        }},
        {"Put", [header](BaseRequestHandler& h, const std::string& u) {
            return h.Put(cpr::Url{u}, header, cpr::Body{"{}"});
        }},
        {"Delete(url, header)", [header](BaseRequestHandler& h, const std::string& u) {
            return h.Delete(cpr::Url{u}, header);
        }},
        {"Delete(url, header, body)", [header](BaseRequestHandler& h, const std::string& u) {
            return h.Delete(cpr::Url{u}, header, cpr::Body{"{}"});
        }},
    };
}

}  // namespace

TEST(Transport, RedirectsAreNotFollowed) {
    LocalServer server(LocalServer::Mode::Redirect);
    RequestHandler handler;
    for (const auto& method : everyHandlerMethod()) {
        cpr::Response response = method.second(handler, server.url("http", "/start"));
        EXPECT_EQ(response.status_code, 302) << method.first;
    }
    for (const auto& path : server.paths()) {
        // A followed redirect would come back for /landed, carrying the key on.
        EXPECT_EQ(path.find("/landed"), std::string::npos) << path;
    }
    EXPECT_EQ(server.paths().size(), everyHandlerMethod().size());
}

TEST(Transport, EveryRequestHasATotalTimeout) {
    LocalServer server(LocalServer::Mode::Stall);
    // Plain http: the connection is made, then the server never answers.
    RequestHandler handler(300, 300);
    for (const auto& method : everyHandlerMethod()) {
        auto started = std::chrono::steady_clock::now();
        cpr::Response response = method.second(handler, server.url("http", "/stall"));
        auto elapsed = std::chrono::steady_clock::now() - started;
        EXPECT_EQ(response.status_code, 0) << method.first;
        EXPECT_EQ(response.error.code, cpr::ErrorCode::OPERATION_TIMEDOUT) << method.first;
        EXPECT_LT(elapsed, std::chrono::milliseconds(LocalServer::kStallMs / 2)) << method.first;
    }
}

TEST(Transport, EveryRequestHasAConnectTimeout) {
    LocalServer server(LocalServer::Mode::Stall);
    // https to a server that never answers the TLS handshake: the handshake is
    // part of connecting, so only the connect timeout (300 ms) can end it
    // before the server gives up. The total timeout is longer than that.
    RequestHandler handler(LocalServer::kStallMs * 3, 300);
    for (const auto& method : everyHandlerMethod()) {
        auto started = std::chrono::steady_clock::now();
        cpr::Response response = method.second(handler, server.url("https", "/stall"));
        auto elapsed = std::chrono::steady_clock::now() - started;
        EXPECT_EQ(response.status_code, 0) << method.first;
        EXPECT_EQ(response.error.code, cpr::ErrorCode::OPERATION_TIMEDOUT) << method.first;
        EXPECT_LT(elapsed, std::chrono::milliseconds(LocalServer::kStallMs / 2)) << method.first;
    }
}

TEST(Transport, TheDefaultTimeoutsAreTheDocumentedOnes) {
    EXPECT_EQ(BaseRequestHandler::DefaultTimeoutMs, 120000);
    EXPECT_EQ(BaseRequestHandler::DefaultConnectTimeoutMs, 30000);
}

namespace {

/** The real RequestHandler, with every https URL sent to the local server instead. */
class LocalRedirector : public RequestHandler {
public:
    LocalRedirector(const LocalServer& server, std::int32_t timeoutMs)
        : RequestHandler(timeoutMs, timeoutMs), server_(server) {}

protected:
    cpr::Url local(const cpr::Url& url) const {
        const std::string text = url.str();
        auto host = text.find("://");
        auto path = host == std::string::npos ? std::string::npos : text.find('/', host + 3);
        return cpr::Url{server_.url("http", path == std::string::npos ? "/" : text.substr(path))};
    }
    cpr::Response doGet(const cpr::Url& url) override { return RequestHandler::doGet(local(url)); }
    cpr::Response doGet(const cpr::Url& url, const cpr::Header& header) override {
        return RequestHandler::doGet(local(url), header);
    }
    cpr::Response doGetWithParameters(const cpr::Url& url, const cpr::Parameters& parameters) override {
        return RequestHandler::doGetWithParameters(local(url), parameters);
    }
    cpr::Response doPost(const cpr::Url& url, const cpr::Header& header, const cpr::Body& body) override {
        return RequestHandler::doPost(local(url), header, body);
    }

private:
    const LocalServer& server_;
};

class LocalClient : public ZeroBounce {
public:
    explicit LocalClient(BaseRequestHandler& handler) {
        initialize("cut-short-key");
        requestHandler = &handler;
    }
};

class LocalWarmupClient : public ZeroBounceWarmup {
public:
    explicit LocalWarmupClient(BaseRequestHandler& handler) {
        initialize("cut-short-warmup-key");
        setRequestHandler(&handler);
    }
};

}  // namespace

TEST(Transport, ADownloadCutShortIsAnErrorAndSavesNothing) {
    // A 200 header promising 1000 bytes, then 26 bytes and the end of the
    // connection (Truncate) or 26 bytes and silence past the read timeout
    // (TruncateStall). Both used to be saved and reported as a success.
    for (auto mode : {LocalServer::Mode::Truncate, LocalServer::Mode::TruncateStall}) {
        const char* label = mode == LocalServer::Mode::Truncate ? "closed early" : "stalled";
        LocalServer server(mode);
        LocalRedirector handler(server, 500);
        LocalClient client(handler);
        const auto dir = std::filesystem::temp_directory_path() /
            ("zb-cpp-cut-short-" + std::to_string(::getpid()));
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);

        GetFileOptions options;
        options.downloadType = "all";
        const std::vector<std::pair<std::string, std::function<void(const std::string&, OnSuccessCallback<ZBGetFileResponse>, OnErrorCallback)>>> downloads{
            {"getFile", [&](const std::string& p, auto ok, auto err) { client.getFile("fid", p, ok, err); }},
            {"getFileWithOptions", [&](const std::string& p, auto ok, auto err) {
                client.getFileWithOptions("fid", p, options, ok, err);
            }},
            {"scoringGetFile", [&](const std::string& p, auto ok, auto err) { client.scoringGetFile("fid", p, ok, err); }},
            {"scoringGetFileWithOptions", [&](const std::string& p, auto ok, auto err) {
                client.scoringGetFileWithOptions("fid", p, options, ok, err);
            }},
            {"emailFinderGetFile", [&](const std::string& p, auto ok, auto err) {
                client.emailFinderGetFile("fid", p, ok, err);
            }},
            {"domainSearchGetFile", [&](const std::string& p, auto ok, auto err) {
                client.domainSearchGetFile("fid", p, ok, err);
            }},
        };
        for (const auto& download : downloads) {
            const std::string path = (dir / (download.first + ".csv")).string();
            int errors = 0;
            download.second(
                path,
                [&](ZBGetFileResponse) { ADD_FAILURE() << download.first << " (" << label << "): success"; },
                [&](ZBErrorResponse error) {
                    ++errors;
                    EXPECT_EQ(error.statusCode, 0) << download.first;
                    ASSERT_FALSE(error.errors.empty());
                    EXPECT_NE(error.errors.front().find("cut short"), std::string::npos)
                        << download.first << ": " << error.errors.front();
                    EXPECT_EQ(error.toString().find("cut-short-key"), std::string::npos);
                });
            EXPECT_EQ(errors, 1) << download.first << " (" << label << ")";
            EXPECT_FALSE(std::filesystem::exists(path)) << download.first << " (" << label << ") saved a partial file";
        }
        std::filesystem::remove_all(dir);

        // The Warmup client's CSV and JSON calls go through the same check.
        LocalWarmupClient warmup(handler);
        int warmupErrors = 0;
        auto onWarmupError = [&](ZBErrorResponse error) {
            ++warmupErrors;
            EXPECT_EQ(error.statusCode, 0);
            EXPECT_NE(error.toString().find("cut short"), std::string::npos) << error.toString();
        };
        warmup.downloadSeeds([&](std::string) { ADD_FAILURE() << "downloadSeeds (" << label << "): success"; },
                             onWarmupError);
        warmup.getSeeds([&](std::string) { ADD_FAILURE() << "getSeeds (" << label << "): success"; }, onWarmupError);
        EXPECT_EQ(warmupErrors, 2) << label;
    }
}

TEST(Transport, AJsonResponseCutShortIsAnError) {
    // A 200 header promising 1000 bytes, then a JSON object that parses on
    // its own and the end of the connection. libcurl reports the transfer as
    // partial; the JSON calls used to ignore that and return the object.
    LocalServer server(LocalServer::Mode::TruncateJson);
    LocalRedirector handler(server, 2000);
    LocalClient client(handler);
    ZBValidateOptions post;
    post.usePost = true;
    int errors = 0;
    auto onError = [&](ZBErrorResponse error) {
        ++errors;
        EXPECT_EQ(error.statusCode, 0);
        EXPECT_NE(error.toString().find("cut short"), std::string::npos) << error.toString();
        EXPECT_EQ(error.toString().find("cut-short-key"), std::string::npos);
    };
    client.getCredits([](ZBCreditsResponse) { ADD_FAILURE() << "getCredits: success"; }, onError);
    client.validate("a@example.com", "", post,
        [](ZBValidateResponse) { ADD_FAILURE() << "validate (POST): success"; }, onError);
    client.listFilters(false, [](std::string) { ADD_FAILURE() << "listFilters: success"; }, onError);
    EXPECT_EQ(errors, 3);
}

namespace {

/**
 * A handler that keeps BaseRequestHandler's own PUT and DELETE, as a
 * subclass written before those were added does. The pure methods are never
 * called here.
 */
class BaseDefaultsHandler : public BaseRequestHandler {
protected:
    cpr::Response doGet(const cpr::Url&) override { return {}; }
    cpr::Response doGet(const cpr::Url&, const cpr::Header&) override { return {}; }
    cpr::Response doGetWithParameters(const cpr::Url&, const cpr::Parameters&) override { return {}; }
    cpr::Response doPost(const cpr::Url&, const cpr::Header&, const cpr::Body&) override { return {}; }
    cpr::Response doPost(const cpr::Url&, const cpr::Header&, const cpr::Multipart&) override { return {}; }
};

/** The request methods BaseRequestHandler implements itself. */
std::vector<std::pair<std::string, std::function<cpr::Response(BaseRequestHandler&, const std::string&)>>>
everyBaseDefaultMethod() {
    std::vector<std::pair<std::string, std::function<cpr::Response(BaseRequestHandler&, const std::string&)>>> out;
    for (auto& method : everyHandlerMethod()) {
        if (method.first.rfind("Put", 0) == 0 || method.first.rfind("Delete", 0) == 0) {
            out.push_back(method);
        }
    }
    return out;
}

}  // namespace

TEST(Transport, TheBaseHandlersPutAndDeleteDoNotFollowRedirects) {
    LocalServer server(LocalServer::Mode::Redirect);
    BaseDefaultsHandler handler;
    const auto methods = everyBaseDefaultMethod();
    ASSERT_EQ(methods.size(), 3u);
    for (const auto& method : methods) {
        cpr::Response response = method.second(handler, server.url("http", "/start"));
        EXPECT_EQ(response.status_code, 302) << method.first;
    }
    for (const auto& path : server.paths()) {
        EXPECT_EQ(path.find("/landed"), std::string::npos) << path;
    }
    EXPECT_EQ(server.paths().size(), methods.size());
}

TEST(Transport, TheBaseHandlersPutAndDeleteHaveTheDefaultConnectTimeout) {
    // A TLS handshake the server never answers, held for longer than the
    // default connect timeout (30 s) and shorter than the total one (120 s):
    // only the connect timeout ends it in time. The three calls run at once,
    // so the test takes about 30 s.
    constexpr int kHoldMs = BaseRequestHandler::DefaultConnectTimeoutMs + 15000;
    LocalServer server(LocalServer::Mode::Stall, kHoldMs);
    BaseDefaultsHandler handler;
    const auto methods = everyBaseDefaultMethod();
    ASSERT_EQ(methods.size(), 3u);
    std::vector<cpr::Response> responses(methods.size());
    std::vector<std::chrono::steady_clock::duration> elapsed(methods.size());
    std::vector<std::thread> threads;
    for (size_t i = 0; i < methods.size(); ++i) {
        threads.emplace_back([&, i] {
            auto started = std::chrono::steady_clock::now();
            responses[i] = methods[i].second(handler, server.url("https", "/stall"));
            elapsed[i] = std::chrono::steady_clock::now() - started;
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    for (size_t i = 0; i < methods.size(); ++i) {
        EXPECT_EQ(responses[i].status_code, 0) << methods[i].first;
        EXPECT_EQ(responses[i].error.code, cpr::ErrorCode::OPERATION_TIMEDOUT) << methods[i].first;
        EXPECT_LT(elapsed[i], std::chrono::milliseconds(kHoldMs - 5000)) << methods[i].first;
    }
}

#endif  // _WIN32
