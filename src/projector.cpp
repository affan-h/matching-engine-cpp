#include "projector.h"
#include <chrono>

Projector::Projector(OutboundEventQueue& queue, ReadModel& model)
    : outbound_queue(queue), read_model(model) {}

Projector::~Projector() {
    stop();
}

bool Projector::start() {
    bool expected = false;
    if (!is_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return true;
    }
    worker_thread = std::thread(&Projector::run, this);
    return true;
}

void Projector::stop() {
    bool expected = true;
    if (!is_running.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;
    }

    if (worker_thread.joinable()) {
        worker_thread.join();
    }
}

void Projector::run() {
    try {
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
            stats.drained_on_shutdown++;
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
    } catch (const std::exception& e) {
        worker_fault.store(true, std::memory_order_release);
    } catch (...) {
        worker_fault.store(true, std::memory_order_release);
    }
}
