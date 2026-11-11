/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cucascade/exec/invocable.hpp>
#include <cucascade/exec/thread_util.hpp>
#include <cucascade/log/logging.hpp>

#include <concepts>
#include <condition_variable>
#include <exception>
#include <functional>
#include <latch>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace cucascade::exec {

class static_thread_pool {
 public:
  /// Start all workers before returning. Names and best-effort affinity are
  /// applied before per_thread_init. An empty CPU list uses the process main
  /// thread's mask, rather than inheriting a potentially pinned creator.
  /// Throws for a non-positive width or failed initialization, after joining
  /// any started workers. The initializer must be safe to call concurrently.
  explicit static_thread_pool(int num_threads,
                              const std::string& name               = "thread_pool",
                              std::vector<int> cpu_ids              = {},
                              std::function<void()> per_thread_init = nullptr)
  {
    if (num_threads <= 0) { throw std::invalid_argument("thread pool width must be positive"); }
    threads_.reserve(static_cast<std::size_t>(num_threads));
    if (cpu_ids.empty()) { cpu_ids = thread_util::get_process_affinity(); }

    std::latch initialized{num_threads};
    std::exception_ptr init_error;
    try {
      for (int i = 0; i < num_threads; ++i) {
        auto worker_name = name.empty() ? std::string{} : name + "_" + std::to_string(i);
        threads_.emplace_back([&, worker_name = std::move(worker_name)] {
          bool ready = false;
          try {
            if (!worker_name.empty()) { (void)thread_util::set_current_thread_name(worker_name); }
            (void)thread_util::set_current_thread_affinity(cpu_ids);
            if (per_thread_init) { per_thread_init(); }
            ready = true;
          } catch (...) {
            std::lock_guard lock(mu_);
            if (!init_error) { init_error = std::current_exception(); }
          }
          initialized.count_down();
          if (ready) { work_loop(); }
        });
      }
      initialized.wait();
      if (init_error) { std::rethrow_exception(init_error); }
    } catch (...) {
      // The destructor does not run if construction fails. Keep the startup
      // state above alive until every started worker has stopped using it.
      stop();
      for (auto& t : threads_) {
        if (t.joinable()) { t.join(); }
      }
      throw;
    }
  }

  static_thread_pool(const static_thread_pool&)            = delete;
  static_thread_pool& operator=(const static_thread_pool&) = delete;

  ~static_thread_pool()
  {
    stop();
    for (auto& t : threads_) {
      if (t.joinable()) { t.join(); }
    }
  }

  void schedule(std::invocable auto&& fn)
  {
    std::lock_guard l(mu_);
    queue_.emplace([callable = std::forward<decltype(fn)>(fn)]() mutable noexcept {
      try {
        callable();
      } catch (const std::exception& e) {
      } catch (...) {
      }
    });
    cv_.notify_one();
  }

  /// \brief Alias for schedule(). Lets static_thread_pool satisfy the same
  ///        scheduler shape as scoped_dispatcher (which exposes enqueue()),
  ///        so generic dispatchers can target either.
  void enqueue(std::invocable auto&& fn) { schedule(std::forward<decltype(fn)>(fn)); }

  [[nodiscard]] std::size_t num_threads() const noexcept { return threads_.size(); }

  void stop() noexcept
  {
    std::unique_lock l(mu_);
    stop_requested_ = true;
    cv_.notify_all();
  }

 private:
  [[nodiscard]] bool has_work_or_stopped() const { return !queue_.empty() || stop_requested_; }

  void work_loop()
  {
    while (!stop_requested_) {
      invocable<void()> func;
      {
        std::unique_lock<std::mutex> l(mu_);
        cv_.wait(l, [this] { return has_work_or_stopped(); });
        if (stop_requested_) { break; }
        func = std::move(queue_.front());
        queue_.pop();
      }
      if (func == nullptr) { break; }
      func();
    }
  }

  std::mutex mu_;
  std::condition_variable cv_;
  std::queue<invocable<void()>> queue_;
  std::atomic<bool> stop_requested_{false};
  std::vector<std::thread> threads_;
};

}  // namespace cucascade::exec
