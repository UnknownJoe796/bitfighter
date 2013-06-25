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

#ifndef _A_TO_B_SCROLLER_H_
#define _A_TO_B_SCROLLER_H_


#include "Timer.h"
#include "ConfigEnum.h" // For DisplayMode def


using namespace TNL;


namespace Zap { 
   
class ClientGame;

namespace UI {




// Class for producing a scrolling transition between two objects (A and B).  Used, for example, to help transition
// between Modules and Weapons on the Loadout menu.
class AToBScroller
{
protected:
   Timer mScrollTimer;
   S32 getTransitionPos(S32 fromPos, S32 toPos) const;
   bool isActive() const;

   static const S32 NO_RENDER = S32_MAX;

   // These will return the top render position, or NO_RENDER if rendering can be skipped
   S32 prepareToRenderFromDisplay(DisplayMode displayMode, S32 top, S32 fromHeight, S32 toHeight = S32_MIN) const;
   S32 prepareToRenderToDisplay  (DisplayMode displayMode, S32 top, S32 fromHeight, S32 toHeight = S32_MIN) const;
   void doneRendering() const;

public:
   AToBScroller();            // Constructor
   virtual ~AToBScroller();   // Destructor

   virtual void onActivated();
   virtual void idle(U32 deltaT);

   void resetScrollTimer();
   void clearScrollTimer();

};

} } // Nested namespace


#endif
