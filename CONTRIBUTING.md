# Contributing to Cruth

Thanks for helping. A few rules keep the project healthy and shippable on the App Store.

## Sign-off (DCO), no CLA

Every commit must carry a `Signed-off-by:` line certifying the [Developer Certificate of Origin](https://developercertificate.org/). Use `git commit -s`. There is no contributor licence agreement.

## Licences

- The project is MIT. New files start with `// SPDX-License-Identifier: MIT`.
- **No GPL, LGPL or AGPL code, directly or transitively.** The App Store cannot ship it. Check new dependencies against [`spec/dependencies.md`](spec/dependencies.md) and add them there in the same pull request. CI rejects GPL-family SPDX headers.

## Architecture rules

- **The core owns every algorithm.** Shells hold only input, windowing, rendering and file access. If a feature needs geometry, it goes in `core/` behind a `Document` command and is reachable from `cruth-cli`, so it can be tested headless.
- **No exceptions across the C API.** `cruth.h` is plain C. Fallible core functions return `Status` or `Result<T>`, and `c_api.cpp` catches everything at the boundary.
- **Test it headless.** Add a unit test in `tests/`, and for a user-facing chain step, extend `tests/cli_chain.cmake`.

## Before you open a pull request

```sh
cmake --preset release && cmake --build --preset release && ctest --preset release
cmake --preset asan && cmake --build --preset asan && ctest --preset asan
```

If you touched `shell-ipad/`, build it in Xcode too (see the README). CI builds it for the simulator on every push.
