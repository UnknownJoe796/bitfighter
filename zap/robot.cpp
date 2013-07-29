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

#include "robot.h"

#include "playerInfo.h"          // For RobotPlayerInfo constructor
#include "BotNavMeshZone.h"      // For BotNavMeshZone class definition
#include "gameObjectRender.h"

#include "MathUtils.h"           // For findLowestRootIninterval()
#include "GeomUtils.h"

#include "ServerGame.h"


#define hypot _hypot    // Kill some warnings

namespace Zap
{

////////////////////////////////////////
////////////////////////////////////////

TNL_IMPLEMENT_NETOBJECT(Robot);

// Combined Lua / C++ default constructor, runs on client and server
Robot::Robot(lua_State *L) : Ship(NULL, TEAM_NEUTRAL, Point(0,0), true),   
                             LuaScriptRunner() 
{
   // For now...  In future we'll need to specify a script in our L object, then we can instantiate a bot
   if(L)
   {
      luaL_error(L, "Currently cannot instantiate a Robot object from Lua.  This will be changed in the near future!");
      return;
   }

   mHasSpawned = false;
   mObjectTypeNumber = RobotShipTypeNumber;

   mCurrentZone = U16_MAX;
   flightPlanTo = U16_MAX;

   mPlayerInfo = new RobotPlayerInfo(this);
   mScore = 0;
   mTotalScore = 0;

#ifndef ZAP_DEDICATED
   mShapeType = ShipShape::Normal;
#endif

   mScriptType = ScriptTypeRobot;

   LUAW_CONSTRUCTOR_INITIALIZATIONS;
}


// Destructor, runs on client and server
Robot::~Robot()
{
   // Items will be dismounted in Ship (Parent) destructor
   setOwner(NULL);

   if(isClient())
   {
      delete mPlayerInfo;     // On the server, mPlayerInfo will be deleted below, after event is fired
      return;
   }

   dismountAll();  // fixes dropping CTF flag without ClientInfo...

   // Server only from here on down
   if(getGame())  // can be NULL if this robot was never added to game (bad / missing robot file)
   {
      EventManager::get()->fireEvent(this, EventManager::PlayerLeftEvent, getPlayerInfo());

      if(getGame()->getGameType())
         getGame()->getGameType()->serverRemoveClient(mClientInfo);

      getGame()->removeBot(this);
      logprintf(LogConsumer::LogLuaObjectLifecycle, "Robot %s terminated (%d bots left)", mScriptName.c_str(), getGame()->getRobotCount());
   }

   delete mPlayerInfo;
   if(mClientInfo.isValid())
      delete mClientInfo.getPointer();

   // Even though a similar line gets called when parent classes are destructed, we need this here to set our very own personal copy
   // of luaProxy as defunct.  Each "level" of an object has their own private lauProxy object that needs to be individually marked.
   LUAW_DESTRUCTOR_CLEANUP;
}


// Reset everything on the robot back to the factory settings -- runs only when bot is spawning in GameType::spawnRobot()
// Only runs on server!
bool Robot::initialize(Point &pos)
{
   try
   {
      flightPlan.clear();

      mCurrentZone = U16_MAX;   // Correct value will be calculated upon first request

      Parent::initialize(pos);

      enableCollision();

      // WarpPositionMask triggers the spinny spawning visual effect
      setMaskBits(RespawnMask | HealthMask        | LoadoutMask         | PositionMask | 
                  MoveMask    | ModulePrimaryMask | ModuleSecondaryMask | WarpPositionMask);      // Send lots to the client

      TNLAssert(!isGhost(), "Didn't expect ghost here... this is supposed to only run on the server!");

      EventManager::get()->update();   // Ensure registrations made during bot initialization are ready to go
   }
   catch(LuaException &e)
   {
      logError("Robot error during spawn: %s.  Shutting robot down.", e.what());
      LuaBase::clearStack(L);
      return false;
   }

   return true;
} 


const char *Robot::getErrorMessagePrefix() { return "***ROBOT ERROR***"; }


// Server only
bool Robot::start()
{
   if(!runScript(!getGame()->isTestServer()))   // Load the script, execute the chunk to get it in memory, then run its main() function
      return false;

   // Pass true so that if this bot doesn't have a TickEvent handler, we don't print a message
   EventManager::get()->subscribe(this, EventManager::TickEvent, RobotContext, true);

   mSubscriptions[EventManager::TickEvent] = true;

   string name = runGetName();                                             // Run bot's getName function
   mClientInfo->setName(getGame()->makeUnique(name.c_str()).c_str());  // Make sure name is unique

   mHasSpawned = true;

   mGame->addToClientList(mClientInfo);

   return true;
}


bool Robot::prepareEnvironment()
{
   try
   {
      if(!LuaScriptRunner::prepareEnvironment())
         return false;

      // Push a pointer to this Robot to the Lua stack, then set the name of this pointer in the protected environment.  
      // This is the name that we'll use to refer to this robot from our Lua code.  

      if(!loadAndRunGlobalFunction(L, LUA_HELPER_FUNCTIONS_KEY, RobotContext))
         return false;

      // Set this first so we have this object available in the helper functions in case we need overrides
      setSelf(L, this, "bot");

      if(!loadAndRunGlobalFunction(L, ROBOT_HELPER_FUNCTIONS_KEY, RobotContext))
         return false;
   }
   catch(LuaException &e)
   {
      logError(e.what());
      LuaBase::clearStack(L);
      return false;
   }

   return true;
}


static string getNextName()
{
   static const string botNames[] = {
      "Addison", "Alexis", "Amelia", "Audrey", "Chloe", "Claire", "Elizabeth", "Ella",
      "Emily", "Emma", "Evelyn", "Gabriella", "Hailey", "Hannah", "Isabella", "Layla",
      "Lillian", "Lucy", "Madison", "Natalie", "Olivia", "Riley", "Samantha", "Zoe"
   };

   static U8 nameIndex = 0;
   return botNames[(nameIndex++) % ARRAYSIZE(botNames)];
}


// Run bot's getName function, return default name if fn isn't defined
string Robot::runGetName()
{
   TNLAssert(lua_gettop(L) == 0 || LuaBase::dumpStack(L), "Stack dirty!");

   // error will only be true if: 1) getName doesn't exist, which should never happen -- getName is stubbed out in robot_helper_functions.lua
   //                             2) getName generates an error
   //                             3) something is hopelessly corrupt (see 1)
   // Note that it is valid for getName to return a nil (or not be implemented in the bot itself) in which case a default name will be chosen.
   // If error is true, runCmd will terminate bot script by running killScript(), so we don't need to worry about making things too nice.
   bool error = runCmd("getName", 1);     

   string name = "";

   if(!error)
   {
      if(lua_isstring(L, -1))   // getName should have left a name on the stack
      {
         name = lua_tostring(L, -1);

         if(name == "")
            name = getNextName();
      }
      else
      {
         // If getName is not implemented, or returns nil, this is not an error; it just means we pick a name for the bot
         if(!lua_isnil(L, -1))
            logprintf(LogConsumer::LogWarning, "Robot error retrieving name (returned value was not a string).  Using \"%s\".", name.c_str());

         name = getNextName();
      }
   }

   clearStack(L);
   return name;
}


// This only runs the very first time the robot is added to the level
// Note that level may not yet be ready, so the bot can't spawn yet
// Runs on client and server 
void Robot::onAddedToGame(Game *game)
{
   if(!isGhost())    // Robots are created with NULL ClientInfos.  We'll add a valid one here.
   {
      TNLAssert(mClientInfo.isNull(), "mClientInfo should be NULL");

      mClientInfo = new FullClientInfo(game, NULL, "Robot", true);  // deleted in destructor
      mClientInfo->setShip(this);
      this->setOwner(mClientInfo);
   }

   Parent::onAddedToGame(game);
   
   if(isGhost())
      return;

   // Server only from here on out

   hasExploded = true;        // Becase we start off "dead", but will respawn real soon now...
   disableCollision();

   game->addBot(this);        // Add this robot to the list of all robots (can't do this in constructor or else it gets run on client side too...)
  
   EventManager::get()->fireEvent(this, EventManager::PlayerJoinedEvent, getPlayerInfo());
}


void Robot::killScript()
{
   deleteObject();
}


// Robot just died
void Robot::kill()
{
   if(hasExploded) 
      return;

   hasExploded = true;

   setMaskBits(ExplodedMask);
   if(!isGhost() && getOwner())
      getOwner()->saveActiveLoadout(mLoadout);

   disableCollision();

   dismountAll();
}


// Need this, as this may come from level or levelgen
bool Robot::processArguments(S32 argc, const char **argv, Game *game)
{
   string unused_String;
   return processArguments(argc, argv, game, unused_String); 
}


// Expect <team> <bot>
bool Robot::processArguments(S32 argc, const char **argv, Game *game, string &errorMessage)
{
   if(argc <= 1)
      setTeam(NO_TEAM);   
   else
   {
      S32 team = atoi(argv[0]);

      if(team == NO_TEAM || (team >= 0 && team < game->getTeamCount()))
         setTeam(atoi(argv[0]));
      else
      {
         errorMessage = "Invalid team specified";
         return false;
      }
   }
   

   string scriptName;

   if(argc >= 2)
      scriptName = argv[1];
   else
      scriptName = game->getSettings()->getIniSettings()->defaultRobotScript;

   FolderManager *folderManager = game->getSettings()->getFolderManager();

   if(scriptName != "")
      mScriptName = folderManager->findBotFile(scriptName);

   if(mScriptName == "")     // Bot script could not be located
   {
      errorMessage = "Could not find bot file " + scriptName;
      return false;
   }

   // Collect our arguments to be passed into the args table in the robot (starting with the robot name)
   // Need to make a copy or containerize argv[i] somehow, because otherwise new data will get written
   // to the string location subsequently, and our vals will change from under us.  That's bad!
   for(S32 i = 2; i < argc; i++)        // Does nothing if we have no args
      mScriptArgs.push_back(string(argv[i]));

   // I'm not sure this goes here, but it needs to be set early in setting up the Robot, but after
   // the constructor
   //
   // Our 'Game' pointer in LuaScriptRunner is the same as the one in this game object
   mLuaGame = game;
   mLuaGridDatabase = game->getGameObjDatabase();

   return true;
}


// Returns zone ID of current zone
S32 Robot::getCurrentZone()
{
   TNLAssert(getGame()->isServer(), "Not a ServerGame");

   // We're in uncharted territory -- try to get the current zone
   mCurrentZone = BotNavMeshZone::findZoneContaining(BotNavMeshZone::getBotZoneDatabase(), getActualPos());

   return mCurrentZone;
}


// Setter method, not a robot function!
void Robot::setCurrentZone(S32 zone)
{
   mCurrentZone = zone;
}


F32 Robot::getAnglePt(Point point)
{
   return getActualPos().angleTo(point);
}


bool Robot::canSeePoint(Point point, bool wallOnly)
{
   Point difference = point - getActualPos();

   Point crossVector(difference.y, -difference.x);  // Create a point whose vector from 0,0 is perpenticular to the original vector
   crossVector.normalize(mRadius);                  // reduce point so the vector has length of ship radius

   // Edge points of ship
   Point shipEdge1 = getActualPos() + crossVector;
   Point shipEdge2 = getActualPos() - crossVector;

   // Edge points of point
   Point pointEdge1 = point + crossVector;
   Point pointEdge2 = point - crossVector;

   Vector<Point> thisPoints;
   thisPoints.push_back(shipEdge1);
   thisPoints.push_back(shipEdge2);
   thisPoints.push_back(pointEdge2);
   thisPoints.push_back(pointEdge1);

   Rect queryRect(thisPoints);

   fillVector.clear();
   mGame->getGameObjDatabase()->findObjects(wallOnly ? (TestFunc)isWallType : (TestFunc)isCollideableType, fillVector, queryRect);

   for(S32 i = 0; i < fillVector.size(); i++)
   {
      const Vector<Point> *otherPoints = fillVector[i]->getCollisionPoly();
      if(otherPoints && polygonsIntersect(thisPoints, *otherPoints))
         return false;
   }

   return true;
}


void Robot::renderLayer(S32 layerIndex)
{
#ifndef ZAP_DEDICATED
   if(isGhost())                                         // Client rendering client's objects
      Parent::renderLayer(layerIndex);

   else if(layerIndex == 1 && flightPlan.size() != 0)    // Client hosting is rendering server objects
      renderFlightPlan(getActualPos(), flightPlan[0], flightPlan);
#endif
}


void Robot::idle(BfObject::IdleCallPath path)
{
   TNLAssert(path != BfObject::ServerProcessingUpdatesFromClient, "Should never idle with ServerProcessingUpdatesFromClient");

   if(hasExploded)
      return;

   if(path != BfObject::ServerIdleMainLoop)   
      Parent::idle(path);                       
   else                         
   {
      U32 deltaT = mCurrentMove.time;

      TNLAssert(deltaT != 0, "Time should never be zero!");    

      tickTimer<Robot>(deltaT);

      Parent::idle(BfObject::ServerProcessingUpdatesFromClient);   // Let's say the script is the client  ==> really not sure this is right
   }
}


// Clear out current move so that if none of the event handlers set the various move components, the bot will do nothing
void Robot::clearMove()
{
   mCurrentMove.fire = false;
   mCurrentMove.x = 0;
   mCurrentMove.y = 0;

   for(S32 i = 0; i < ShipModuleCount; i++)
   {
      mCurrentMove.modulePrimary[i] = false;
      mCurrentMove.moduleSecondary[i] = false;
   }
}


bool Robot::isRobot()
{
   return true;
}


LuaPlayerInfo *Robot::getPlayerInfo()
{
   return mPlayerInfo;
}


S32 Robot::getScore()
{
   return mScore;
}


F32 Robot::getRating()
{
   return mTotalScore == 0 ? 0.5f : (F32)mScore / (F32)mTotalScore;
}


const char *Robot::getScriptName()
{
   return mScriptName.c_str();
}


Robot *Robot::clone() const
{
   return new Robot(*this);
}


// Another helper function: returns id of closest zone to a given point
U16 Robot::findClosestZone(const Point &point)
{
   U16 closestZone = U16_MAX;

   // First, do a quick search for zone based on the buffer; should be 99% of the cases

   // Search radius is just slightly larger than twice the zone buffers added to objects like barriers
   S32 searchRadius = 2 * BotNavMeshZone::BufferRadius + 1;

   Vector<DatabaseObject*> objects;
   Rect rect = Rect(point.x + searchRadius, point.y + searchRadius, point.x - searchRadius, point.y - searchRadius);

   BotNavMeshZone::getBotZoneDatabase()->findObjects(BotNavMeshZoneTypeNumber, objects, rect);

   for(S32 i = 0; i < objects.size(); i++)
   {
      BotNavMeshZone *zone = static_cast<BotNavMeshZone *>(objects[i]);
      Point center = zone->getCenter();

      if(getGame()->getGameObjDatabase()->pointCanSeePoint(center, point))  // This is an expensive test
      {
         closestZone = zone->getZoneId();
         break;
      }
   }

   // Target must be outside extents of the map, find nearest zone if a straight line was drawn
   if(closestZone == U16_MAX)
   {
      Point extentsCenter = getGame()->getWorldExtents()->getCenter();

      F32 collisionTimeIgnore;
      Point surfaceNormalIgnore;

      DatabaseObject* object = BotNavMeshZone::getBotZoneDatabase()->findObjectLOS(BotNavMeshZoneTypeNumber,
            ActualState, point, extentsCenter, collisionTimeIgnore, surfaceNormalIgnore);

      BotNavMeshZone *zone = static_cast<BotNavMeshZone *>(object);

      if (zone != NULL)
         closestZone = zone->getZoneId();
   }

   return closestZone;
}

//// Lua methods

//                Fn name               Param profiles                  Profile count                           
#define LUA_METHODS(CLASS, METHOD) \
   METHOD(CLASS,  setAngle,             ARRAYDEF({{ PT, END }, { NUM, END }}), 2 )           \
   METHOD(CLASS,  getAnglePt,           ARRAYDEF({{ PT, END }              }), 1 )           \
   METHOD(CLASS,  hasLosPt,             ARRAYDEF({{ PT, END }              }), 1 )           \
                                                                                             \
   METHOD(CLASS,  getWaypoint,          ARRAYDEF({{ PT, END }}), 1 )                         \
                                                                                             \
   METHOD(CLASS,  setThrust,            ARRAYDEF({{ NUM, NUM, END }, { NUM, PT, END}}), 2 )  \
   METHOD(CLASS,  setThrustToPt,        ARRAYDEF({{ PT,       END }                 }), 1 )  \
                                                                                             \
   METHOD(CLASS,  fire,                 ARRAYDEF({{            END }}), 1 )                  \
   METHOD(CLASS,  setWeapon,            ARRAYDEF({{ WEAP_ENUM, END }}), 1 )                  \
   METHOD(CLASS,  setWeaponIndex,       ARRAYDEF({{ WEAP_SLOT, END }}), 1 )                  \
   METHOD(CLASS,  hasWeapon,            ARRAYDEF({{ WEAP_ENUM, END }}), 1 )                  \
                                                                                             \
   METHOD(CLASS,  activateModule,       ARRAYDEF({{ MOD_ENUM, END }}), 1 )                   \
   METHOD(CLASS,  activateModuleIndex,  ARRAYDEF({{ MOD_SLOT, END }}), 1 )                   \
                                                                                             \
   METHOD(CLASS,  globalMsg,            ARRAYDEF({{ STR, END }}), 1 )                        \
   METHOD(CLASS,  teamMsg,              ARRAYDEF({{ STR, END }}), 1 )                        \
   METHOD(CLASS,  privateMsg,           ARRAYDEF({{ STR, STR, END }}), 1 )                   \
                                                                                             \
   METHOD(CLASS,  findObjects,          ARRAYDEF({{ TABLE, INTS, END }, { INTS, END }}), 2 ) \
   METHOD(CLASS,  findGlobalObjects,    ARRAYDEF({{ TABLE, INTS, END }, { INTS, END }}), 2 ) \
   METHOD(CLASS,  findClosestEnemy,     ARRAYDEF({{              END }, { NUM,  END }}), 2 ) \
                                                                                             \
   METHOD(CLASS,  getFiringSolution,    ARRAYDEF({{ BFOBJ, END }}), 1 )                      \
   METHOD(CLASS,  getInterceptCourse,   ARRAYDEF({{ BFOBJ, END }}), 1 )                      \
                                                                                             \
   METHOD(CLASS,  engineerDeployObject, ARRAYDEF({{ INT,    END }}), 1 )                     \
   METHOD(CLASS,  dropItem,             ARRAYDEF({{         END }}), 1 )                     \
   METHOD(CLASS,  copyMoveFromObject,   ARRAYDEF({{ MOVOBJ, END }}), 1 )                     \


GENERATE_LUA_METHODS_TABLE(Robot, LUA_METHODS);
GENERATE_LUA_FUNARGS_TABLE(Robot, LUA_METHODS);

#undef LUA_METHODS


const char *Robot::luaClassName = "Robot";
REGISTER_LUA_SUBCLASS(Robot, Ship);


// Turn to angle a (in radians, or toward a point)
S32 Robot::lua_setAngle(lua_State *L)
{
   S32 profile = checkArgList(L, functionArgs, "Robot", "setAngle");

   Move move = getCurrentMove();

   if(profile == 0)        // Args: PT    ==> Aim towards point
   {
      Point point = getPointOrXY(L, 1);
      move.angle = getAnglePt(point);
   }

   else if(profile == 1)   // Args: NUM   ==> Aim at this angle (radians)
      move.angle = getFloat(L, 1);

   setCurrentMove(move);
   return 0;
}


// Get angle toward point
S32 Robot::lua_getAnglePt(lua_State *L)
{
   checkArgList(L, functionArgs, "Robot", "getAnglePt");

   Point point = getPointOrXY(L, 1);

   return returnFloat(L, getAnglePt(point));
}


// Can robot see point P?
S32 Robot::lua_hasLosPt(lua_State *L)
{
   checkArgList(L, functionArgs, "Robot", "hasLosPt");

   Point point = getPointOrXY(L, 1);

   return returnBool(L, canSeePoint(point));
}


// Get next waypoint to head toward when traveling from current location to x,y
// Note that this function will be called frequently by various robots, so any
// optimizations will be helpful.
S32 Robot::lua_getWaypoint(lua_State *L)
{
   TNLAssert(getGame()->isServer(), "Not a ServerGame");

   checkArgList(L, functionArgs, "Robot", "getWaypoint");

   Point target = getPointOrXY(L, 1);

   // If we can see the target, go there directly
   if(canSeePoint(target, true))
   {
      flightPlan.clear();
      return returnPoint(L, target);
   }

   // TODO: cache destination point; if it hasn't moved, then skip ahead.

   U16 targetZone = BotNavMeshZone::findZoneContaining(BotNavMeshZone::getBotZoneDatabase(), target); // Where we're going  ===> returns zone id

   if(targetZone == U16_MAX)       // Our target is off the map.  See if it's visible from any of our zones, and, if so, go there
   {
      targetZone = findClosestZone(target);

      if(targetZone == U16_MAX)
         return returnNil(L);
   }

   // Make sure target is still in the same zone it was in when we created our flightplan.
   // If we're not, our flightplan is invalid, and we need to skip forward and build a fresh one.
   if(flightPlan.size() > 0 && targetZone == flightPlanTo)
   {
      // In case our target has moved, replace final point of our flightplan with the current target location
      flightPlan[0] = target;

      // First, let's scan through our pre-calculated waypoints and see if we can see any of them.
      // If so, we'll just head there with no further rigamarole.  Remember that our flightplan is
      // arranged so the closest points are at the end of the list, and the target is at index 0.
      Point dest;
      bool found = false;
//      bool first = true;

      while(flightPlan.size() > 0)
      {
         Point last = flightPlan.last();

         // We'll assume that if we could see the point on the previous turn, we can
         // still see it, even though in some cases, the turning of the ship around a
         // protruding corner may make it technically not visible.  This will prevent
         // rapidfire recalcuation of the path when it's not really necessary.

         // removed if(first) ... Problems with Robot get stuck after pushed from burst or mines.
         // To save calculations, might want to avoid (canSeePoint(last))
         if(canSeePoint(last, true))
         {
            dest = last;
            found = true;
//            first = false;
            flightPlan.pop_back();   // Discard now possibly superfluous waypoint
         }
         else
            break;
      }

      // If we found one, that means we found a visible waypoint, and we can head there...
      if(found)
      {
         flightPlan.push_back(dest);    // Put dest back at the end of the flightplan
         return returnPoint(L, dest);
      }
   }

   // We need to calculate a new flightplan
   flightPlan.clear();

   U16 currentZone = getCurrentZone();     // Zone we're in

   if(currentZone == U16_MAX)      // We don't really know where we are... bad news!  Let's find closest visible zone and go that way.
      currentZone = findClosestZone(getActualPos());

   if(currentZone == U16_MAX)      // That didn't go so well...
      return returnNil(L);

   // We're in, or on the cusp of, the zone containing our target.  We're close!!
   if(currentZone == targetZone)
   {
      Point p;
      flightPlan.push_back(target);

      if(!canSeePoint(target, true))           // Possible, if we're just on a boundary, and a protrusion's blocking a ship edge
      {
         BotNavMeshZone *zone = static_cast<BotNavMeshZone *>(BotNavMeshZone::getBotZoneDatabase()->getObjectByIndex(targetZone));

         p = zone->getCenter();
         flightPlan.push_back(p);
      }
      else
         p = target;

      return returnPoint(L, p);
   }

   // If we're still here, then we need to find a new path.  Either our original path was invalid for some reason,
   // or the path we had no longer applied to our current location
   flightPlanTo = targetZone;

   // check cache for path first
   pair<S32,S32> pathIndex = pair<S32,S32>(currentZone, targetZone);

   const Vector<BotNavMeshZone *> *zones = BotNavMeshZone::getBotZones();      // Grab our pre-cached list of nav zones

   if(getGame()->getGameType()->cachedBotFlightPlans.find(pathIndex) == getGame()->getGameType()->cachedBotFlightPlans.end())
   {
      // Not found so calculate flight plan
      flightPlan = AStar::findPath(zones, currentZone, targetZone, target);

      // Add to cache
      getGame()->getGameType()->cachedBotFlightPlans[pathIndex] = flightPlan;
   }
   else
      flightPlan = getGame()->getGameType()->cachedBotFlightPlans[pathIndex];

   if(flightPlan.size() > 0)
      return returnPoint(L, flightPlan.last());
   else
      return returnNil(L);    // Out of options, end of the road
}


/**
  * @luafunc Robot::findClosestEnemy(range)
  * @brief   Finds the closest enemy ship or robot that is within the specified distance.
  * @descr   Finds closest enemy within specified distance of the bot.  If dist is omitted, this will use standard 
  *          scanner range, taking into account whether the bot has the Sensor module.  To search the entire map,
  *          specify -1 for the range.
  * @param   range - (Optional) Radius in which to search.  Use -1 to search entire map.  If omitted, will use normal scanner range.
  * @return  Ship object representing closest enemy, or nil if none were found.
  */
S32 Robot::lua_findClosestEnemy(lua_State *L)
{
   S32 profile = checkArgList(L, functionArgs, "Robot", "findClosestEnemy");

   Point pos = getActualPos();
   Rect queryRect(pos, pos);
   bool useRange = true;

   if(profile == 0)           // Args: None
      queryRect.expand(getGame()->computePlayerVisArea(this));  
   else                       // Args: Range
   {
      F32 range = getFloat(L, 1);
      if(range == -1)
         useRange = false;
        else
         queryRect.expand(Point(range, range));
   }


   F32 minDist = F32_MAX;
   Ship *closest = NULL;

   fillVector.clear();

   if(useRange)
      getGame()->getGameObjDatabase()->findObjects((TestFunc)isShipType, fillVector, queryRect);   
   else
      getGame()->getGameObjDatabase()->findObjects((TestFunc)isShipType, fillVector);   

   for(S32 i = 0; i < fillVector.size(); i++)
   {
      // Ignore self 
      if(fillVector[i] == this) 
         continue;

      // Ignore ship/robot if it's dead or cloaked
      Ship *ship = static_cast<Ship *>(fillVector[i]);
      if(ship->hasExploded || !ship->isVisible(hasModule(ModuleSensor)))
         continue;

      // Ignore ships on same team during team games
      if(ship->getTeam() == getTeam() && getGame()->getGameType()->isTeamGame())
         continue;

      F32 dist = ship->getActualPos().distSquared(getActualPos());
      if(dist < minDist)
      {
         minDist = dist;
         closest = ship;
      }
   }

   return returnShip(L, closest);    // Handles closest == NULL
}


// Thrust at velocity v toward angle a
S32 Robot::lua_setThrust(lua_State *L)
{
   S32 profile = checkArgList(L, functionArgs, "Robot", "setThrust");

   F32 ang;
   F32 vel = getFloat(L, 1);

   if(profile == 0)           // Args: NUM, NUM  (speed, angle)
      ang = getFloat(L, 2);

   else if(profile == 1)      // Args: NUM, PT   (speed, destination)
   {
      Point point = getPointOrXY(L, 2);

      ang = getAnglePt(point) - 0 * FloatHalfPi;
   }

   Move move = getCurrentMove();

   move.x = vel * cos(ang);
   move.y = vel * sin(ang);

   setCurrentMove(move);

   return 0;
}


// Thrust toward specified point, but slow speed so that we land directly on that point if it is within range
S32 Robot::lua_setThrustToPt(lua_State *L)
{
   checkArgList(L, functionArgs, "Robot", "setThrustToPt");

   Point point = getPointOrXY(L, 1);

   F32 ang = getAnglePt(point) - 0 * FloatHalfPi;

   Move move = getCurrentMove();

   F32 dist = getActualPos().distanceTo(point);

   F32 vel = dist / ((F32) move.time);      // v = d / t, t is in ms

   if(vel > 1.f)
      vel = 1.f;

   move.x = vel * cos(ang);
   move.y = vel * sin(ang);

   setCurrentMove(move);

  return 0;
}


// Fire current weapon if possible
S32 Robot::lua_fire(lua_State *L)
{
   Move move = getCurrentMove();
   move.fire = true;
   setCurrentMove(move);

   return 0;
}


// Set weapon to specified weapon, if we have it
S32 Robot::lua_setWeapon(lua_State *L)
{
   checkArgList(L, functionArgs, "Robot", "setWeapon");

   WeaponType weap = (WeaponType)getInt(L, 1);

   // Check the weapons we have on board -- if any match the requested weapon, activate it
   for(S32 i = 0; i < ShipWeaponCount; i++)
      if(mLoadout.getWeapon(i) == weap)
      {
         selectWeapon(i);
         break;
      }

   // If we get here without having found our weapon, then nothing happens.  Better luck next time!
   return 0;
}


// Set weapon to index of slot (i.e. 1, 2, or 3)
S32 Robot::lua_setWeaponIndex(lua_State *L)
{
   checkArgList(L, functionArgs, "Robot", "setWeaponIndex");

   U32 weap = (U32)getInt(L, 1); // Acceptable range = (1, ShipWeaponCount) -- has already been verified by checkArgList()
   selectWeapon(weap - 1);       // Correct for the fact that index in C++ is 0 based

   return 0;
}


// Do we have a given weapon in our current loadout?
S32 Robot::lua_hasWeapon(lua_State *L)
{
   checkArgList(L, functionArgs, "Robot", "hasWeapon");
   WeaponType weap = (WeaponType)getInt(L, 1);

   for(S32 i = 0; i < ShipWeaponCount; i++)
      if(mLoadout.getWeapon(i) == weap)
         return returnBool(L, true);      // We have it!

   return returnBool(L, false);           // We don't!
}


// Activate module this cycle --> takes module enum.
// If specified module is not part of the loadout, does nothing.
S32 Robot::lua_activateModule(lua_State *L)
{
   checkArgList(L, functionArgs, "Robot", "activateModule");

   ShipModule mod = (ShipModule) getInt(L, 1);

   for(S32 i = 0; i < ShipModuleCount; i++)
      if(getModule(i) == mod)
      {
         mCurrentMove.modulePrimary[i] = true;
         break;
      }

   return 0;
}


// Activate module this cycle --> takes module index
S32 Robot::lua_activateModuleIndex(lua_State *L)
{
   checkArgList(L, functionArgs, "Robot", "activateModuleIndex");

   U32 indx = (U32)getInt(L, 1);

   mCurrentMove.modulePrimary[indx] = true;

   return 0;
}


/**
 * @luafunc Robot::globalMsg(string message)
 * @brief   Send a message to all players.
 * @param   message Message to send.
 */
S32 Robot::lua_globalMsg(lua_State *L)
{
   checkArgList(L, functionArgs, luaClassName, "globalMsg");

   const char *message = getString(L, 1);

   GameType *gt = getGame()->getGameType();
   if(gt)
   {
      gt->sendChat(mClientInfo->getName(), mClientInfo, message, true, mClientInfo->getTeamIndex());

      // Fire our event handler
      EventManager::get()->fireEvent(this, EventManager::MsgReceivedEvent, message, getPlayerInfo(), true);
   }

   return 0;
}


// Send message to team (what happens when neutral/hostile robot does this???)
/**
 * @luafunc Robot::teamMsg(string message)
 * @brief   Send a message to this Robot's team.
 * @param   message Message to send.
 */
S32 Robot::lua_teamMsg(lua_State *L)
{
   checkArgList(L, functionArgs, luaClassName, "teamMsg");

   const char *message = getString(L, 1);

   GameType *gt = getGame()->getGameType();
   if(gt)
   {
      gt->sendChat(mClientInfo->getName(), mClientInfo, message, false, mClientInfo->getTeamIndex());

      // Fire our event handler
      EventManager::get()->fireEvent(this, EventManager::MsgReceivedEvent, message, getPlayerInfo(), false);
   }

   return 0;
}


/**
 * @luafunc Robot::privateMsg(string message, string playerName)
 * @brief   Send a private message to a player.
 * @param   message Message to send.
 * @param   playerName Name of player to which to send a message.
 */
S32 Robot::lua_privateMsg(lua_State *L)
{
   checkArgList(L, functionArgs, luaClassName, "privateMsg");

   const char *message = getString(L, 1);
   const char *playerName = getString(L, 2);

   mGame->sendPrivateChat(mClientInfo->getName(), playerName, message);

   // No event fired for private message

   return 0;
}


/**
  *   @luafunc Robot::findObjects(table, itemType, ...)
  *   @brief   Finds all items of the specified type within ship's area of vision.
  *   @descr   Can specify multiple types.  The \e table argument is optional, but bots that call this function frequently will perform
  *            better if they provide a reusable table in which found objects can be stored.  By providing a table, you will avoid
  *            incurring the overhead of construction and destruction of a new one.
  *
  *   If a table is not provided, the function will create a table and return it on the stack.
  *
  *   <i>Note that although this function is part of the Robot object, it can (and should) be called without a direct bot: reference.</i>  
  *   See the example below.
  *
  *   @param  table - (Optional) Reusable table into which results can be written.
  *   @param  itemType - One or more itemTypes specifying what types of objects to find.
  *   @return resultsTable - Will either be a reference back to the passed \e table, or a new table if one was not provided.
  *
  *   @code items = { }     -- Reusable container for findGlobalObjects.  Because it is defined outside
  *                         -- any functions, it will have global scope.
  *
  *         function countObjects(objType, ...)   -- Pass one or more object types
  *           table.clear(items)                  -- Remove any items in table from previous use
  *           findObjects(items, objType, ...)    -- Put all items of specified type(s) into items table, no bot reference
  *           print(#items)                       -- Print the number of items found to the console
  *         end
  */
S32 Robot::lua_findObjects(lua_State *L)
{
   checkArgList(L, functionArgs, "Robot", "findObjects");

   Point pos = getActualPos();
   Rect queryRect(pos, pos);
   queryRect.expand(getGame()->computePlayerVisArea(this));  

   return LuaScriptRunner::findObjects(L, getGame()->getGameObjDatabase(), &queryRect, this);
}


/**
  *   @luafunc Robot::findGlobalObjects(table, itemType, ...)
  *   @brief   Finds all items of the specified type anywhere on the level.
  *   @descr   Can specify multiple types.  The \e table argument is optional, but bots that call this function frequently will perform
  *            better if they provide a reusable table in which found objects can be stored.  By providing a table, you will avoid
  *            incurring the overhead of construction and destruction of a new one.
  *
  *   If a table is not provided, the function will create a table and return it on the stack.
  *
  *   <i>Note that although this function is part of the Robot object, it can (and should) be called without a direct bot: reference.</i>  
  *   See the example below.
  *
  *   @param  table - (Optional) Reusable table into which results can be written.
  *   @param  itemType - One or more itemTypes specifying what types of objects to find.
  *   @return resultsTable - Will either be a reference back to the passed \e table, or a new table if one was not provided.
  *
  *   @code items = { }     -- Reusable container for findGlobalObjects.  Because it is defined outside
  *                         -- any functions, it will have global scope.
  *
  *         function countObjects(objType, ...)       -- Pass one or more object types
  *           table.clear(items)                      -- Remove any items in table from previous use
  *           findGlobalObjects(items, objType, ...)  -- Put all items of specified type(s) into items table, no bot reference
  *           print(#items)                           -- Print the number of items found to the console
  *         end
  */
S32 Robot::lua_findGlobalObjects(lua_State *L)
{
   checkArgList(L, functionArgs, "Robot", "findGlobalObjects");

   return LuaScriptRunner::findObjects(L, getGame()->getGameObjDatabase(), NULL, this);
}


static bool calcInterceptCourse(BfObject *target, Point aimPos, F32 aimRadius, S32 aimTeam, F32 aimVel, 
                                F32 aimLife, bool ignoreFriendly, bool botHasSensor, F32 &interceptAngle)
{
   Point offset = target->getPos() - aimPos;    // Account for fact that robot doesn't fire from center
   offset.normalize(aimRadius * 1.2f);          // 1.2 ==> fudge factor to prevent robot from not shooting because it thinks it will hit itself
   aimPos += offset;

   bool targetIsShip = isShipType(target->getObjectTypeNumber());

   if(targetIsShip)
   {
      Ship *potential = static_cast<Ship *>(target);

      // Is it dead or cloaked?  If so, ignore
      if(!potential->isVisible(botHasSensor) || potential->hasExploded)
         return false;
   }

   if(ignoreFriendly && target->getTeam() == aimTeam)      // Is target on our team?
      return false;                                        // ...if so, skip it!

   // Calculate where we have to shoot to hit this...
   Point Vs = target->getVel();

   Point d = target->getPos() - aimPos;

   F32 t;      // t is set in next statement
   if(!findLowestRootInInterval(Vs.dot(Vs) - aimVel * aimVel, 2 * Vs.dot(d), d.dot(d), aimLife * 0.001f, t))
      return false;

   Point leadPos = target->getPos() + Vs * t;

   // Calculate distance
   Point delta = (leadPos - aimPos);

   // Make sure we can see it...
   Point n;

   DatabaseObject* objectInTheWay = target->findObjectLOS(isFlagCollideableType, ActualState, aimPos, target->getPos(), t, n);

   if(objectInTheWay && objectInTheWay != target)
      return false;

   // See if we're gonna clobber our own stuff...
   target->disableCollision();

   Point delta2 = delta;
   delta2.normalize(aimLife * aimVel / 1000);

   BfObject *hitObject = target->findObjectLOS((TestFunc)isWithHealthType, 0, aimPos, aimPos + delta2, t, n);
   target->enableCollision();

   if(ignoreFriendly && hitObject && hitObject->getTeam() == aimTeam)
      return false;

   interceptAngle = delta.ATAN2();

   return true;
}


// Given an object, which angle do we need to be at to fire to hit it?
// Returns nil if a workable solution can't be found
// Logic adapted from turret aiming algorithm
// Note that bot WILL fire at teammates if you ask it to!
S32 Robot::lua_getFiringSolution(lua_State *L)
{
   checkArgList(L, functionArgs, "Robot", "getFiringSolution");

   BfObject *target = luaW_check<BfObject>(L, 1);

   WeaponInfo weap = WeaponInfo::getWeaponInfo(mLoadout.getActiveWeapon());    // Robot's active weapon

   F32 interceptAngle;

   if(calcInterceptCourse(target, getActualPos(), getRadius(), getTeam(), (F32)weap.projVelocity, 
                          (F32)weap.projLiveTime, false, hasModule(ModuleSensor), interceptAngle))
      return returnFloat(L, interceptAngle);

   return returnNil(L);
}


// Given an object, what angle do we need to fly toward in order to collide with an object?  This
// works a lot like getFiringSolution().
S32 Robot::lua_getInterceptCourse(lua_State *L)
{
   checkArgList(L, functionArgs, "Robot", "getInterceptCourse");

   BfObject *target = luaW_check<BfObject>(L, 1);

   F32 interceptAngle;     // <== will be set by calcInterceptCourse() below

   if(calcInterceptCourse(target, getActualPos(), getRadius(), getTeam(), 256, 3000, false, hasModule(ModuleSensor), interceptAngle))
      return returnFloat(L, interceptAngle);
      
   return returnNil(L);
}


S32 Robot::lua_engineerDeployObject(lua_State *L)
{
   checkArgList(L, functionArgs, "Robot", "engineerDeployObject");

   S32 type = (S32)lua_tointeger(L, 0);

   return returnBool(L, getOwner()->sEngineerDeployObject(type));
}


S32 Robot::lua_dropItem(lua_State *L)
{
   checkArgList(L, functionArgs, "Robot", "dropItem");

   S32 count = mMountedItems.size();
   for(S32 i = count - 1; i >= 0; i--)
      mMountedItems[i]->dismount(DISMOUNT_NORMAL);

   return 0;
}


S32 Robot::lua_copyMoveFromObject(lua_State *L)
{
   checkArgList(L, functionArgs, "Robot", "copyMoveFromObject");

   MoveObject *obj = luaW_check<MoveObject>(L, 1);

   Move move = obj->getCurrentMove();
   move.time = getCurrentMove().time;     // Keep current move time
   setCurrentMove(move);

   return 0;
}


};
