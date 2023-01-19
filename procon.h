//
// Created by wuyua on 2023/1/18.
//

#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <semaphore>
#include <thread>

#ifdef PROCON_DEBUG
#define PROCON_LOG(fstr, ...) fmt::print("[PROCON]" fstr "\n", ##__VA_ARGS__)
#else
#define PROCON_LOG(fstr, ...)
#endif

template <typename T>
concept ProducerCbLike =
    requires(T a, void* buffer, size_t max_size) {
      { a(buffer, max_size) } -> std::convertible_to<size_t>;
    };

template <typename T>
concept ConsumerCbLike =
    requires(T a, const void* buffer, size_t buffer_size) {
      { a(buffer, buffer_size) };
    };

template <typename T>
concept BufferLike = std::is_pointer_v<T>;

template <
    int n_buffers = 2,
    typename Allocator = std::allocator<uint8_t>,
    ProducerCbLike ProducerCb,
    ConsumerCbLike ConsumerCb,
    BufferLike... ExtBuffers>
void procon(
    const ProducerCb& pro_cb,
    const ConsumerCb& con_cb,
    size_t buffer_size,
    ExtBuffers... ext_buffers) {
  constexpr bool use_external_buffer = sizeof...(ext_buffers) > 0;
  std::array<void*, n_buffers> buffers;
  Allocator allocator;
  if constexpr (use_external_buffer) {
    static_assert(sizeof...(ext_buffers) == n_buffers);
    // Use external buffer
    buffers = std::array{ext_buffers...};
  } else {
    // Allocate buffer
    for (int i = 0; i < n_buffers; ++i) {
      buffers[i] = allocator.allocate(buffer_size);
    }
  }

  // Variable declarations
  std::counting_semaphore<n_buffers + 1> n_filled_sem{0};
  std::counting_semaphore<n_buffers + 1> n_empty_sem{n_buffers};
  std::exception_ptr ex_ptr{nullptr};

  std::array<size_t, n_buffers> buffer_lengths{};
  std::atomic<bool> producer_done{false};
  std::atomic<bool> stop_requested{false};

  // Producer on new thread
  auto producer_thread = std::thread([&]() {
    size_t next_buffer_idx = 0;
    while (true) {
      n_empty_sem.acquire();
      if (stop_requested.load()) {
        producer_done = true;
        return;
      }
      auto buffer = (void*)buffers[next_buffer_idx];
      size_t buffer_len = 0;
      try {
        buffer_len = pro_cb(buffer, buffer_size);
      } catch (...) {
        ex_ptr = std::current_exception();
        // To unlock the load function and receive this exception
        n_filled_sem.release();
        return;
      }

      buffer_lengths[next_buffer_idx] = buffer_len;
      PROCON_LOG("producing #{} length = {}", next_buffer_idx, buffer_len);

      next_buffer_idx = (next_buffer_idx + 1) % n_buffers;

      if (buffer_len == 0) {
        PROCON_LOG("Producer done");
        producer_done = true;
        n_filled_sem.release();
        return;
      } else {
        n_filled_sem.release();
      }
    }
  });

  // Consumer (on current thread)
  std::exception_ptr consumer_exception{nullptr};
  size_t consumer_buffer_idx{0};
  try {
    while (true) {
      n_filled_sem.acquire();

      if (ex_ptr) {
        std::rethrow_exception(ex_ptr);
      }

      const void* buffer = buffers[consumer_buffer_idx];
      size_t read_len = buffer_lengths[consumer_buffer_idx];
      PROCON_LOG("consuming #{} length = {}", consumer_buffer_idx, read_len);
      if (read_len == 0) {
        // exit normally
        break;
      }
      // May throw, to terminate iteration.
      con_cb(buffer, read_len);

      consumer_buffer_idx = (consumer_buffer_idx + 1) % n_buffers;
      n_empty_sem.release();
    }

  } catch (...) {
    consumer_exception = std::current_exception();
  }

  // Finalize
  PROCON_LOG("Finalizing");
  if (producer_thread.joinable()) {
    stop_requested = true;
    n_empty_sem.release();
    PROCON_LOG("Joining producer thread");
    producer_thread.join();
    PROCON_LOG("Joined producer thread");
  }

  if constexpr (use_external_buffer) {
    PROCON_LOG("Skip deallocating borrowed buffers");
  } else {
    PROCON_LOG("Deallocating buffers");
    for (int i = 0; i < n_buffers; ++i) {
      allocator.deallocate((uint8_t*)buffers[i], buffer_size);
    }
  }

  if (consumer_exception) {
    PROCON_LOG("Consumer exception thrown");
    std::rethrow_exception(consumer_exception);
  }
}