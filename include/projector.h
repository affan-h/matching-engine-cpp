#pragma once

#include <atomic>
#include <thread>
#include <cstdint>
#include "spsc_queue.h"
#include "read_model.h"

struct ProjectorStats {
    std::atomic<uint64_t> events_projected{0};
    std::atomic<uint64_t> trades_projected{0};
    std::atomic<uint64_t> l2_updates_projected{0};
    std::atomic<uint64_t> order_states_projected{0};
};

class Projector {
private:
    OutboundEventQueue& outbound_queue;
    ReadModel&          read_model;

    std::atomic<bool>   is_running{false};
    std::thread         worker_thread;
    ProjectorStats      stats;

    void run();

public:
    Projector(OutboundEventQueue& queue, ReadModel& model);
    ~Projector();

    bool start();
    void stop();

    bool isRunning() const { return is_running.load(std::memory_order_relaxed); }
    const ProjectorStats& getStats() const { return stats; }
};
