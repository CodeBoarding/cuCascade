/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cucascade/exec/thread_pool.hpp>
#include <cucascade/exec/thread_util.hpp>

#include <catch2/catch_test_macros.hpp>
#include <sched.h>

#include <chrono>
#include <future>
#include <string>
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

TEST_CASE("thread pool affinity uses valid CPU IDs and preserves an empty request", "[thread_pool]")
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
