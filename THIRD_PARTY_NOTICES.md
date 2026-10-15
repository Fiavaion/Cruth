# Third-party notices

Cruth is MIT-licensed. It includes the following third-party code, all under permissive licences
compatible with App Store distribution. Full licence texts are in each directory.

| Component | Version | Licence | Location | Used for |
| --- | --- | --- | --- | --- |
| QuadriFlow | 810b7a0 (2019-12-07) | BSD-3-Clause | `third_party/quadriflow/LICENSE.txt` | Quad remeshing (PRD R-01); local changes in `third_party/quadriflow/CRUTH_PATCHES.md` |
| PCG32 (header, as shipped with QuadriFlow) | — | Apache-2.0 | `third_party/quadriflow/3rd/pcg32/pcg32.h` | Random numbers inside QuadriFlow |
| LEMON graph library (header subset) | 1.3.1 | Boost Software License 1.0 | `third_party/lemon/LICENSE` | Min-cost flow inside QuadriFlow |
| Eigen (`Eigen/` headers only) | 3.4.0 | MPL-2.0, compiled with `EIGEN_MPL2_ONLY` | `third_party/eigen/COPYING.*` | Linear algebra inside QuadriFlow |

No GPL or LGPL code is included. CI rejects GPL-family SPDX headers, and Eigen's `EIGEN_MPL2_ONLY`
turns any inclusion of non-MPL-2 Eigen code into a compile error.
