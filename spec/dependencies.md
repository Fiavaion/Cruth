# Dependency allow-list

The PRD forbids GPL-family code anywhere in the shipped app. A dependency may be added only once it is listed here with its licence and why it is needed. The core currently has **no third-party dependencies**.

| Library | Licence | Status | Use |
| --- | --- | --- | --- |
| OpenSubdiv | Apache-2.0 (modified) | Planned | Multires, Catmull-Clark (S-01) |
| Instant Meshes | BSD-3-Clause | Planned | Interactive quad-remesh preview (R-01) |
| QuadriFlow | BSD/MIT with `BUILD_FREE_LICENSE` | Planned | Final quad remesh (R-01) |
| geometry-central | MIT | Planned | BFF parameterisation (U-01) |
| Eigen | MPL-2.0 | Planned | Linear algebra for UV and remeshing |
| libigl (core only) | MPL-2.0 | Planned | LSCM and ARAP; never `igl/copyleft/` |
| xatlas | MIT | Evaluated, not used | Its polygon input path indexes faces as fixed-size; charting, LSCM and packing are in-house instead |
| tinyexr | BSD-3-Clause | Not needed | EXR and PNG writers, and their deflate encoder, are in-house (`core/src/image_io.cpp`) |
| OpenUSD | Apache-2.0 (modified) | Planned | Scene and project format |
| Dear ImGui | MIT | Planned (desktop) | Desktop shell UI |

**Rejected:** CGAL (GPL algorithms), Qt (LGPL, which conflicts with the App Store), RetopoFlow (GPL; its ideas are reimplemented, not its code), OpenVDB on iOS (too heavy; desktop only).
