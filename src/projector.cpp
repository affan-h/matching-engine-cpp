#include "projector.h"
#include <chrono>

Projector::Projector(OutboundEventQueue& queue, ReadModel& model)
    : outbound_queue(queue), read_model(model) {}

Projector::~Projector() {
    stop();
}

bool Projector::start() {
    if (is_running.load(std::memory_order_relaxed)) return true;
    is_running.store(true, std::memory_order_release);
    worker_thread = std::thread(&Projector::run, this);
    return true;
}

void Projector::stop() {
    if (!is_running.load(std::memory_order_relaxed)) return;
    is_running.store(false, std::memory_order_release);

    if (worker_thread.joinable()) {
        worker_thread.join();
    }
}

void Projector::run() {
    events::OutboundEvent event;

    while (is_running.load(std::memory_order_relaxed)) {
        if (outbound_queue.pop(event)) {
            read_model.applyEvent(event);
            stats.events_projected++;
            switch (event.type) {
                case events::OutboundEventType::Trade:
                    stats.trades_projected++;
                    break;
                case events::OutboundEventType::L2Update:
                    stats.l2_updates_projected++;
                    break;
                case events::OutboundEventType::OrderState:
                    stats.order_states_projected++;
                    break;
            }
        } else {
            std::this_thread::yield();
        }
    }

    // Drain remaining events during shutdown to prevent data loss
    while (outbound_queue.pop(event)) {
        read_model.applyEvent(event);
        stats.events_projected++;
        switch (event.type) {
            case events::OutboundEventType::Trade:
                stats.trades_projected++;
                break;
            case events::OutboundEventType::L2Update:
                stats.l2_updates_projected++;
                break;
            case events::OutboundEventType::OrderState:
                stats.order_states_projected++;
                break;
        }
    }
}
