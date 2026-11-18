NorseModelSwap rules
====================

Every *.ini here swaps a mesh or a material for another. Each [section] is one rule (its name is only a label);
rules run in load order, and nothing in the game's files changes.

    [Outcasts wear a different helmet]
    Swap = /Game/Norse/.../SK_Helmet_A.SK_Helmet_A -> /Game/MyPack/.../SK_Helmet_B.SK_Helmet_B   ; mesh or material, repeatable
    When = IsEnemy AND UnitType(Outcast_*)    ; optional, NorseConditions: only on these units
    Actors = BP_Metahuman_Unit_C              ; whose meshes (class names, wildcards); default: the units
    Chance = 100                              ; per actor, the same roll every time

A rule with no When and no Chance also swaps the reference in the item data (armour and clothing), so every unit
wearing that item changes. Spawned actors are checked 1 s and 4 s after they appear (a unit is dressed after it
spawns). Console: mdl.list [class] (the meshes and materials on the first units - where to find the paths),
mdl.apply, mdl.reload.
