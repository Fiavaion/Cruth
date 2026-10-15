# Cruth project format (draft 0)

Status: **draft, not implemented yet.** Phase 0 reads and writes OBJ, STL and PLY only. This file records the intended shape so the core's data model grows toward it (PRD feature 5).

A project is a plain folder, so it is git-friendly and readable without Cruth:

```
MyHead.cruth/
  project.json          format version, units, object list, finishing-graph settings
  scene.usda            OpenUSD scene: objects, transforms, materials, base meshes (level 0)
  objects/<id>/
    multires/level-N.bin  sparse per-level displacement deltas (little-endian, documented below)
    layers/<layer>.bin    per-level offsets and per-channel paint values
    mask.bin
    retopo/edits.json     manual retopo deltas re-applied by "Re-run"
    uv/<set>.json         seams, pins and packing settings
  brushes/*.json + alphas/*.exr
  materials/*.json
  maps/*.exr, maps/*.png   baked outputs
```

Rules:

- Every binary file has a JSON sidecar or a header stating its version, element type and count.
- Blender's USD importer must open `scene.usda` with no add-on and show the level-0 meshes.
- Writes are atomic per file (write to a temp file, then rename) so autosave cannot corrupt a project.
