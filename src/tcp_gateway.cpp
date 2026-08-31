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

TcpGateway::TcpGateway(
    MatchingEngine& engine,
    SPSCQueue& queue,
    const GatewayConfig& config,
    ReadModel* read_model
)
    : engine(engine), queue(queue), config(config), read_model(read_model)
{
    std::signal(SIGPIPE, SIG_IGN);
}

TcpGateway::~TcpGateway() {
    stop();
}

bool TcpGateway::start() {
    bool expected = false;
    if (!is_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return true;
    }

    std::string config_err;
    if (!config.isValid(&config_err)) {
        std::cerr << "[TcpGateway] Invalid configuration: " << config_err << "\n";
        is_running.store(false, std::memory_order_release);
        return false;
    }

    // 1. Create TCP listening socket
    int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        std::cerr << "[TcpGateway] socket() failed: " << errno << "\n";
        is_running.store(false, std::memory_order_release);
        return false;
    }

    // 2. Set SO_REUSEADDR and SO_REUSEPORT
    int opt = 1;
    ::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    ::setsockopt(lfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    // 3. Set non-blocking mode
    int flags = ::fcntl(lfd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(lfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "[TcpGateway] fcntl(O_NONBLOCK) failed\n";
        ::close(lfd);
        is_running.store(false, std::memory_order_release);
        return false;
    }

    // 4. Bind address
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(config.port));
    if (::inet_pton(AF_INET, config.host.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "[TcpGateway] inet_pton failed for host: " << config.host << "\n";
        ::close(lfd);
        is_running.store(false, std::memory_order_release);
        return false;
    }

    if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[TcpGateway] bind() failed on port " << config.port << ": " << errno << "\n";
        ::close(lfd);
        is_running.store(false, std::memory_order_release);
        return false;
    }

    // 5. Retrieve dynamic port if ephemeral (port 0)
    sockaddr_in bound_addr{};
    socklen_t bound_len = sizeof(bound_addr);
    if (::getsockname(lfd, reinterpret_cast<sockaddr*>(&bound_addr), &bound_len) == 0) {
        bound_port = ntohs(bound_addr.sin_port);
    } else {
        bound_port = config.port;
    }

    // 6. Listen backlog
    if (::listen(lfd, 1024) < 0) {
        std::cerr << "[TcpGateway] listen() failed: " << errno << "\n";
        ::close(lfd);
        is_running.store(false, std::memory_order_release);
        return false;
    }

    // 7. Create kqueue
    int kfd = ::kqueue();
    if (kfd < 0) {
        std::cerr << "[TcpGateway] kqueue() failed: " << errno << "\n";
        ::close(lfd);
        is_running.store(false, std::memory_order_release);
        return false;
    }

    // 8. Register lfd with kqueue for read events
    struct kevent ev{};
    EV_SET(&ev, lfd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (::kevent(kfd, &ev, 1, nullptr, 0, nullptr) < 0) {
        std::cerr << "[TcpGateway] kevent(listen_fd) failed: " << errno << "\n";
        ::close(kfd);
        ::close(lfd);
        is_running.store(false, std::memory_order_release);
        return false;
    }

    listen_fd.store(lfd, std::memory_order_release);
    kq_fd.store(kfd, std::memory_order_release);

    // 9. Launch threads: Gateway Event Loop + Engine Consumer
    gateway_thread = std::thread(&TcpGateway::runGateway, this);
    consumer_thread = std::thread(&TcpGateway::runConsumer, this);

    return true;
}

void TcpGateway::stop() {
    bool expected = true;
    if (!is_running.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;
    }

    // Close listen socket and kqueue to unblock kevent() atomically
    int lfd = listen_fd.exchange(-1, std::memory_order_acq_rel);
    if (lfd >= 0) {
        close(lfd);
    }

    int kfd = kq_fd.exchange(-1, std::memory_order_acq_rel);
    if (kfd >= 0) {
        close(kfd);
    }

    // Join threads
    if (gateway_thread.joinable()) {
        gateway_thread.join();
    }
    if (consumer_thread.joinable()) {
        consumer_thread.join();
    }

    // Close all open client connections
    for (auto& pair : clients) {
        if (pair.second.fd >= 0) {
            close(pair.second.fd);
        }
    }
    clients.clear();
    active_clients.store(0, std::memory_order_release);
}

void TcpGateway::closeClient(int fd) {
    auto it = clients.find(fd);
    if (it != clients.end()) {
        close(fd);
        clients.erase(it);
        if (active_clients > 0) {
            active_clients--;
        }
        stats.connections_closed++;
    }
}

static bool send_all_socket(int fd, const uint8_t* data, size_t size) {
    size_t total_sent = 0;
    while (total_sent < size) {
        ssize_t n = send(fd, data + total_sent, size - total_sent, 0);
        if (n > 0) {
            total_sent += static_cast<size_t>(n);
        } else if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::yield();
                continue;
            }
            return false;
        } else {
            return false;
        }
    }
    return true;
}

void TcpGateway::handleSession(int fd, const SessionFrame& session) {
    if (session.type == wire::MessageType::Ping) {
        auto resp = wire::encode_pong(session.nonce);
        send_all_socket(fd, resp.data(), resp.size());
    }
    stats.session_frames_processed++;
}

void TcpGateway::handleQuery(int fd, const QueryFrame& query) {
    if (!read_model) return;

    switch (query.type) {
        case wire::MessageType::QueryBook: {
            L2BookState book;
            read_model->getL2Book(query.instrument_id, book);
            auto resp = wire::encode_query_book_response(book);
            send_all_socket(fd, resp.data(), resp.size());
            break;
        }
        case wire::MessageType::QueryTrades: {
            std::vector<TradeRecord> trades;
            read_model->getRecentTrades(query.instrument_id, query.limit, trades);
            auto resp = wire::encode_query_trades_response(query.instrument_id, trades);
            send_all_socket(fd, resp.data(), resp.size());
            break;
        }
        case wire::MessageType::QueryOrder: {
            OrderRecord order;
            bool found = false;
            if (query.query_by_client_id) {
                found = read_model->getOrderByClientId(query.order_id, order);
            } else {
                found = read_model->getOrder(query.order_id, order);
            }
            auto resp = wire::encode_query_order_response(found, order);
            send_all_socket(fd, resp.data(), resp.size());
            break;
        }
        case wire::MessageType::QueryStats: {
            EngineMetrics metrics;
            read_model->getMetrics(metrics);
            auto resp = wire::encode_query_stats_response(metrics);
            send_all_socket(fd, resp.data(), resp.size());
            break;
        }
        default:
            break;
    }
}

void TcpGateway::runGateway() {
    constexpr int MAX_EVENTS = 64;
    struct kevent event_list[MAX_EVENTS];
    uint8_t recv_buf[4096];

    // Timeout 100ms for responsiveness to shutdown
    struct timespec timeout{};
    timeout.tv_sec = 0;
    timeout.tv_nsec = 100000000; // 100 ms

    while (is_running.load(std::memory_order_relaxed)) {
        int cur_kq = kq_fd.load(std::memory_order_acquire);
        if (cur_kq < 0) break;

        int n_events = kevent(cur_kq, nullptr, 0, event_list, MAX_EVENTS, &timeout);

        if (n_events < 0) {
            if (errno == EINTR) continue;
            break; // Socket closed or error during shutdown
        }

        int cur_listen = listen_fd.load(std::memory_order_acquire);

        for (int i = 0; i < n_events; ++i) {
            int fd = static_cast<int>(event_list[i].ident);

            if (cur_listen >= 0 && fd == cur_listen) {
                // Accept new client connections in a non-blocking loop
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(cur_listen, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break; // No more pending connections
                        }
                        break;
                    }

                    // Enforce max active connections limit
                    if (clients.size() >= config.max_connections) {
                        if (config.enable_logging) {
                            std::cerr << "[TcpGateway] Max connections limit (" << config.max_connections << ") reached, rejecting client fd=" << client_fd << "\n";
                        }
                        close(client_fd);
                        stats.connections_rejected++;
                        continue;
                    }

                    // Configure non-blocking client socket
                    int cflags = fcntl(client_fd, F_GETFL, 0);
                    if (cflags >= 0) {
                        fcntl(client_fd, F_SETFL, cflags | O_NONBLOCK);
                    }

                    // Prevent SIGPIPE on write to closed socket on macOS
#ifdef SO_NOSIGPIPE
                    int set_nosigpipe = 1;
                    setsockopt(client_fd, SOL_SOCKET, SO_NOSIGPIPE, &set_nosigpipe, sizeof(set_nosigpipe));
#endif

                    // Register client with kqueue for read events
                    struct kevent client_ev{};
                    EV_SET(&client_ev, client_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
                    int reg_kq = kq_fd.load(std::memory_order_acquire);
                    if (reg_kq >= 0 && kevent(reg_kq, &client_ev, 1, nullptr, 0, nullptr) == 0) {
                        ClientConnection conn;
                        conn.fd = client_fd;
                        clients[client_fd] = std::move(conn);
                        active_clients++;
                        stats.connections_accepted++;
                    } else {
                        close(client_fd);
                    }
                }
            } else {
                // Data available on client socket
                auto it = clients.find(fd);
                if (it == clients.end()) continue;

                ClientConnection& client = it->second;
                bool should_close = false;

                while (true) {
                    ssize_t n = recv(fd, recv_buf, sizeof(recv_buf), 0);

                    if (n > 0) {
                        // Check buffer overflow protection
                        if (client.parser.remainingBytes() + static_cast<size_t>(n) > config.max_client_buffer) {
                            stats.buffer_overflows++;
                            if (config.enable_logging) {
                                std::cerr << "[TcpGateway] Client fd=" << fd << " exceeded max buffer size (" << config.max_client_buffer << " bytes)\n";
                            }
                            should_close = true;
                            break;
                        }

                        client.parser.append(recv_buf, static_cast<size_t>(n));

                        // Parse all complete frames in buffer
                        while (true) {
                            ParsedFrame frame{};
                            ParseError err = ParseError::None;
                            ParseStatus status = client.parser.parseNextFrame(frame, err);

                            if (status == ParseStatus::Ok) {
                                if (frame.category == FrameCategory::Session) {
                                    handleSession(fd, frame.session);
                                } else if (frame.category == FrameCategory::Command) {
                                    frame.command.client_fd = fd;
                                    // Push to SPSC Queue with bounded backpressure retries
                                    bool pushed = false;
                                    for (int retry = 0; retry < config.max_backpressure_retries; ++retry) {
                                        if (queue.push(frame.command)) {
                                            pushed = true;
                                            break;
                                        }
                                        std::this_thread::yield();
                                    }

                                    if (!pushed) {
                                        stats.queue_full_drops++;
                                        if (config.enable_logging) {
                                            std::cerr << "[TcpGateway] SPSC command queue full, dropping command from client fd=" << fd << "\n";
                                        }
                                        should_close = true;
                                        break;
                                    } else {
                                        stats.events_pushed++;
                                    }
                                } else if (frame.category == FrameCategory::Query) {
                                    handleQuery(fd, frame.query);
                                    stats.queries_processed++;
                                }
                            } else if (status == ParseStatus::NeedMoreData) {
                                break; // Waiting for more network bytes
                            } else {
                                stats.malformed_frames++;
                                if (config.enable_logging) {
                                    std::cerr << "[TcpGateway] Client fd=" << fd << " sent malformed frame (code=" << static_cast<int>(err) << ")\n";
                                }
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
                    engine.addLimitOrder(event.instrument, event.side, event.price, event.qty, event.tif, event.client_order_id);
                    break;
                case EventType::MarketOrder:
                    engine.addMarketOrder(event.instrument, event.side, event.qty, event.client_order_id);
                    break;
                case EventType::CancelOrder:
                    engine.cancelOrder(event.instrument, event.id, event.client_order_id);
                    break;
                case EventType::ModifyOrder:
                    engine.modifyOrder(event.instrument, event.id, event.price, event.qty, event.client_order_id);
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
                engine.addLimitOrder(event.instrument, event.side, event.price, event.qty, event.tif, event.client_order_id);
                break;
            case EventType::MarketOrder:
                engine.addMarketOrder(event.instrument, event.side, event.qty, event.client_order_id);
                break;
            case EventType::CancelOrder:
                engine.cancelOrder(event.instrument, event.id, event.client_order_id);
                break;
            case EventType::ModifyOrder:
                engine.modifyOrder(event.instrument, event.id, event.price, event.qty, event.client_order_id);
                break;
        }
        stats.events_processed++;
    }
}
