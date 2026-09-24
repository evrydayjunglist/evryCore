-- Young and Vicious (24626): remove the Swiftclaw vehicle when its rider leaves early.
-- The owner observed an abandoned vehicle 38002 that remained and could not be lassoed again.
-- This cleanup approximates retail behavior because the capture only shows a completed ride.

UPDATE `creature_template` SET `ScriptName`='npc_swiftclaw_vehicle_young_and_vicious' WHERE `entry`=38002;
