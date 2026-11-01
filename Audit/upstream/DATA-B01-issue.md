# Draft upstream issue: overmap predecessors are lost on every load

Ready to paste into a new issue at https://github.com/CleverRaven/Cataclysm-DDA/issues
(the fork can't post it from here). Check first that it hasn't been reported or fixed
upstream since `a1a646d6`.

---

**Title:** Overmap terrain predecessors are thrown away on every load (regression from ff91f4a3)

**Describe the bug**

`overmap::unserialize` (src/savegame.cpp, the `"predecessors"` member) inserts an empty
vector for every point. The serialized list is moved into `om_predecessors` only after
that vector has been moved into the map, and is then cleared:

```cpp
std::vector<oter_id> om_predecessors;

for( auto& [p, serialized_predecessors] : flattened_predecessors ) {
    predecessors_.insert_or_assign( p, std::move( om_predecessors ) );   // always empty

    // Reuse allocations because it's a good habit.
    om_predecessors = std::move( serialized_predecessors );
    om_predecessors.clear();
}
```

Commit ff91f4a3 ("remove 0.H compatibilities") deleted the 0.H fixup block that used to
fill `om_predecessors` from `serialized_predecessors` before the insert, and left this loop
behind.

**Effect**

Every save and reload loses the predecessor chain of every overmap tile that hasn't been
generated yet. When the player later reaches a road, trail, stream or special that uses
`expects_predecessor` (90+ mapgens), mapgen falls back to `fallback_predecessor_mapgen`
(usually `field`) instead of the forest, swamp or lake shore it was placed over. The next
save writes the empty lists back, so the loss is permanent.

**Steps to reproduce**

1. Start a world, note an ungenerated road or trail running through forest on the
   overmap.
2. Save and reload.
3. Walk there: the road is generated on open field instead of forest.

Or as a test: build an overmap, set forest then `road_nesw` at a point (roads require a
predecessor), serialize, unserialize, and check the predecessors at that point.

**Fix**

Store the serialized list itself:

```cpp
for( auto& [p, serialized_predecessors] : flattened_predecessors ) {
    predecessors_.insert_or_assign( p, std::move( serialized_predecessors ) );
}
```

Lists written by the current `overmap::ter_set` are already deduplicated, so the removed
0.H fixup isn't needed for them.

**Versions**

Present at upstream commit `a1a646d6` (2026-10-31); introduced by ff91f4a3 (2026-09-20).
