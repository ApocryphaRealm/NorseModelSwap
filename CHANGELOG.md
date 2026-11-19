# Changelog

## 1.0.0 - 2026-11-18 - working

### Added
- First release. It contains everything built and tested in 0.1.0, listed here and kept as history below:
- Swap = old -> new for meshes and materials: in item data (rules with no When/Chance) and on spawned actors (When, Actors, Chance).
- Console mdl.list, mdl.apply, mdl.reload.
- main.dll carries a Windows version resource (file and product version 1.0.0, shown in its Properties > Details).

### Fixed
- A crash on the second map load: swap targets freed at the map change are checked with Alive() and loaded again.
- Conditions are compiled from the first map load on, not at start-up (they no longer depend on loading after NorseConditions).

### Changed
- Writes into game data are recorded and re-imported at each map load when their assets are no longer loaded (NorseReflect RecordWrite/RecheckWrites) - a safety net; the freeing it guards against was seen once and did not repeat in testing.

## 0.1.0 - 2026-11-18 (tested in game: TEST-RESULT-0.1.0.txt)
### Added
- Swap = old -> new for meshes and materials: in item data (rules with no When/Chance) and on spawned actors (When, Actors, Chance).
- Console mdl.list, mdl.apply, mdl.reload.
### Fixed
- A crash on the second map load: swap targets freed at the map change are checked with Alive() and loaded again.

### Changed (2026-11-18, refinement)
- Writes into game data are recorded and re-imported at each map load when their assets are no longer loaded (NorseReflect RecordWrite/RecheckWrites) - a safety net; the freeing it guards against was seen once and did not repeat in testing.

### Fixed (2026-11-18, refinement)
- Conditions are compiled from the first map load on, not at start-up (they no longer depend on loading after NorseConditions).
