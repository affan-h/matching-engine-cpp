#include "tcp_gateway.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <csignal>
#include <iostream>
#include <vector>

TcpGateway::TcpGateway(MatchingEngine& engine, SPSCQueue& queue, const GatewayConfig& config)
    : engine(engine), queue(queue), config(config)
{
    std::signal(SIGPIPE, SIG_IGN);
}

TcpGateway::~TcpGateway() {
    stop();
}

bool TcpGateway::start() {
    if (is_running.load()) {
        return true;
    }

    // 1. Create TCP listening socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "[TcpGateway] socket() failed: " << errno << "\n";
        return false;
    }

    // 2. Set SO_REUSEADDR
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "[TcpGateway] setsockopt(SO_REUSEADDR) failed\n";
        close(listen_fd);
        listen_fd = -1;
        return false;
    }

    // 3. Set non-blocking mode
    int flags = fcntl(listen_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "[TcpGateway] fcntl(O_NONBLOCK) failed\n";
        close(listen_fd);
        listen_fd = -1;
        return false;
    }

    // 4. Bind address
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(config.port));
    if (inet_pton(AF_INET, config.host.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "[TcpGateway] inet_pton failed for host: " << config.host << "\n";
        close(listen_fd);
        listen_fd = -1;
        return false;
    }

    if (bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[TcpGateway] bind() failed on port " << config.port << ": " << errno << "\n";
        close(listen_fd);
        listen_fd = -1;
        return false;
    }

    // 5. Listen
    if (listen(listen_fd, SOMAXCONN) < 0) {
        std::cerr << "[TcpGateway] listen() failed\n";
        close(listen_fd);
        listen_fd = -1;
        return false;
    }

    // 6. Retrieve bound port (crucial if port was 0 / ephemeral)
    sockaddr_in bound_addr{};
    socklen_t bound_len = sizeof(bound_addr);
    if (getsockname(listen_fd, reinterpret_cast<struct sockaddr*>(&bound_addr), &bound_len) == 0) {
        bound_port = ntohs(bound_addr.sin_port);
    } else {
        bound_port = config.port;
    }

    // 7. Create kqueue descriptor
    kq_fd = kqueue();
    if (kq_fd < 0) {
        std::cerr << "[TcpGateway] kqueue() failed: " << errno << "\n";
        close(listen_fd);
        listen_fd = -1;
        return false;
    }

    // 8. Register listen_fd read event in kqueue
    struct kevent change;
    EV_SET(&change, listen_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (kevent(kq_fd, &change, 1, nullptr, 0, nullptr) < 0) {
        std::cerr << "[TcpGateway] kevent(listen_fd) registration failed\n";
        close(kq_fd);
        close(listen_fd);
        kq_fd = -1;
        listen_fd = -1;
        return false;
    }

    is_running.store(true);

    // 9. Start worker threads
    consumer_thread = std::thread(&TcpGateway::runConsumer, this);
    gateway_thread  = std::thread(&TcpGateway::runGateway, this);

    return true;
}

void TcpGateway::stop() {
    if (!is_running.exchange(false)) {
        return;
    }

    // Join gateway event loop
    if (gateway_thread.joinable()) {
        gateway_thread.join();
    }

    // Join matching engine consumer thread
    if (consumer_thread.joinable()) {
        consumer_thread.join();
    }

    // Close all open client connections
    for (auto& [fd, client] : clients) {
        close(fd);
    }
    clients.clear();

    if (listen_fd >= 0) {
        close(listen_fd);
        listen_fd = -1;
    }

    if (kq_fd >= 0) {
        close(kq_fd);
        kq_fd = -1;
    }
}

void TcpGateway::closeClient(int fd) {
    auto it = clients.find(fd);
    if (it != clients.end()) {
        it->second.parser.reset();
        clients.erase(it);
        stats.connections_closed++;
    }

    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    kevent(kq_fd, &ev, 1, nullptr, 0, nullptr);

    close(fd);
}

void TcpGateway::runGateway() {
    struct kevent event_list[64];

    while (is_running.load(std::memory_order_relaxed)) {
        // Poll with 20ms timeout to periodically check is_running
        struct timespec timeout;
        timeout.tv_sec  = 0;
        timeout.tv_nsec = 20'000'000; // 20 ms

        int nev = kevent(kq_fd, nullptr, 0, event_list, 64, &timeout);
        if (nev < 0) {
            if (errno == EINTR) continue;
            if (!is_running.load(std::memory_order_relaxed)) break;
            std::cerr << "[TcpGateway] kevent() error: " << errno << "\n";
            break;
        }

        for (int i = 0; i < nev; ++i) {
            int fd = static_cast<int>(event_list[i].ident);

            if (fd == listen_fd) {
                // Accept all pending incoming connections
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(listen_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break; // No more incoming connections
                        }
                        if (errno == EINTR) continue;
                        break;
                    }

                    // Set client socket non-blocking
                    int cflags = fcntl(client_fd, F_GETFL, 0);
                    fcntl(client_fd, F_SETFL, cflags | O_NONBLOCK);

                    // Ignore SIGPIPE on socket writes (macOS)
                    int sopt = 1;
                    setsockopt(client_fd, SOL_SOCKET, SO_NOSIGPIPE, &sopt, sizeof(sopt));

                    // Register with kqueue
                    struct kevent client_ev;
                    EV_SET(&client_ev, client_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
                    kevent(kq_fd, &client_ev, 1, nullptr, 0, nullptr);

                    clients[client_fd] = ClientConnection{client_fd, TcpParser{}};
                    stats.connections_accepted++;
                }
            } else {
                // Client socket event
                auto it = clients.find(fd);
                if (it == clients.end()) continue;
                ClientConnection& client = it->second;

                bool should_close = false;
                uint8_t recv_buf[4096];

                while (true) {
                    ssize_t n = recv(fd, recv_buf, sizeof(recv_buf), 0);
                    if (n > 0) {
                        // Per-client buffer overflow protection
                        if (client.parser.remainingBytes() + static_cast<size_t>(n) > config.max_client_buffer) {
                            stats.buffer_overflows++;
                            should_close = true;
                            break;
                        }

                        client.parser.append(recv_buf, static_cast<size_t>(n));

                        // Parse all complete frames in buffer
                        while (true) {
                            OrderEvent event{};
                            ParseError err = ParseError::None;
                            ParseStatus status = client.parser.parseNext(event, err);

                            if (status == ParseStatus::Ok) {
                                // Push to SPSC Queue with bounded backpressure retries
                                bool pushed = false;
                                for (int retry = 0; retry < config.max_backpressure_retries; ++retry) {
                                    if (queue.push(event)) {
                                        pushed = true;
                                        break;
                                    }
                                    std::this_thread::yield();
                                }

                                if (!pushed) {
                                    // Severe backpressure: drop and disconnect client
                                    stats.queue_full_drops++;
                                    should_close = true;
                                    break;
                                } else {
                                    stats.events_pushed++;
                                }
                            } else if (status == ParseStatus::NeedMoreData) {
                                break; // Waiting for more network bytes
                            } else {
                                // Fatal framing/validation error -> disconnect client
                                stats.malformed_frames++;
                                should_close = true;
                                break;
                            }
                        }

                        if (should_close) break;
                    } else if (n == 0) {
                        // Clean EOF / client disconnect
                        should_close = true;
                        break;
                    } else {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break; // All socket data drained
                        }
                        if (errno == EINTR) {
                            continue;
                        }
                        // Fatal socket error
                        should_close = true;
                        break;
                    }
                }

                if (should_close || (event_list[i].flags & EV_EOF)) {
                    closeClient(fd);
                }
            }
        }
    }
}

void TcpGateway::runConsumer() {
    OrderEvent event;

    while (is_running.load(std::memory_order_relaxed)) {
        if (queue.pop(event)) {
            switch (event.type) {
                case EventType::LimitOrder:
                    engine.addLimitOrder(event.instrument, event.side, event.price, event.qty, event.tif);
                    break;
                case EventType::MarketOrder:
                    engine.addMarketOrder(event.instrument, event.side, event.qty);
                    break;
                case EventType::CancelOrder:
                    engine.cancelOrder(event.instrument, event.id);
                    break;
                case EventType::ModifyOrder:
                    engine.modifyOrder(event.instrument, event.id, event.price, event.qty);
                    break;
            }
            stats.events_processed++;
        } else {
            std::this_thread::yield();
        }
    }

    // Drain remaining queue items during shutdown
    while (queue.pop(event)) {
        switch (event.type) {
            case EventType::LimitOrder:
                engine.addLimitOrder(event.instrument, event.side, event.price, event.qty, event.tif);
                break;
            case EventType::MarketOrder:
                engine.addMarketOrder(event.instrument, event.side, event.qty);
                break;
            case EventType::CancelOrder:
                engine.cancelOrder(event.instrument, event.id);
                break;
            case EventType::ModifyOrder:
                engine.modifyOrder(event.instrument, event.id, event.price, event.qty);
                break;
        }
        stats.events_processed++;
    }
}
