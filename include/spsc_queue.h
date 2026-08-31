#pragma once

#include <atomic>
#include <vector>
#include <cstddef>
#include "network_protocol.h"
#include "outbound_events.h"

template <typename T>
class LockFreeSPSCQueue {
private:
    std::vector<T> buffer;
    const size_t capacity;
    
    // Prevent false sharing by aligning atomic variables to cache line boundaries (64 bytes)
    alignas(64) std::atomic<size_t> head{0}; 
    alignas(64) std::atomic<size_t> tail{0};

public:
    explicit LockFreeSPSCQueue(size_t size) : buffer(size < 2 ? 2 : size), capacity(size < 2 ? 2 : size) {}

    // Called ONLY by the single Producer thread
    bool push(const T& item) {
        size_t current_tail = tail.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) % capacity;

        // If the next tail catches up to the head, the queue is full
        if (next_tail == head.load(std::memory_order_acquire)) {
            return false; 
        }

        buffer[current_tail] = item;
        tail.store(next_tail, std::memory_order_release); // Publish the write
        return true;
    }

    // Called ONLY by the single Consumer thread
    bool pop(T& out_item) {
        size_t current_head = head.load(std::memory_order_relaxed);

        // If head equals tail, the queue is empty
        if (current_head == tail.load(std::memory_order_acquire)) {
            return false; 
        }

        out_item = buffer[current_head];
        head.store((current_head + 1) % capacity, std::memory_order_release);
        return true;
    }

    size_t size() const {
        size_t h = head.load(std::memory_order_relaxed);
        size_t t = tail.load(std::memory_order_relaxed);
        if (t >= h) return t - h;
        return capacity - (h - t);
    }

    bool empty() const {
        return head.load(std::memory_order_relaxed) == tail.load(std::memory_order_relaxed);
    }

    size_t getCapacity() const { return capacity; }
};

// Aliases for Command (Ingress) and Outbound (Projection) queues
using SPSCQueue = LockFreeSPSCQueue<OrderEvent>;
using OutboundEventQueue = LockFreeSPSCQueue<events::OutboundEvent>;
