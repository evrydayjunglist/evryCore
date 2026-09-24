-- Young and Vicious (24626): wild Swiftclaw (37989) patrols the island.
-- Follow the existing route points directly so failed path searches do not shorten the patrol.
-- Flags=2 selects ExactSplinePath, as used by other Durotar paths such as 3923900.
-- The retail capture shows Swiftclaw making a full island circuit.
-- Later updates add closer route points and restore pathfinding around obstacles.

UPDATE `waypoint_path` SET `Flags`=2 WHERE `PathId`=3798900;
