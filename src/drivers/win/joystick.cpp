/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2002 Xodnizel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "common.h"
#include "dinput.h"

#include "input.h"
#include "joystick.h"


#define MAX_JOYSTICKS   32
static LPDIRECTINPUTDEVICE7 Joysticks[MAX_JOYSTICKS]={0};
static GUID JoyGUID[MAX_JOYSTICKS];

static int numjoysticks = 0;
static int HavePolled[MAX_JOYSTICKS];

static int background = 0;
static DIJOYSTATE2 StatusSave[MAX_JOYSTICKS];
static DIJOYSTATE2 StatusPrevious[MAX_JOYSTICKS];
static int StatusValid[MAX_JOYSTICKS];
static int PreviousValid[MAX_JOYSTICKS];

static int FindByGUID(GUID how)
{
 int x;

 for(x=0; x<numjoysticks; x++)
  if(!memcmp(&JoyGUID[x], &how, sizeof(GUID)))
   return(x);

 return(0xFF);
}

#define FPOV_CENTER     16

static int POVFix(long pov, int roundpos)
{
 long lowpov;

 if(LOWORD(pov) == 65535)
  return(FPOV_CENTER);

 if(roundpos == -1)  /* Special case for button configuration */
 {
  pov /= 4500;
  pov = (pov >> 1) + (pov & 1);
  pov &= 3;
  return(pov);
 }

 lowpov = pov % 9000;
 if(lowpov < (4500 - 4500 / 2))
 {
  pov /= 9000;
 }
 else if(lowpov > (4500 + 4500/2))
 {
  pov /= 9000;
  pov = (pov + 1) % 4;
 }
 else
 {
  if(!roundpos) pov /= 9000;
  else { pov /= 9000; pov = (pov + 1) % 4; }
 }
 return(pov);
}


typedef struct
{
 LONG MinX;
 LONG MaxX;
 LONG MinY;
 LONG MaxY;
 LONG MinZ;
 LONG MaxZ;
 LONG MinRx;
 LONG MaxRx;
 LONG MinRy;
 LONG MaxRy;
 LONG MinRz;
 LONG MaxRz;
} POWER_RANGER;

static POWER_RANGER ranges[MAX_JOYSTICKS];

//    r=diprg.lMax-diprg.lMin;
//    JoyXMax[w]=diprg.lMax-(r>>2);
//    JoyXMin[w]=diprg.lMin+(r>>2);

static int JoyAutoRestore(HRESULT dival,LPDIRECTINPUTDEVICE7 lpJJoy)
{
   switch(dival)
    {
     case DIERR_INPUTLOST:
     case DIERR_NOTACQUIRED:
                           return(IDirectInputDevice7_Acquire(lpJJoy)==DI_OK);
    }
  return(0);
}

/* Poll one joystick at most once during the current input update. */
static int PollJoystickState(int n)
{
 HRESULT dival;

 if(n < 0 || n >= numjoysticks)
  return(0);

 if(HavePolled[n])
  return(StatusValid[n]);

 while((dival = IDirectInputDevice7_Poll(Joysticks[n])) != DI_OK)
 {
  if(dival == DI_NOEFFECT)
   break;

  if(!JoyAutoRestore(dival, Joysticks[n]))
  {
   /* Treat an unavailable device as released so a hotkey cannot stick. */
   memset(&StatusSave[n], 0, sizeof(DIJOYSTATE2));
   StatusValid[n] = 1;
   HavePolled[n] = 1;
   return(1);
  }
 }

 dival = IDirectInputDevice7_GetDeviceState(Joysticks[n], sizeof(DIJOYSTATE2), &StatusSave[n]);
 if(dival != DI_OK)
  memset(&StatusSave[n], 0, sizeof(DIJOYSTATE2));

 StatusValid[n] = 1;
 HavePolled[n] = 1;
 return(1);
}

static int TestJoystickBinding(const DIJOYSTATE2 *state, int n, int buttonnum)
{
 if(buttonnum & 0x8000)       /* Axis "button" */
 {
  int axis = buttonnum & 7;
  long raw = 0;
  long min = 0;
  long max = 0;

  switch(axis)
  {
   case 0: raw = state->lX;  min = ranges[n].MinX;  max = ranges[n].MaxX;  break;
   case 1: raw = state->lY;  min = ranges[n].MinY;  max = ranges[n].MaxY;  break;
   case 2: raw = state->lZ;  min = ranges[n].MinZ;  max = ranges[n].MaxZ;  break;
   case 3: raw = state->lRx; min = ranges[n].MinRx; max = ranges[n].MaxRx; break;
   case 4: raw = state->lRy; min = ranges[n].MinRy; max = ranges[n].MaxRy; break;
   case 5: raw = state->lRz; min = ranges[n].MinRz; max = ranges[n].MaxRz; break;
   default: return(0);
  }

  if(max == min)
   return(0);

  /* Normalize to roughly -131072..131071, matching the existing input path. */
  long source = (long)(((int64)raw - min) * 262144 / (max - min) - 131072);

  if(buttonnum & 0x4000)
   return(source <= (0 - 262144 / 4));
  else
   return(source >= (262144 / 4));
 }
 else if(buttonnum & 0x2000) /* Hat "button" */
 {
  int hat = (buttonnum >> 4) & 3;
  int target = buttonnum & 3;
  int pov = state->rgdwPOV[hat];

  return(POVFix(pov, 0) == target || POVFix(pov, 1) == target);
 }
 else                           /* Normal button */
 {
  int button = buttonnum & 127;
  return((state->rgbButtons[button] & 0x80) != 0);
 }
}

/* Called once at the beginning of each raw-input/hotkey update. */
void UpdateJoysticks(void)
{
 int n;

 /* The state polled during the preceding update becomes the comparison state
    used by just-down hotkeys during this update. */
 for(n = 0; n < numjoysticks; n++)
 {
  if(HavePolled[n] && StatusValid[n])
  {
   memcpy(&StatusPrevious[n], &StatusSave[n], sizeof(DIJOYSTATE2));
   PreviousValid[n] = 1;
  }
 }

 memset(HavePolled, 0, sizeof(HavePolled));
}

int DTestButtonJoy(ButtConfig *bc, uint8_t just_down)
{
 uint32 x;

 for(x = 0; x < bc->NumC; x++)
 {
  int n;
  int current;

  if(bc->ButtType[x] != BUTTC_JOYSTICK)
   continue;

  n = bc->DeviceNum[x];
  if(n == 0xFF || n >= numjoysticks)
   continue;

  if(!PollJoystickState(n))
   continue;

  current = TestJoystickBinding(&StatusSave[n], n, bc->ButtonNum[x]);
  if(!current)
   continue;

  if(!just_down)
   return(1);

  /* Suppress a false edge on the first poll after startup, reconnect, or
     leaving the mapping dialog.  A release followed by a new press will fire. */
  if(!PreviousValid[n])
   continue;

  if(!TestJoystickBinding(&StatusPrevious[n], n, bc->ButtonNum[x]))
   return(1);
 }

 return(0);
}

static int canax[MAX_JOYSTICKS][6];

/* Now the fun configuration test begins. */
void BeginJoyWait(HWND hwnd)
{
 int n;

 //StatusSave = malloc(sizeof(DIJOYSTATE2) * numjoysticks);
 memset(canax, 0, sizeof(canax));

 for(n=0; n<numjoysticks; n++)
 {
  IDirectInputDevice7_Unacquire(Joysticks[n]);
  IDirectInputDevice7_SetCooperativeLevel(Joysticks[n],hwnd,DISCL_FOREGROUND|DISCL_NONEXCLUSIVE);
  IDirectInputDevice7_Acquire(Joysticks[n]);
  IDirectInputDevice7_Poll(Joysticks[n]);
  IDirectInputDevice7_GetDeviceState(Joysticks[n],sizeof(DIJOYSTATE2),&StatusSave[n]);
 }
}

int DoJoyWaitTest(GUID *guid, uint8 *devicenum, uint16 *buttonnum)
{
 int n;
 int x;

 for(n=0; n<numjoysticks; n++)
 {
  HRESULT dival;
  DIJOYSTATE2 JoyStatus;
  int ba;

  while((dival = IDirectInputDevice7_Poll(Joysticks[n])) != DI_OK)
  {
   if(dival == DI_NOEFFECT) break;
   if(!JoyAutoRestore(dival,Joysticks[n])) return(0);
  }
  dival = IDirectInputDevice7_GetDeviceState(Joysticks[n],sizeof(DIJOYSTATE2),&JoyStatus);

  for(ba = 0; ba < 128; ba++)
   if((JoyStatus.rgbButtons[ba]&0x80) && !(StatusSave[n].rgbButtons[ba]&0x80))
   {
    *devicenum = n;
    *buttonnum = ba;
    *guid = JoyGUID[n];
    //memcpy(&StatusSave[n], &JoyStatus, sizeof(DIJOYSTATE2));
    memcpy(StatusSave[n].rgbButtons, JoyStatus.rgbButtons, 128);
    return(1);
   }

  memcpy(StatusSave[n].rgbButtons, JoyStatus.rgbButtons, 128);

  long source,psource;

  for (int axis = 0; axis < 6; axis++)
  {
	  long da, jsl, ssl, min;
	  switch (axis)
	  {
	  case 0:
		  da = ranges[n].MaxX - ranges[n].MinX;
		  jsl = JoyStatus.lX;
		  ssl = StatusSave[n].lX;
		  min = ranges[n].MinX;
		  break;
	  case 1:
		  da = ranges[n].MaxY - ranges[n].MinY;
		  jsl = JoyStatus.lY;
		  ssl = StatusSave[n].lY;
		  min = ranges[n].MinY;
		  break;
	  case 2:
		  da = ranges[n].MaxZ - ranges[n].MinZ;
		  jsl = JoyStatus.lZ;
		  ssl = StatusSave[n].lZ;
		  min = ranges[n].MinZ;
		  break;
	  case 3:
		  da = ranges[n].MaxRx - ranges[n].MinRx;
		  jsl = JoyStatus.lRx;
		  ssl = StatusSave[n].lRx;
		  min = ranges[n].MinRx;
		  break;
	  case 4:
		  da = ranges[n].MaxRy - ranges[n].MinRy;
		  jsl = JoyStatus.lRy;
		  ssl = StatusSave[n].lRy;
		  min = ranges[n].MinRy;
		  break;
	  case 5:
		  da = ranges[n].MaxRz - ranges[n].MinRz;
		  jsl = JoyStatus.lRz;
		  ssl = StatusSave[n].lRz;
		  min = ranges[n].MinRz;
		  break;
	  }

	  if (da)
	  {
		  source = ((int64)jsl - min) * 262144 / da - 131072;
		  psource = ((int64)ssl - min) * 262144 / da - 131072;

		  if (abs(source) >= 65536 && canax[n][axis])
		  {
			  *guid = JoyGUID[n];
			  *devicenum = n;
			  *buttonnum = 0x8000 | axis | ((source < 0) ? 0x4000 : 0);
			  memcpy(&StatusSave[n], &JoyStatus, sizeof(DIJOYSTATE2));
			  canax[n][axis] = 0;
			  return(1);
		  }
		  else if (abs(source) <= 32768) canax[n][axis] = 1;
	  }
  }

  for(x=0; x<4; x++)
  {   
   if(POVFix(JoyStatus.rgdwPOV[x],-1) != FPOV_CENTER && POVFix(StatusSave[n].rgdwPOV[x],-1) == FPOV_CENTER)
   {
    *guid = JoyGUID[n];
    *devicenum = n;
    *buttonnum = 0x2000 | (x<<4) | POVFix(JoyStatus.rgdwPOV[x], -1);
    memcpy(&StatusSave[n], &JoyStatus, sizeof(DIJOYSTATE2));
    return(1);
   }
  }
  memcpy(&StatusSave[n], &JoyStatus, sizeof(DIJOYSTATE2));
 }
 
 return(0);
}

void EndJoyWait(HWND hwnd)
{
 int n;

 for(n=0; n<numjoysticks; n++)
 {
  IDirectInputDevice7_Unacquire(Joysticks[n]);
  IDirectInputDevice7_SetCooperativeLevel(Joysticks[n],hwnd,DISCL_FOREGROUND|DISCL_NONEXCLUSIVE);
 }

 /* Do not let the button used to assign a hotkey immediately activate it. */
 memset(HavePolled, 0, sizeof(HavePolled));
 memset(StatusValid, 0, sizeof(StatusValid));
 memset(PreviousValid, 0, sizeof(PreviousValid));
}

int KillJoysticks (void)
{
 int x;

 for(x=0; x<numjoysticks; x++)
 {
  IDirectInputDevice7_Unacquire(Joysticks[x]);
  IDirectInputDevice7_Release(Joysticks[x]);
  Joysticks[x] = NULL;
 } 

 numjoysticks = 0;
 memset(HavePolled, 0, sizeof(HavePolled));
 memset(StatusValid, 0, sizeof(StatusValid));
 memset(PreviousValid, 0, sizeof(PreviousValid));
 memset(StatusSave, 0, sizeof(StatusSave));
 memset(StatusPrevious, 0, sizeof(StatusPrevious));

 return(1);
}

void JoyClearBC(ButtConfig *bc)
{
 uint32 x; //mbg merge 7/17/06 changed to uint
 for(x=0; x<bc->NumC; x++)
  if(bc->ButtType[x] == BUTTC_JOYSTICK)
   bc->DeviceNum[x] = FindByGUID(bc->DeviceInstance[x]);
}

static int GetARange(LPDIRECTINPUTDEVICE7 dev, LONG which, LONG *min, LONG *max)
{
    HRESULT dival;
    DIPROPRANGE diprg;
    //int r; //mbg merge 7/17/06 removed

    memset(&diprg,0,sizeof(DIPROPRANGE));
    diprg.diph.dwSize=sizeof(DIPROPRANGE);
    diprg.diph.dwHeaderSize=sizeof(DIPROPHEADER);
    diprg.diph.dwHow=DIPH_BYOFFSET;
    diprg.diph.dwObj=which;
    dival=IDirectInputDevice7_GetProperty(dev,DIPROP_RANGE,&diprg.diph);
    if(dival!=DI_OK)
    {
     *min = *max = 0;
     return(1);
    }
    *min = diprg.lMin;
    *max = diprg.lMax;
    return(1);
}

static BOOL CALLBACK JoystickFound(LPCDIDEVICEINSTANCE lpddi, LPVOID pvRef)
{
 //HRESULT dival; //mbg merge 7/17/06 removed
 int n = numjoysticks;

 //mbg merge 7/17/06 changed:
 if(DI_OK != IDirectInput7_CreateDeviceEx(lpDI,lpddi->guidInstance,IID_IDirectInputDevice7,(LPVOID *)&Joysticks[n],0))
 //if(DI_OK != IDirectInput7_CreateDeviceEx(lpDI,&lpddi->guidInstance,&IID_IDirectInputDevice7,(LPVOID *)&Joysticks[n],0))
 
 {
  FCEU_printf("Device creation of a joystick failed during init.\n");
  return(DIENUM_CONTINUE);
 }

 if(DI_OK != IDirectInputDevice7_SetCooperativeLevel(Joysticks[n],*(HWND *)pvRef, (background?DISCL_BACKGROUND:DISCL_FOREGROUND)|DISCL_NONEXCLUSIVE))
 {
  FCEU_printf("Cooperative level set of a joystick failed during init.\n");
  IDirectInputDevice7_Release(Joysticks[n]);
  return(DIENUM_CONTINUE);
 }

 if(DI_OK != IDirectInputDevice7_SetDataFormat(Joysticks[n], &c_dfDIJoystick2))
 {
  FCEU_printf("Data format set of a joystick failed during init.\n");
  IDirectInputDevice7_Release(Joysticks[n]);
  return(DIENUM_CONTINUE);
 }

 GetARange(Joysticks[n], DIJOFS_X, &ranges[n].MinX, &ranges[n].MaxX);
 GetARange(Joysticks[n], DIJOFS_Y, &ranges[n].MinY, &ranges[n].MaxY);
 GetARange(Joysticks[n], DIJOFS_Z, &ranges[n].MinZ, &ranges[n].MaxZ);
 GetARange(Joysticks[n], DIJOFS_RX, &ranges[n].MinRx, &ranges[n].MaxRx);
 GetARange(Joysticks[n], DIJOFS_RY, &ranges[n].MinRy, &ranges[n].MaxRy);
 GetARange(Joysticks[n], DIJOFS_RZ, &ranges[n].MinRz, &ranges[n].MaxRz);

 JoyGUID[numjoysticks] = lpddi->guidInstance;

 if(DI_OK != IDirectInputDevice7_Acquire(Joysticks[n]))
 {
  //FCEU_printf("Acquire of a joystick failed during init.\n");  
 }

 numjoysticks++;

 if(numjoysticks > MAX_JOYSTICKS) return(0);

 return(DIENUM_CONTINUE);
}

int InitJoysticks(HWND hwnd)
{
 IDirectInput7_EnumDevices(lpDI, DIDEVTYPE_JOYSTICK, JoystickFound, (LPVOID *)&hwnd, DIEDFL_ATTACHEDONLY);
 return(1);
}

static bool curr = false;


static void UpdateBackgroundAccess(bool on)
{
	if(curr == on) return;

	curr = on;

	for(int n=0; n<numjoysticks; n++)
	{
		IDirectInputDevice7_Unacquire(Joysticks[n]);
		if(background)
			IDirectInputDevice7_SetCooperativeLevel(Joysticks[n],hAppWnd,DISCL_BACKGROUND|DISCL_NONEXCLUSIVE);
		else
			IDirectInputDevice7_SetCooperativeLevel(Joysticks[n],hAppWnd,DISCL_FOREGROUND|DISCL_NONEXCLUSIVE);
		IDirectInputDevice7_Acquire(Joysticks[n]);
	}
}

void JoystickSetBackgroundAccessBit(int bit)
{
	background |= (1<<bit);
	UpdateBackgroundAccess(background != 0);
}
void JoystickClearBackgroundAccessBit(int bit)
{
	background &= ~(1<<bit);
	UpdateBackgroundAccess(background != 0);
}

void JoystickSetBackgroundAccess(bool on)
{
	if(on)
		JoystickSetBackgroundAccessBit(JOYBACKACCESS_OLDSTYLE);
	else
		JoystickClearBackgroundAccessBit(JOYBACKACCESS_OLDSTYLE);
}
