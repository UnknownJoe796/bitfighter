//------------------------------------------------------------------------------
// Copyright Chris Eykamp
// See LICENSE.txt for full copyright information
//------------------------------------------------------------------------------

#include "Cursor.h"
#include "DisplayManager.h"

#include "tnlAssert.h"
#include "tnlPlatform.h"

#ifdef TNL_OS_WIN32 
#  include <windows.h>        // For ARRAYSIZE def
#endif

#include "SDL_mouse.h"     
#include "SDL_version.h"

namespace Zap
{

// Declare some statics
static bool mInitialized = false;      // Prevent double initialization
static SDL_Cursor *mDefault = NULL;
static SDL_Cursor *mSpray = NULL;
static SDL_Cursor *mVerticalResize = NULL;

////////////////////////////////////////
////////////////////////////////////////
// Cursors embedded below.  Note that image data comes from Gimp -- create the image, save as "xbm" type, and copy the structs as done below.  
// Be sure to include the mask file.
// The only modification is to add the two hotspot bytes.  Images can be full color, with transparency.
static Cursor cursorSpray = {
   32, 32, 16, 16,

   // Regular bits
   {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00,
      0x00, 0x20, 0x02, 0x00, 0x00, 0x10, 0x04, 0x00, 0x00, 0x08, 0x08, 0x00,
      0x00, 0x78, 0x0f, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x43, 0x61, 0x00,
      0x80, 0x42, 0xa1, 0x00, 0x40, 0x42, 0x21, 0x01, 0x00, 0x7e, 0x3f, 0x02,
      0x10, 0x00, 0x00, 0x04, 0x20, 0x7e, 0x3f, 0x02, 0x40, 0x42, 0x21, 0x01,
      0x80, 0x42, 0xa1, 0x00, 0x00, 0x43, 0x61, 0x00, 0x00, 0x40, 0x01, 0x00,
      0x00, 0x78, 0x0f, 0x00, 0x00, 0x08, 0x08, 0x00, 0x00, 0x10, 0x04, 0x00,
      0x00, 0x20, 0x02, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x80, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },

   // Mask bits
   {  
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xc0, 0x01, 0x00,
      0x00, 0xe0, 0x03, 0x00, 0x00, 0xf0, 0x07, 0x00, 0x00, 0xf8, 0x0f, 0x00,
      0x00, 0xf8, 0x0f, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0xc3, 0x61, 0x00,
      0x80, 0xc3, 0xe1, 0x00, 0xc0, 0xc3, 0xe1, 0x01, 0xc0, 0xff, 0xff, 0x03,
      0xf0, 0xff, 0xff, 0x07, 0xe0, 0xff, 0xff, 0x03, 0xc0, 0xc3, 0xe1, 0x01,
      0x80, 0xc3, 0xe1, 0x00, 0x00, 0xc3, 0x61, 0x00, 0x00, 0xc0, 0x01, 0x00,
      0x00, 0xf8, 0x0f, 0x00, 0x00, 0xf8, 0x0f, 0x00, 0x00, 0xf0, 0x07, 0x00,
      0x00, 0xe0, 0x03, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0x80, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
};


static Cursor cursorVerticalResize = {
   32, 32, 16, 16,
   {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00,
      0x00, 0x20, 0x02, 0x00, 0x00, 0x10, 0x04, 0x00, 0x00, 0x08, 0x08, 0x00,
      0x00, 0x78, 0x0f, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x40, 0x01, 0x00,
      0x00, 0x40, 0x01, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x40, 0x01, 0x00,
      0x00, 0x40, 0x01, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x40, 0x01, 0x00,
      0x00, 0x40, 0x01, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x40, 0x01, 0x00,
      0x00, 0x78, 0x0f, 0x00, 0x00, 0x08, 0x08, 0x00, 0x00, 0x10, 0x04, 0x00,
      0x00, 0x20, 0x02, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x80, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },

   {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xc0, 0x01, 0x00,
      0x00, 0xe0, 0x03, 0x00, 0x00, 0xf0, 0x07, 0x00, 0x00, 0xf8, 0x0f, 0x00,
      0x00, 0xf8, 0x0f, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0xc0, 0x01, 0x00,
      0x00, 0xc0, 0x01, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0xc0, 0x01, 0x00,
      0x00, 0xc0, 0x01, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0xc0, 0x01, 0x00,
      0x00, 0xc0, 0x01, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0xc0, 0x01, 0x00,
      0x00, 0xf8, 0x0f, 0x00, 0x00, 0xf8, 0x0f, 0x00, 0x00, 0xf0, 0x07, 0x00,
      0x00, 0xe0, 0x03, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0x80, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
};


static Cursor cursorDefault = {
   32, 32, 0, 0,

   // Regular bits
   {
      0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
      0x11, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00,
      0x81, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,
      0x01, 0x04, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x81, 0x0f, 0x00, 0x00,
      0x91, 0x00, 0x00, 0x00, 0x29, 0x01, 0x00, 0x00, 0x25, 0x01, 0x00, 0x00,
      0x43, 0x02, 0x00, 0x00, 0x40, 0x02, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },

   // Mask bits
   {  
      0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
      0x1f, 0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x00,
      0xff, 0x00, 0x00, 0x00, 0xff, 0x01, 0x00, 0x00, 0xff, 0x03, 0x00, 0x00,
      0xff, 0x07, 0x00, 0x00, 0xff, 0x0f, 0x00, 0x00, 0xff, 0x0f, 0x00, 0x00,
      0xff, 0x00, 0x00, 0x00, 0xef, 0x01, 0x00, 0x00, 0xe7, 0x01, 0x00, 0x00,
      0xc3, 0x03, 0x00, 0x00, 0xc0, 0x03, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
};


SDL_Cursor *Cursor::toSDL()
{
   reverseBits();
   return SDL_CreateCursor(bits, maskBits, width, height, hotX, hotY);
}


void Cursor::init()
{
   TNLAssert(!mInitialized, "Don't initialize me twice!");

   mDefault = cursorDefault.toSDL();
   mSpray = cursorSpray.toSDL();
   mVerticalResize = cursorVerticalResize.toSDL();

   SDL_SetCursor(mDefault);

   mInitialized = true;
}


void Cursor::reverseBits() 
{
   // We need to reverse the bits encoded in our cursor; We're assuming the data comes from Gimp, which writes data
   // with a different bit order than SDL expects.  Don't know who is right, but this needs to be done.
   // Algorithm from http://graphics.stanford.edu/~seander/bithacks.html#BitReverseObvious

   TNLAssert(ARRAYSIZE(bits) == ARRAYSIZE(maskBits), "Mask is not the same size as the bits!");

   for(U32 i = 0; i < ARRAYSIZE(bits); i++)
   {
      bits[i] = U8(((bits[i] * 0x0802LU & 0x22110LU) | (bits[i] * 0x8020LU & 0x88440LU)) * 0x10101LU >> 16);    
      maskBits[i] = U8(((maskBits[i] * 0x0802LU & 0x22110LU) | (maskBits[i] * 0x8020LU & 0x88440LU)) * 0x10101LU >> 16);    
   }
}


void Cursor::enableCursor()
{
   // Untrap mouse
   SDL_SetWindowGrab(DisplayManager::getScreenInfo()->sdlWindow, SDL_FALSE);

   SDL_ShowCursor(1);
}


void Cursor::disableCursor()
{
   SDL_ShowCursor(0);

   // Trap mouse
   SDL_SetWindowGrab(DisplayManager::getScreenInfo()->sdlWindow, SDL_TRUE);
}


////////////////////////////////////////
////////////////////////////////////////

SDL_Cursor *Cursor::getSpray()
{
   return mSpray;
}


SDL_Cursor *Cursor::getVerticalResize()
{
   return mVerticalResize;
}


SDL_Cursor *Cursor::getDefault()
{
   return mDefault;
}


}
