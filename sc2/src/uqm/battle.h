/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef _BATTLE_H
#define _BATTLE_H

#include "options.h"
#include "libs/compiler.h"

#if defined (NETPLAY)
typedef DWORD BattleFrameCounter;
#endif

#include "init.h"
		// For NUM_SIDES

typedef struct battlestate_struct {
	BOOLEAN (*InputFunc) (struct battlestate_struct *pInputState);
	COUNT MenuRepeatDelay;
	BOOLEAN first_time;
	DWORD NextTime;
} BATTLE_STATE;

extern BYTE battle_counter[NUM_SIDES];
extern BOOLEAN instantVictory;
#if defined (NETPLAY)
extern BattleFrameCounter battleFrameCount;
#endif
#ifdef NETPLAY
extern COUNT currentDeadSide;
COUNT GetPlayerOrder (COUNT i);
#else
#	define GetPlayerOrder(i) (i)
#endif

BOOLEAN Battle (void);

#define BATTLE_FRAME_RATE (ONE_SECOND / 24)

extern void BattleSong (BOOLEAN DoPlay);
extern void FreeBattleSong (void);

#endif  /* _BATTLE_H */
