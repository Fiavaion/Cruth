// Cruth core — minimal fork-join parallel loop over an index range.
//
// Used for embarrassingly parallel geometry passes (closest-point queries,
// relax steps). Each call runs fn(begin, end) on contiguous chunks across the
// hardware threads and returns when all are done. Small ranges run inline.
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>

namespace cruth {

template <typename Fn>
void parallel_for(std::size_t count, Fn&& fn, std::size_t min_chunk = 2048) {
    const std::size_t hw = std::max(1u, std::thread::hardware_concurrency());
    const std::size_t chunks = std::min(hw, (count + min_chunk - 1) / std::max<std::size_t>(min_chunk, 1));
    if (chunks <= 1) {
        fn(std::size_t(0), count);
        return;
    }
    std::vector<std::thread> workers;
    workers.reserve(chunks - 1);
    const std::size_t step = (count + chunks - 1) / chunks;
    for (std::size_t c = 1; c < chunks; ++c) {
        const std::size_t begin = c * step, end = std::min(count, begin + step);
        if (begin < end) workers.emplace_back([&fn, begin, end] { fn(begin, end); });
    }
    fn(std::size_t(0), std::min(count, step));
    for (auto& w : workers) w.join();
}

}  // namespace cruth
