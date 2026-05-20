#pragma once
#include <atomic>
#include <vector>
#include <cstddef>
#include "network_protocol.h"

class SPSCQueue {
private:
    std::vector<OrderEvent> buffer;
    const size_t capacity;
    
    // Prevent false sharing by aligning atomic variables to typical cache line sizes (64 bytes)
    alignas(64) std::atomic<size_t> head{0}; 
    alignas(64) std::atomic<size_t> tail{0};

public:
    SPSCQueue(size_t size) : buffer(size), capacity(size) {}

    // Called ONLY by the Producer (Network) thread
    bool push(const OrderEvent& event) {
        size_t current_tail = tail.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) % capacity;

        // If the next tail catches up to the head, the queue is full
        if (next_tail == head.load(std::memory_order_acquire)) {
            return false; 
        }

        buffer[current_tail] = event;
        tail.store(next_tail, std::memory_order_release); // Publish the write
        return true;
    }

    // Called ONLY by the Consumer (Matching Engine) thread
    bool pop(OrderEvent& out_event) {
        size_t current_head = head.load(std::memory_order_relaxed);

        // If head equals tail, the queue is empty
        if (current_head == tail.load(std::memory_order_acquire)) {
            return false; 
        }

        out_event = buffer[current_head];
        head.store((current_head + 1) % capacity, std::memory_order_release);
        return true;
    }
};