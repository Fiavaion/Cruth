# Dependency allow-list

The PRD forbids GPL-family code anywhere in the shipped app. A dependency may be added only once it is listed here with its licence and why it is needed. Vendored code lives in `third_party/` and is summarised in `THIRD_PARTY_NOTICES.md`.

| Library | Licence | Status | Use |
| --- | --- | --- | --- |
| OpenSubdiv | Apache-2.0 (modified) | Planned | Multires, Catmull-Clark (S-01) |
| QuadriFlow | BSD-3-Clause | **Vendored** (patched: no Boost, no external SAT, no `exit()`) | Quad remesh (R-01) |
| LEMON | Boost Software License 1.0 | **Vendored** (header subset, no LP back-ends) | Min-cost flow for QuadriFlow |
| Eigen | MPL-2.0 (`EIGEN_MPL2_ONLY`) | **Vendored** (`Eigen/` only) | Linear algebra for QuadriFlow |
| Instant Meshes | BSD-3-Clause | Not needed for now | QuadriFlow covers R-01; an interactive preview may use it later |
| geometry-central | MIT | Planned | BFF parameterisation (U-01) |
| libigl (core only) | MPL-2.0 | Planned | LSCM and ARAP; never `igl/copyleft/` |
| xatlas | MIT | Evaluated, not used | Its polygon input path indexes faces as fixed-size; charting, LSCM and packing are in-house instead |
| tinyexr | BSD-3-Clause | Not needed | EXR and PNG writers, and their deflate encoder, are in-house (`core/src/image_io.cpp`) |
| OpenUSD | Apache-2.0 (modified) | Planned | Scene and project format |
| Dear ImGui | MIT | Planned (desktop) | Desktop shell UI |

**Rejected:** CGAL (GPL algorithms), Qt (LGPL, which conflicts with the App Store), RetopoFlow (GPL; its ideas are reimplemented, not its code), OpenVDB on iOS (too heavy; desktop only).
