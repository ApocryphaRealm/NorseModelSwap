# NorseModelSwap

Swap meshes and materials by rule, for the Norse overhaul (NORSE: Oath of Blood, UE4SS v4.0.0). Nothing in the game's
files changes. Modelled on Model Swapper (Skyrim).

```ini
[Enemies get the Berserker helmet]
Swap = /Game/Norse/Characters/Items/Armor/Helmets/SK_Helmet_Common_Tier1_01.SK_Helmet_Common_Tier1_01 -> /Game/Norse/Characters/Items/Armor/Helmets/SK_Helmet_Berserker_Unique_01.SK_Helmet_Berserker_Unique_01
When = IsEnemy
```

Two layers from the same `Swap = old -> new` lines (a mesh or a material):
- **Item data**: a rule with no `When` and no `Chance` also swaps the reference in the item assets (armour and
  clothing hold their meshes in `Male`/`Female`, armour its `Material`), at each map load.
- **Live actors**: the mesh components of units (`BP_Metahuman_Unit_C`: Helmet, Clothing, Bracers, Cape, Boots, Arms,
  Legs, Face, Hair, Beard) or any actor class named in `Actors =`. A component showing the old mesh gets the new one
  (SetSkeletalMeshAsset / SetSkinnedAssetAndUpdate / SetStaticMesh); a material slot holding the old material gets
  the new one (SetMaterial). `When` (NorseConditions) limits it to some units; `Chance` rolls per actor. Checked 1 s
  and 4 s after the actor begins play (a unit is dressed after it spawns), and on `mdl.apply` / `mdl.reload`.

Every target asset is loaded once and checked with `Alive()` before each use: the game frees loaded assets at a map
change even when they are marked root set (found in testing; see TEST-RESULT-0.1.0.txt), so a freed one is loaded
again.

Console: `mdl.list [class]` (meshes and materials on the first actors: where to find paths), `mdl.apply`,
`mdl.reload`; NorseDevTools reloads when a file in `models\` is saved. Author guide: `package\models\README.txt`;
tested rules: `docs\examples\test-models.ini`.

0.1.0 tested in game. Not yet: weapon actors, `Chance`.
