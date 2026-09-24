-- Young and Vicious (24626): route wild Swiftclaw around trees and buildings.
-- Following the denser route points directly still allowed the raptor to cross obstacles.
-- Restore pathfinding between the closer points while keeping the raptor running.
-- Remove the temporary pause that split the earlier direct spline into segments.

UPDATE `waypoint_path` SET `Flags`=0, `MoveType`=1 WHERE `PathId`=3798900;
UPDATE `waypoint_path_node` SET `Delay`=0 WHERE `PathId`=3798900 AND `Delay`<>0;
