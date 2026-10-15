# Cruth changes to QuadriFlow

Upstream: https://github.com/hjwdzh/QuadriFlow at 810b7a0967c35b0dc85b4464e3835e26a756c967 (BSD-3-Clause, see LICENSE.txt).
Vendored: `src/` without `main.cpp` and `Optimizer.cu`; `3rd/pcg32/pcg32.h` (Apache-2.0).
Every change is marked `[Cruth patch]` in the source.

- `flow.hpp`, `optimizer.cpp`: Boost.Graph is optional (`QUADRIFLOW_WITH_BOOST`). Without it the
  Boykov–Kolmogorov max-flow helper is compiled out and lemon's network simplex (min-cost flow)
  solves the large flow problems instead. A plain push-relabel max flow was tried and rejected:
  its flow cycles corrupt the integer edge offsets.
- `post-solver.cpp` (a stand-alone Ceres-based tool, not part of upstream's library build) is not vendored; only its header is used.
- `localsat.cpp`, `hierarchy.cpp`: with `QUADRIFLOW_NO_EXTERNAL_SAT` the optional "aggressive SAT"
  pass, which shells out to a `minisat` binary through `system()`, is compiled out. iOS has no
  `system()`, and a library must not spawn processes.
- `hierarchy.cpp`, `optimizer.cpp`: `exit()` on internal-consistency failures now throws
  `std::runtime_error`, which Cruth's wrapper reports as an error instead of terminating the app.
- `loader.cpp`: `obj_vertexHash` no longer derives from `std::unary_function`, which C++17 removed
  (libc++ and MSVC no longer provide it).

## lemon

Vendored header subset with a hand-written `lemon/config.h` (`LEMON_ONLY_TEMPLATES`).

- `lemon/bits/array_map.h`: element construction and destruction go through
  `std::allocator_traits` instead of `std::allocator::construct`/`destroy`, which C++20 removed.
  This lets QuadriFlow compile as C++20 against libc++ (Xcode's default).
