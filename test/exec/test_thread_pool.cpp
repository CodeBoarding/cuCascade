/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cucascade/exec/thread_pool.hpp>
#include <cucascade/exec/thread_util.hpp>

#include <catch2/catch_all.hpp>
#include <sched.h>

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using cucascade::exec::static_thread_pool;
namespace thread_util = cucascade::exec::thread_util;

TEST_CASE("thread pool names fit the Linux thread name limit", "[thread_pool]")
{
  for (auto const& name : {std::string{"io"}, std::string{"footer-one-worker"}}) {
    CAPTURE(name);
    std::promise<std::string> observed;
    auto result = observed.get_future();
    static_thread_pool pool{1, name};
    pool.schedule([&observed] { observed.set_value(thread_util::get_current_thread_name()); });
    REQUIRE(result.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
    CHECK(result.get() == (name + "_0").substr(0, 15));
  }
}

TEST_CASE("thread pool affinity uses valid CPU IDs and defaults to process CPUs", "[thread_pool]")
{
  auto const inherited = thread_util::get_current_thread_affinity();
  REQUIRE_FALSE(inherited.empty());
  auto const cpu = inherited.back();

  for (auto const& requested :
       {std::vector<int>{}, std::vector<int>{cpu}, std::vector<int>{-1, cpu, CPU_SETSIZE}}) {
    CAPTURE(requested);
    std::promise<std::vector<int>> observed;
    auto result = observed.get_future();
    static_thread_pool pool{1, "affinity", requested};
    pool.schedule([&observed] { observed.set_value(thread_util::get_current_thread_affinity()); });
    REQUIRE(result.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
    CHECK(result.get() == (requested.empty() ? inherited : std::vector<int>{cpu}));
  }
}

TEST_CASE("thread pool applies naming and affinity before initialization", "[thread_pool]")
{
  auto const cpus = thread_util::get_current_thread_affinity();
  REQUIRE_FALSE(cpus.empty());
  std::string observed_name;
  std::vector<int> observed_cpus;
  static_thread_pool pool{1, "worker-bootstrap-long", {cpus.back()}, [&] {
                            observed_name = thread_util::get_current_thread_name();
                            observed_cpus = thread_util::get_current_thread_affinity();
                          }};
  CHECK(observed_name == "worker-bootstra");
  CHECK(observed_cpus == std::vector<int>{cpus.back()});
}

TEST_CASE("thread pool does not inherit a pinned creator's CPU restriction", "[thread_pool]")
{
  // Read the test main thread's actual mask, independently of get_process_affinity.
  auto const process_cpus = thread_util::get_current_thread_affinity();
  REQUIRE_FALSE(process_cpus.empty());
  if (process_cpus.size() < 2) { SKIP("requires at least two allowed CPUs"); }

  auto result                                        = std::async(std::launch::async, [&] {
    std::array<int, 1> pinned{process_cpus.front()};
    if (!thread_util::set_current_thread_affinity(pinned)) {
      throw std::runtime_error("cannot pin the creator thread");
    }
    auto creator_cpus = thread_util::get_current_thread_affinity();
    auto queried_cpus = thread_util::get_process_affinity();
    std::vector<int> initialized_cpus;
    std::promise<std::vector<int>> observed;
    auto worker = observed.get_future();
    static_thread_pool pool{1, "process-cpus", {}, [&] {
                              initialized_cpus = thread_util::get_current_thread_affinity();
                            }};
    pool.schedule([&] { observed.set_value(thread_util::get_current_thread_affinity()); });
    return std::make_tuple(creator_cpus, queried_cpus, initialized_cpus, worker.get());
  });
  auto const [creator, queried, initialized, worker] = result.get();
  CHECK(creator == std::vector<int>{process_cpus.front()});
  CHECK(queried == process_cpus);
  CHECK(initialized == process_cpus);
  CHECK(worker == process_cpus);
}

TEST_CASE("thread pool propagates initialization failures after joining every worker",
          "[thread_pool]")
{
  std::atomic<int> initialized{0};
  std::atomic<int> exited{0};
  struct exit_counter {
    std::atomic<int>& count;
    ~exit_counter() { ++count; }
  };
  auto initialize = [&] {
    thread_local exit_counter on_exit{exited};
    if (initialized.fetch_add(1) == 1) { throw std::runtime_error("worker initialization failed"); }
  };

  CHECK_THROWS_WITH((static_thread_pool{3, "startup", {}, initialize}),
                    "worker initialization failed");
  CHECK(initialized == 3);
  CHECK(exited == 3);
}

TEST_CASE("thread pool rejects widths that cannot execute work", "[thread_pool]")
{
  CHECK_THROWS_AS(static_thread_pool{0}, std::invalid_argument);
  CHECK_THROWS_AS(static_thread_pool{-1}, std::invalid_argument);
}
