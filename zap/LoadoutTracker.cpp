//-----------------------------------------------------------------------------------
//
// Bitfighter - A multiplayer vector graphics space game
// Based on Zap demo released for Torque Network Library by GarageGames.com
//
// Derivative work copyright (C) 2008-2009 Chris Eykamp
// Original work copyright (C) 2004 GarageGames.com, Inc.
// Other code copyright as noted
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful (and fun!),
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
//------------------------------------------------------------------------------------


#include "LoadoutTracker.h"
#include "game.h"             // For getModuleInfo
#include "stringUtils.h"      // For parseString

#include "tnlLog.h"


namespace Zap
{

// Constructor
LoadoutTracker::LoadoutTracker(const string &loadoutStr)
{
   resetLoadout();

   // If we have a loadout string, try to get something useful out of it.
   // Note that even if we are able to parse the loadout successfully, it might still be invalid for a 
   // particular server or gameType... engineer, for example, is not allowed everywhere.
   if(loadoutStr == "")
      return;

   Vector<string> words;
   parseString(loadoutStr, words, ',');

   if(words.size() != ShipModuleCount + ShipWeaponCount)      // Invalid loadout string
   {
      logprintf(LogConsumer::ConfigurationError, "Misconfigured loadout preset found in INI");
      return;
   }

   bool found;

   for(S32 i = 0; i < ShipModuleCount; i++)
   {
      found = false;
      const char *word = words[i].c_str();

      for(S32 j = 0; j < ModuleCount; j++)
         if(stricmp(word, Game::getModuleInfo((ShipModule) j)->getName()) == 0)     // Case insensitive
         {
            mModules[i] = ShipModule(j);
            found = true;
            break;
         }

      if(!found)
      {
         logprintf(LogConsumer::ConfigurationError, "Unknown module found in loadout preset in INI file: %s", word);
         resetLoadout();
         return;
      }
   }

   for(S32 i = 0; i < ShipWeaponCount; i++)
   {
      found = false;
      const char *word = words[i + ShipModuleCount].c_str();

      for(S32 j = 0; j < WeaponCount; j++)
         if(stricmp(word, GameWeapon::weaponInfo[j].name.getString()) == 0)
         {
            mWeapons[i] = WeaponType(j);
            found = true;
            break;
         }

      if(!found)
      {
         logprintf(LogConsumer::ConfigurationError, "Unknown weapon found in loadout preset in INI file: %s", word);
         resetLoadout();
         return;
      }
   }
}


// Reset this loadout to its factory settings
void LoadoutTracker::resetLoadout()
{
   for(S32 i = 0; i < ShipModuleCount; i++)
      mModules[i] = ModuleNone;

   for(S32 i = 0; i < ShipWeaponCount; i++)
      mWeapons[i] = WeaponNone;

   deactivateAllModules();
   mActiveWeapon = 0;
}


// Returns true if anything changed
bool LoadoutTracker::update(const LoadoutTracker &loadout)
{
   bool loadoutChanged = false;

   for(S32 i = 0; i < ShipModuleCount; i++)
      if(mModules[i] != loadout.mModules[i])
      {
         mModules[i] = loadout.mModules[i];
         loadoutChanged = true;
      }

   for(S32 i = 0; i < ModuleCount; i++)
   {
      mModulePrimaryActive[i] = loadout.mModulePrimaryActive[i];  
      mModuleSecondaryActive[i] = loadout.mModuleSecondaryActive[i];
   }

   for(S32 i = 0; i < ShipWeaponCount; i++)
      if(mWeapons[i] != loadout.mWeapons[i])
      {
         mWeapons[i] = loadout.mWeapons[i];
         loadoutChanged = true;
      }

   return loadoutChanged;
}


// Takes an array of U8s repesenting loadout... M,M,W,W,W.  See DefaultLoadout for an example
void LoadoutTracker::setLoadout(const U8 *items)
{
   for(S32 i = 0; i < ShipModuleCount; i++)
      mModules[i] = (ShipModule) items[i];

   for(S32 i = 0; i < ShipWeaponCount; i++)
      mWeapons[i] = (WeaponType) items[i + ShipModuleCount];
}


void LoadoutTracker::setModule(U32 moduleIndex, ShipModule module)
{
   mModules[moduleIndex] = module;
}


void LoadoutTracker::setWeapon(U32 weaponIndex, WeaponType weapon)
{
   mWeapons[weaponIndex] = weapon;
}


void LoadoutTracker::setActiveWeapon(U32 weaponIndex)
{
   mActiveWeapon = weaponIndex % ShipWeaponCount;
}


void LoadoutTracker::setModulePrimary(ShipModule module, bool isActive)
{
   mModulePrimaryActive[module] = isActive;
}


void LoadoutTracker::setModuleIndxPrimary(U32 moduleIndex, bool isActive)
{
   mModulePrimaryActive[mModules[moduleIndex]] = isActive;
}


void LoadoutTracker::setModuleSecondary(ShipModule module, bool isActive)
{
    mModuleSecondaryActive[module] = isActive;
}


void LoadoutTracker::setModuleIndxSecondary(U32 moduleIndex, bool isActive)
{
   mModuleSecondaryActive[mModules[moduleIndex]] = isActive;
}


void LoadoutTracker::deactivateAllModules()
{
   for(S32 i = 0; i < ModuleCount; i++)
   {
      mModulePrimaryActive[i] = false;
      mModuleSecondaryActive[i] = false;
   }
}


bool LoadoutTracker::hasModule(ShipModule mod) const
{
   for(U32 i = 0; i < ShipModuleCount; i++)
      if(mModules[i] == mod)
         return true;

   return false;
}


bool LoadoutTracker::isValid() const
{
   return mModules[0] != ModuleNone;
}


bool LoadoutTracker::isWeaponActive(U32 weaponIndex) const
{
   return weaponIndex == mActiveWeapon;
}


WeaponType LoadoutTracker::getWeapon(U32 weaponIndex) const
{
   return mWeapons[weaponIndex];
}


WeaponType LoadoutTracker::getCurrentWeapon() const
{
   return mWeapons[mActiveWeapon];
}


U32 LoadoutTracker::getCurrentWeaponIndx() const
{
   return mActiveWeapon;
}


ShipModule LoadoutTracker::getModule(U32 modIndex) const
{
   return mModules[modIndex];
}


bool LoadoutTracker::isModulePrimaryActive(ShipModule module) const
{
      return mModulePrimaryActive[module];
}


bool LoadoutTracker::isModuleSecondaryActive(ShipModule module) const
{
   return mModuleSecondaryActive[module];
}


Vector<U8> LoadoutTracker::loadoutToVector() const
{
   Vector<U8> loadout(ShipModuleCount + ShipWeaponCount);

   for(S32 i = 0; i < ShipModuleCount; i++)
      loadout.push_back(U8(mModules[i]));

   for(S32 i = 0; i < ShipWeaponCount; i++)
      loadout.push_back(U8(mWeapons[i]));

   return loadout;
}


// Will return an empty string if loadout looks invalid
string LoadoutTracker::loadoutToString(const Vector<U8> &loadout)
{
   // Only expect missized loadout when presets haven't all been set, and loadout.size will be 0
   if(loadout.size() != ShipModuleCount + ShipWeaponCount)
      return "";

   Vector<string> loadoutStrings(ShipModuleCount + ShipWeaponCount);    // Reserving some space makes things a tiny bit more efficient

   // First modules
   for(S32 i = 0; i < ShipModuleCount; i++)
      loadoutStrings.push_back(Game::getModuleInfo((ShipModule) loadout[i])->getName());

   // Then weapons
   for(S32 i = ShipModuleCount; i < ShipWeaponCount + ShipModuleCount; i++)
      loadoutStrings.push_back(GameWeapon::weaponInfo[loadout[i]].name.getString());

   return listToString(loadoutStrings, ',');
}

}