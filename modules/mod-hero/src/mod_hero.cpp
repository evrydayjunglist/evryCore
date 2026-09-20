/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Log.h"
#include "ScriptMgr.h"

// Hero, class 16, and the progression that will eventually fill it.
//
// Hero comes from Conquest of Azeroth, but it does not live in mod-coa beside
// Reaper, and that is deliberate. Reaper is a class with a kit. Hero is a
// container for a character who has no class: what a Hero can do is meant to
// come from Free Pick and Wildcard, an economy of ability and talent essence
// spent on abilities borrowed from every other class. That system will be
// larger than everything in mod-coa, it needs settings of its own, and it has
// nothing to do with Reaper. Keeping it separate also means Hero's data can be
// turned off on its own, because the updater applies a module's SQL only while
// that module is enabled.
//
// One deliberate difference from Conquest of Azeroth: there, Hero is what you
// are on a classless realm and it replaces the class system. Here it is a
// sixteenth class you pick instead of Paladin or Mage. That is a product
// decision, not an oversight, and it is why there is no realm-wide classless
// mode anywhere in this module.
//
// The class rows will live under data/sql. There are no commands on purpose:
// character creation for a class beyond the client's own fifteen is proven, so
// a Hero can be made on the creation screen the way any character is, and
// `.character erase` already removes one from the server console if a Hero ever
// stops the client drawing the character list.

class HeroWorldScript : public WorldScript
{
public:
    HeroWorldScript() : WorldScript("mod_hero_WorldScript") { }

    void OnStartup() override
    {
        TC_LOG_INFO("server.loading", "mod-hero: loaded");
    }
};

void Addmod_heroScripts()
{
    new HeroWorldScript();
}
