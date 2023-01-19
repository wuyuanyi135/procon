#define PROCON_DEBUG
#include <fmt/chrono.h>
#include <fmt/core.h>

#include <chrono>
#include <thread>

#include "catch_amalgamated.hpp"
#include "procon.h"

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace fmt;

#define NOW system_clock::now()

TEST_CASE("simple procon") {
  std::vector<uint8_t> data;
  data.resize(1000);
  for (int i = 0; i < data.size(); ++i) {
    data[i] = rand();
  }

  auto make_producer = [&] {
    auto it = std::make_shared<size_t>(0);
    auto data_len = data.size();

    return [=](void* buffer, size_t max_len) -> size_t {
      auto len = std::min(max_len, data_len - *it);

      if (len == 0) {
        return 0;
      }

      std::this_thread::sleep_for(0.1s);
      std::copy(data.begin() + *it, data.begin() + *it + len, (uint8_t*)buffer);
      print("Produced {} into {} \n", len, ptr(buffer));
      *it += len;
      return len;
    };
  };

  auto make_consumer = [&] {
    auto it = std::make_shared<int>(0);
    return [it, &data](const void* buffer, size_t len) {
      REQUIRE(std::equal(data.begin() + *it, data.begin() + *it + len, (uint8_t*)buffer));
      std::this_thread::sleep_for(0.1s);
      *it += len;
      print("Consumed {} from {} \n", len, ptr(buffer));
    };
  };

  auto t_serial_1 = NOW;
  {
    auto producer = make_producer();
    auto consumer = make_consumer();

    std::vector<uint8_t> buffer;
    buffer.resize(100);
    while (true) {
      size_t len = producer(buffer.data(), buffer.size());
      if (len == 0) {
        break;
      }
      consumer(buffer.data(), len);
    }
  }
  auto t_serial_2 = NOW;

  auto t_procon_1 = NOW;
  {
    auto producer = make_producer();
    auto consumer = make_consumer();
    procon(
        producer,
        consumer,
        100);
  }
  auto t_procon_2 = NOW;

  print("Serial: {}\n", t_serial_2 - t_serial_1);
  print("Procon: {}\n", t_procon_2 - t_procon_1);
}

TEST_CASE("error by producer") {
  int i = 0;
  REQUIRE_THROWS_AS(
      procon(
          [&](void* buffer, size_t max_len) -> size_t {
            if (i++ > 3) {
              throw std::runtime_error("stop");
            } else {
              return max_len;
            }
            print("Producing buffer\n");
            return max_len;
          },
          [](const void* buffer, size_t len) {
            print("Consuming buffer\n");
          },
          512),
      std::runtime_error);
}

TEST_CASE("pause by consumer") {
  int i = 0;
  REQUIRE_THROWS_AS(
      procon(
          [](void* buffer, size_t max_len) -> size_t {
            print("Producing buffer\n");
            return max_len;
          },
          [&](const void* buffer, size_t len) {
            if (i++ > 2) {
              print("Stop requested\n");
              throw std::runtime_error("stop");
            } else {
              print("Consuming buffer\n");
            }
          },
          512),
      std::runtime_error);
}

TEST_CASE("external buffer") {
  std::vector<uint8_t> ext_buf1;
  std::vector<uint8_t> ext_buf2;
  std::vector<uint8_t> ext_buf3;

  ext_buf1.resize(512);
  ext_buf2.resize(512);
  ext_buf3.resize(512);

  int i = 0;

  procon<3>(
      [&](void* buffer, size_t max_len) -> size_t {
        using Catch::Matchers::Contains;

        auto ext_buffers = std::array{ext_buf1.data(), ext_buf2.data(), ext_buf3.data()};
        REQUIRE_THAT(ext_buffers, Contains(buffer));

        if (i++ > 3) {
          return 0;
        } else {
          return max_len;
        }
      },
      [&](const void* buffer, size_t len) {
      },
      512,
      (void*)ext_buf1.data(),
      (void*)ext_buf2.data(),
      (void*)ext_buf3.data());
}

