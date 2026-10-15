#include "pch.hpp"
#include "https.hpp"
#include "http.hpp"
#include "server_data.hpp"
#include <openssl/err.h>
#include <atomic>
#include <chrono>
#include <memory>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;
#endif

namespace https
{
    namespace
    {
        using clock = std::chrono::steady_clock;
        std::atomic<bool> stopping{false};

        struct socket_owner
        {
            SOCKET fd;
            ~socket_owner()
            {
                if (fd == INVALID_SOCKET) return;
#ifdef _WIN32
                closesocket(fd);
#else
                close(fd);
#endif
            }
        };

        bool nonblocking(SOCKET fd)
        {
#ifdef _WIN32
            u_long enabled = 1;
            return ioctlsocket(fd, FIONBIO, &enabled) == 0;
#else
            const int flags = fcntl(fd, F_GETFL, 0);
            return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
        }

        int poll_socket(SOCKET fd, bool write)
        {
            fd_set ready;
            FD_ZERO(&ready);
            FD_SET(fd, &ready);
            timeval timeout{0, 250000};
#ifdef _WIN32
            const int count = 0;
#else
            const int count = fd + 1;
#endif
            if (write) return select(count, nullptr, &ready, nullptr, &timeout);
            return select(count, &ready, nullptr, nullptr, &timeout);
        }

        bool wait_socket(SOCKET fd, bool write, clock::time_point deadline)
        {
#ifndef _WIN32
            if (fd < 0 || fd >= FD_SETSIZE) return false;
#endif
            while (!stopping && clock::now() < deadline)
            {
                const int result = poll_socket(fd, write);
                if (result > 0) return true;
                if (result < 0) return false;
            }
            return false;
        }

        bool retry(SSL *ssl, int result, clock::time_point deadline)
        {
            switch (SSL_get_error(ssl, result))
            {
                case SSL_ERROR_WANT_READ: return wait_socket(SSL_get_fd(ssl), false, deadline);
                case SSL_ERROR_WANT_WRITE: return wait_socket(SSL_get_fd(ssl), true, deadline);
                default: return false;
            }
        }

        bool handshake(SSL *ssl, clock::time_point deadline)
        {
            while (!stopping)
            {
                const int result = SSL_accept(ssl);
                if (result == 1) return true;
                if (!retry(ssl, result, deadline)) return false;
            }
            return false;
        }

        bool read_request(SSL *ssl, std::string &request, clock::time_point deadline)
        {
            char buffer[2048];
            while (!stopping && request.size() <= 16384)
            {
                const int received = SSL_read(ssl, buffer, sizeof(buffer));
                if (received <= 0)
                {
                    if (!retry(ssl, received, deadline)) return false;
                    continue;
                }
                request.append(buffer, received);
                const auto state = check_request(request);
                if (state != request_state::incomplete) return state == request_state::complete;
            }
            return false;
        }

        bool write_response(SSL *ssl, const std::string &response, clock::time_point deadline)
        {
            std::size_t sent{};
            while (!stopping && sent < response.size())
            {
                const int result = SSL_write(ssl, response.data() + sent, static_cast<int>(response.size() - sent));
                if (result > 0) sent += result;
                else if (!retry(ssl, result, deadline)) return false;
            }
            return sent == response.size();
        }

        std::string server_response()
        {
            const std::string body = std::format(
                "server|{}\nport|{}\ntype|{}\ntype2|{}\n#maint|{}\nloginurl|{}\nmeta|{}\nRTENDMARKERBS1001",
                gServer_data.server, gServer_data.port, gServer_data.type, gServer_data.type2,
                gServer_data.maint, gServer_data.loginurl, gServer_data.meta);
            return std::format("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
                               body.size(), body);
        }

        void serve(SSL_CTX *ctx, SOCKET fd, const std::string &response)
        {
            const socket_owner socket{fd};
            if (!nonblocking(fd)) return;
            const std::unique_ptr<SSL, decltype(&SSL_free)> ssl{SSL_new(ctx), SSL_free};
            if (!ssl || SSL_set_fd(ssl.get(), static_cast<int>(fd)) != 1) return;
            const auto deadline = clock::now() + std::chrono::seconds(10);
            if (!handshake(ssl.get(), deadline)) return;
            std::string request;
            if (!read_request(ssl.get(), request, deadline)) return;
            const bool known = request.starts_with("POST /growtopia/server_data.php HTTP/1.") ||
                               request.starts_with("GET /growtopia/server_data.php HTTP/1.");
            const std::string missing = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            write_response(ssl.get(), known ? response : missing, deadline);
            SSL_shutdown(ssl.get());
        }
    }

    void stop()
    {
        stopping = true;
    }

    namespace
    {
        using ssl_context = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;

        ssl_context create_context()
        {
            ssl_context ctx{SSL_CTX_new(TLS_server_method()), SSL_CTX_free};
            if (!ctx) return ctx;
            if (SSL_CTX_use_certificate_file(ctx.get(), "resources/ctx/server.crt", SSL_FILETYPE_PEM) != 1 ||
                SSL_CTX_use_PrivateKey_file(ctx.get(), "resources/ctx/server.key", SSL_FILETYPE_PEM) != 1)
            {
                ERR_print_errors_fp(stderr);
                return {nullptr, SSL_CTX_free};
            }
            return ctx;
        }

        bool listen_socket(SOCKET fd)
        {
            const int enabled = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons(443);
            if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
            {
                std::fprintf(stderr, "Unable to bind HTTPS listener on port 443\n");
                return false;
            }
            if (listen(fd, SOMAXCONN) != 0 || !nonblocking(fd)) return false;
            return true;
        }

        void accept_connections(std::stop_token token, SSL_CTX *ctx, SOCKET listener, const std::string &response)
        {
            while (!token.stop_requested() && !stopping)
            {
                if (!wait_socket(listener, false, clock::now() + std::chrono::seconds(1))) continue;
                const SOCKET fd = accept(listener, nullptr, nullptr);
                if (fd == INVALID_SOCKET) continue;
                try { serve(ctx, fd, response); }
                catch (const std::exception &) { std::fputs("HTTPS request failed.\n", stderr); }
            }
        }
    }

    void listener()
    {
#ifdef SIGPIPE
        std::signal(SIGPIPE, SIG_IGN);
#endif
        const auto ctx = create_context();
        if (!ctx) return;
        const socket_owner socket{::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
        if (socket.fd == INVALID_SOCKET) return;
        if (!listen_socket(socket.fd)) return;
        const std::string response = server_response();
        std::array<std::jthread, 4> workers;
        for (auto &worker : workers)
            worker = std::jthread([&](std::stop_token token) { accept_connections(token, ctx.get(), socket.fd, response); });
        for (auto &worker : workers) worker.join();
    }
}
