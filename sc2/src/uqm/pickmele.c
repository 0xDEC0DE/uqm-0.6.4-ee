//Copyright Paul Reiche, Fred Ford. 1992-2002

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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#define PICKMELE_INTERNAL
#include "pickmele.h"

#include "battlecontrols.h"
#include "controls.h"
#include "flash.h"
#include "intel.h"
#include "battle.h"
#include "melee.h"
#ifdef NETPLAY
#	include "netplay/netmelee.h"
#	include "netplay/netmisc.h"
#	include "netplay/notify.h"
#endif
#include "races.h"
#include "setup.h"
#include "sounds.h"
#include "libs/log.h"
#include "libs/mathlib.h"


#define NUM_MELEE_COLS_ORIG NUM_MELEE_COLUMNS
#define PICK_X_OFFS 57
#define PICK_Y_OFFS 24
#define PICK_SIDE_OFFS 100

#ifdef NETPLAY
static void reportShipSelected (GETMELEE_STATE *gms, COUNT index);
#endif

// Returns the <index>th ship in the queue, or 0 if it is not available.
static HSTARSHIP
MeleeShipByQueueIndex (const QUEUE *queue, COUNT index)
{
	HSTARSHIP hShip;
	HSTARSHIP hNextShip;
	
	for (hShip = GetHeadLink (queue); hShip != 0; hShip = hNextShip)
	{
		STARSHIP *StarShipPtr = LockStarShip (queue, hShip);
		if (StarShipPtr->index == index)
		{
			hNextShip = hShip;
			if (StarShipPtr->SpeciesID == NO_ID)
				hShip = 0;
			UnlockStarShip (queue, hNextShip);
			break;
		}
		hNextShip = _GetSuccLink (StarShipPtr);
		UnlockStarShip (queue, hShip);
	}

	return hShip;
}

// Returns the <index>th available ship in the queue.
static HSTARSHIP
MeleeShipByUsedIndex (const QUEUE *queue, COUNT index)
{
	HSTARSHIP hShip;
	HSTARSHIP hNextShip;
	
	for (hShip = GetHeadLink (queue); hShip != 0; hShip = hNextShip)
	{
		STARSHIP *StarShipPtr = LockStarShip (queue, hShip);
		if ((StarShipPtr->SpeciesID != NO_ID) && index-- == 0)
		{
			UnlockStarShip (queue, hShip);
			break;
		}
		hNextShip = _GetSuccLink (StarShipPtr);
		UnlockStarShip (queue, hShip);
	}

	return hShip;
}

#if 0
static COUNT
queueIndexFromShip (HSTARSHIP hShip)
{
	COUNT result;
	STARSHIP *StarShipPtr = LockStarShip (queue, hShip);
	result = StarShipPtr->index;
	UnlockStarShip (queue, hShip);
}
#endif

// Pre: called does not hold the graphics lock
static void
PickMelee_ChangedSelection (GETMELEE_STATE *gms, COUNT playerI)
{
	RECT r;
	r.corner.x = PICK_X_OFFS + ((ICON_WIDTH + 2) * gms->player[playerI].col);
	r.corner.y = PICK_Y_OFFS + ((ICON_HEIGHT + 2) * gms->player[playerI].row)
			+ ((1 - playerI) * PICK_SIDE_OFFS);
	r.extent.width = (ICON_WIDTH + 2);
	r.extent.height = (ICON_HEIGHT + 2);
	Flash_setRect (gms->player[playerI].flashContext, &r);
}

// Only returns false when there is no ship for the choice.
bool
setShipSelected(GETMELEE_STATE *gms, COUNT playerI, COUNT choice,
		bool reportNetwork)
{
	HSTARSHIP ship;

	assert (!gms->player[playerI].done);

	if (choice == (COUNT) ~0)
	{
		// Random ship selection.
		ship = MeleeShipByUsedIndex (&race_q[playerI],
				gms->player[playerI].randomIndex);
	}
	else
	{
		// Explicit ship selection.
		ship = MeleeShipByQueueIndex (&race_q[playerI], choice);
	}

	if (ship == 0)
		return false;

	gms->player[playerI].choice = choice;
	gms->player[playerI].hBattleShip = ship;
	PlayMenuSound (MENU_SOUND_SUCCESS);
#ifdef NETPLAY
	if (reportNetwork)
		reportShipSelected (gms, choice);
#else
	(void) reportNetwork;
#endif
	gms->player[playerI].done = true;
	return true;
}

// Returns FALSE if aborted.
static BOOLEAN
SelectShip_processInput (GETMELEE_STATE *gms, COUNT playerI,
		BATTLE_INPUT_STATE inputState)
{
	if (inputState & BATTLE_WEAPON)
	{
		if (gms->player[playerI].col == NUM_MELEE_COLS_ORIG &&
				gms->player[playerI].row == 0)
		{
			// Random ship
			(void) setShipSelected (gms, playerI, (COUNT) ~0, TRUE);
		}
		else if (gms->player[playerI].col == NUM_MELEE_COLS_ORIG &&
				gms->player[playerI].row == 1)
		{
			// Selected exit
			if (ConfirmExit ())
				return FALSE;
		}
		else
		{
			// Selection is on a ship slot.
			COUNT shipI =
					(gms->player[playerI].row * NUM_MELEE_COLS_ORIG)
					+ gms->player[playerI].col;
			(void) setShipSelected (gms, playerI, shipI, TRUE);
					// If the choice is not valid, setShipSelected()
					// will not set .done.
		}
	}
	else
	{
		// Process motion commands.
		COUNT new_row, new_col;
		
		new_row = gms->player[playerI].row;
		new_col = gms->player[playerI].col;
		if (inputState & BATTLE_LEFT)
		{
			if (new_col-- == 0)
				new_col = NUM_MELEE_COLS_ORIG;
		}
		else if (inputState & BATTLE_RIGHT)
		{
			if (new_col++ == NUM_MELEE_COLS_ORIG)
				new_col = 0;
		}
		if (inputState & BATTLE_THRUST)
		{
			if (new_row-- == 0)
				new_row = NUM_MELEE_ROWS - 1;
		}
		else if (inputState & BATTLE_DOWN)
		{
			if (++new_row == NUM_MELEE_ROWS)
				new_row = 0;
		}
		
		if (new_row != gms->player[playerI].row ||
				new_col != gms->player[playerI].col)
		{
			gms->player[playerI].row = new_row;
			gms->player[playerI].col = new_col;
			
			PlayMenuSound (MENU_SOUND_MOVE);
			PickMelee_ChangedSelection (gms, playerI);
		}
	}

	return TRUE;
}

BOOLEAN
selectShipHuman (HumanInputContext *context, GETMELEE_STATE *gms)
{
	BATTLE_INPUT_STATE inputState =
			PulsedInputToBattleInput (context->playerNr);

	return SelectShip_processInput (gms, context->playerNr, inputState);
}

BOOLEAN
selectShipComputer (ComputerInputContext *context, GETMELEE_STATE *gms)
{
#define COMPUTER_SELECTION_DELAY (ONE_SECOND >> 1)
	TimeCount now = GetTimeCounter ();
	if (now < gms->player[context->playerNr].timeIn +
			COMPUTER_SELECTION_DELAY)
		return TRUE;

	return SelectShip_processInput (gms, context->playerNr, BATTLE_WEAPON);
			// Simulate selection of the random choice button.
}

#ifdef NETPLAY
BOOLEAN
selectShipNetwork (NetworkInputContext *context, GETMELEE_STATE *gms)
{
	flushPacketQueues ();
			// Sets gms->player[context->playerNr].remoteSelected if input
			// is received.
	if (gms->player[context->playerNr].remoteSelected)
		gms->player[context->playerNr].done = TRUE;

	return TRUE;
}
#endif

// Select a new ship from the fleet for battle.
// Returns 'TRUE' if no choice has been made yet; this function is to be
// called again later.
// Returns 'FALSE' if a choice has been made. gms->hStarShip is set
// to the chosen (or randomly selected) ship, or to 0 if 'exit' has
// been chosen.
/* TODO: Include player timeouts */
static BOOLEAN
DoGetMelee (GETMELEE_STATE *gms)
{
	BOOLEAN done;
	COUNT playerI;

	SetMenuSounds (MENU_SOUND_NONE, MENU_SOUND_NONE);

	if (!gms->Initialized)
	{
		gms->Initialized = TRUE;
		return TRUE;
	}

	for (playerI = 0; playerI < NUM_PLAYERS; playerI++)
	{
		if (!gms->player[playerI].selecting)
			continue;

		if (!gms->player[playerI].done)
			Flash_process (gms->player[playerI].flashContext);
	}

	SleepThread (ONE_SECOND / 120);

#ifdef NETPLAY
	netInput ();

	if (!allConnected ())
		goto aborted;
#endif
	
	if (GLOBAL (CurrentActivity) & CHECK_ABORT)
		goto aborted;

	done = TRUE;
	for (playerI = 0; playerI < NUM_PLAYERS; playerI++)
	{
		if (!gms->player[playerI].selecting)
			continue;

		if (!gms->player[playerI].done) {
			if (!PlayerInput[playerI]->handlers->selectShip (
					PlayerInput[playerI], gms))
				goto aborted;

			if (gms->player[playerI].done)
			{
				Flash_terminate (gms->player[playerI].flashContext);
				gms->player[playerI].flashContext = NULL;
			}
			else
				done = FALSE;
		}
	}

#ifdef NETPLAY
	flushPacketQueues ();
#endif
	return !done;

aborted:
#ifdef NETPLAY
	flushPacketQueues ();
#endif
	for (playerI = 0; playerI < NUM_PLAYERS; playerI++)
	{
		if (!gms->player[playerI].selecting)
			continue;

		gms->player[playerI].choice = 0;
		gms->player[playerI].hBattleShip = 0;
	}
	GLOBAL (CurrentActivity) &= ~CHECK_ABORT;
	return FALSE;
}

#ifdef NETPLAY
static void
endMeleeCallback (NetConnection *conn, void *arg)
{
	NetMelee_reenterState_inSetup (conn);
	(void) arg;
}
#endif

static COUNT
GetRaceQueueValue (const QUEUE *queue) {
	COUNT result;
	HSTARSHIP hBattleShip, hNextShip;

	result = 0;
	for (hBattleShip = GetHeadLink (queue);
			hBattleShip != 0; hBattleShip = hNextShip)
	{
		STARSHIP *StarShipPtr = LockStarShip (queue, hBattleShip);
		hNextShip = _GetSuccLink (StarShipPtr);
		
		if (StarShipPtr->SpeciesID == NO_ID)
			continue;  // Not active any more.

		result += StarShipPtr->ship_cost;

		UnlockStarShip (queue, hBattleShip);
	}

	return result;
}

// Cross out the icon for the dead ship.
// 'frame' is the PickMeleeFrame for the player.
// 'shipI' is the index in the ship list.
// Pre: caller holds the graphics lock.
static void
CrossOutShip (FRAME frame, COUNT shipI)
{
	CONTEXT OldContext;
	STAMP s;

	OldContext = SetContext (OffScreenContext);
	
	SetContextFGFrame (frame);

	s.origin.x = 3 + ((ICON_WIDTH + 2) * (shipI % NUM_MELEE_COLS_ORIG));
	s.origin.y = 9 + ((ICON_HEIGHT + 2) * (shipI / NUM_MELEE_COLS_ORIG));
	s.frame = SetAbsFrameIndex (StatusFrame, 3);
			// Cross for through the ship image.
	DrawStamp (&s);

	SetContext (OldContext);
}

// Draw the value of the fleet in the top right of the PickMeleeFrame.
// Pre: caller holds the graphics lock.
static void
UpdatePickMeleeFleetValue (FRAME frame, COUNT which_player)
{
	CONTEXT OldContext;
	COUNT value;
	RECT r;
	TEXT t;
	UNICODE buf[40];
	
	value = GetRaceQueueValue (&race_q[which_player]);

	OldContext = SetContext (OffScreenContext);
	SetContextFGFrame (frame);

	// Erase the old value text.
	GetFrameRect (frame, &r);
	r.extent.width -= 4;
	t.baseline.x = r.extent.width;
	r.corner.x = r.extent.width - (6 * 3);
	r.corner.y = 2;
	r.extent.width = (6 * 3);
	r.extent.height = 7 - 2;
	SetContextForeGroundColor (PICK_BG_COLOR);
	DrawFilledRectangle (&r);

	// Draw the new value text.
	sprintf (buf, "%d", value);
	t.baseline.y = 7;
	t.align = ALIGN_RIGHT;
	t.pStr = buf;
	t.CharCount = (COUNT)~0;
	SetContextFont (TinyFont);
	SetContextForeGroundColor (PICK_VALUE_COLOR);
	font_DrawText (&t);
	
	SetContext (OldContext);
}

// Pre: caller holds the graphics lock.
static void
DrawPickMeleeFrame (COUNT which_player)
{
	CONTEXT oldContext;
	STAMP s;

	oldContext = SetContext (SpaceContext);
	s.frame = SetAbsFrameIndex (PickMeleeFrame, which_player);
	s.origin.x = PICK_X_OFFS - 3;
	s.origin.y = PICK_Y_OFFS - 9 + ((1 - which_player) * PICK_SIDE_OFFS);
	DrawStamp (&s);
			// Draw the selection box to screen.
	
	SetContext (oldContext);
}

// Pre: caller holds the graphics lock.
void
MeleeGameOver (void)
{
	COUNT playerI;
	DWORD TimeOut;
	BOOLEAN PressState, ButtonState;

	// Show the battle result.
	for (playerI = 0; playerI < NUM_PLAYERS; playerI++)
		DrawPickMeleeFrame (playerI);

	TimeOut = GetTimeCounter () + (ONE_SECOND * 4);
	UnlockMutex (GraphicsLock);

	PressState = PulsedInputState.menu[KEY_MENU_SELECT] ||
			PulsedInputState.menu[KEY_MENU_CANCEL];
	do
	{
		UpdateInputState ();
		ButtonState = PulsedInputState.menu[KEY_MENU_SELECT] ||
				PulsedInputState.menu[KEY_MENU_CANCEL];
		if (PressState)
		{
			PressState = ButtonState;
			ButtonState = FALSE;
		}

		TaskSwitch ();
	} while (!(GLOBAL (CurrentActivity) & CHECK_ABORT) && (!ButtonState
			&& (!(PlayerControl[0] & PlayerControl[1] & PSYTRON_CONTROL)
			|| GetTimeCounter () < TimeOut)));

#ifdef NETPLAY
	setStateConnections (NetState_endMelee);
	localReadyConnections (endMeleeCallback, NULL, true);
#endif

	LockMutex (GraphicsLock);
}

BOOLEAN
MeleeShipDeath (STARSHIP *ship, COUNT which_player) {
	FRAME frame;

	// Deactivate fleet position.
	ship->SpeciesID = NO_ID;

	frame = SetAbsFrameIndex (PickMeleeFrame, which_player);
	CrossOutShip (frame, ship->index);
	UpdatePickMeleeFleetValue (frame, which_player);
	
	if (battle_counter[0] == 0 || battle_counter[1] == 0)
	{
		// One side is out of ships. Game over.
		return FALSE;
	}

	return TRUE;
}

// Post: the NetState for all players is NetState_interBattle
static BOOLEAN
GetMeleeStarShips (COUNT playerMask, HSTARSHIP *ships)
{
	COUNT playerI;
	BOOLEAN ok;
	GETMELEE_STATE gmstate;
	TimeCount now;
	COUNT i;

#ifdef NETPLAY
	for (playerI = 0; playerI < NUM_PLAYERS; playerI++)
	{
		NetConnection *conn;

		if ((playerMask & (1 << playerI)) == 0)
			continue;

		// XXX: This does not have to be done per connection.
		conn = netConnections[playerI];
		if (conn != NULL) {
			BattleStateData *battleStateData;
			battleStateData =
					(BattleStateData *) NetConnection_getStateData (conn);
			battleStateData->getMeleeState = &gmstate;
		}
	}
#endif
	
	ok = true;

	now = GetTimeCounter ();
	gmstate.InputFunc = DoGetMelee;
	gmstate.Initialized = FALSE;
	for (i = 0; i < NUM_PLAYERS; ++i)
	{
		// We have to use TFB_Random() results in specific order
		playerI = GetPlayerOrder (i);
		gmstate.player[playerI].selecting =
				(playerMask & (1 << playerI)) != 0;
		gmstate.player[playerI].ships_left = battle_counter[playerI];

		// We determine in advance which ship would be chosen if the player
		// wants a random ship, to keep it simple to keep network parties
		// synchronised.
		gmstate.player[playerI].randomIndex =
				(COUNT)TFB_Random () % gmstate.player[playerI].ships_left;
		gmstate.player[playerI].done = FALSE;

		if (!gmstate.player[playerI].selecting)
			continue;

		gmstate.player[playerI].timeIn = now;
		gmstate.player[playerI].row = 0;
		gmstate.player[playerI].col = NUM_MELEE_COLS_ORIG;
#ifdef NETPLAY
		gmstate.player[playerI].remoteSelected = FALSE;
#endif

		gmstate.player[playerI].flashContext =
				Flash_createHighlight (ScreenContext, (FRAME) 0, NULL);
		Flash_setMergeFactors (gmstate.player[playerI].flashContext,
				2, 3, 2);
		Flash_setFrameTime (gmstate.player[playerI].flashContext,
				ONE_SECOND / 16);
#ifdef NETPLAY
		if (PlayerControl[playerI] & NETWORK_CONTROL)
			Flash_setSpeed (gmstate.player[playerI].flashContext,
					ONE_SECOND / 2, 0, ONE_SECOND / 2, 0);
		else
#endif
		{
			Flash_setSpeed (gmstate.player[playerI].flashContext,
					0, ONE_SECOND / 16, 0, ONE_SECOND / 16);
		}
		PickMelee_ChangedSelection (&gmstate, playerI);
		Flash_start (gmstate.player[playerI].flashContext);
	}

#ifdef NETPLAY
	{
		// NB. gmstate.player[].randomIndex and gmstate.player[].done must
		// be initialised before negotiateReadyConnections is completed, to
		// ensure that they are initialised when the SelectShip packet
		// arrives.
		bool allOk = negotiateReadyConnections (true, NetState_selectShip);
		if (!allOk)
		{
			// Some network connection has been reset.
			ok = false;
		}
	}
#endif
	SetDefaultMenuRepeatDelay ();
	
	SetContext (OffScreenContext);

	UnlockMutex (GraphicsLock);
	ResetKeyRepeat ();
	DoInput (&gmstate, FALSE);
	WaitForSoundEnd (0);

	LockMutex (GraphicsLock);

	for (playerI = 0; playerI < NUM_PLAYERS; playerI++)
	{
		if (!gmstate.player[playerI].selecting)
			continue;
		
		if (gmstate.player[playerI].done)
		{
			// Flash rectangle is already terminated.
			ships[playerI] = gmstate.player[playerI].hBattleShip;
		}
		else
		{
			Flash_terminate (gmstate.player[playerI].flashContext);
			gmstate.player[playerI].flashContext = NULL;
			ok = false;
		}
	}

#ifdef NETPLAY
	if (ok)
	{
		if (!negotiateReadyConnections (true, NetState_interBattle))
			ok = false;
	}
	else
		setStateConnections (NetState_interBattle);
#endif

	if (!ok)
	{
		// Aborting.
		GLOBAL (CurrentActivity) &= ~IN_BATTLE;
	}

#ifdef NETPLAY
	for (playerI = 0; playerI < NUM_PLAYERS; playerI++)
	{
		NetConnection *conn;

		if ((playerMask & (1 << playerI)) == 0)
			continue;

		// XXX: This does not have to be done per connection.
		conn = netConnections[playerI];
		if (conn != NULL && NetConnection_isConnected (conn))
		{
			BattleStateData *battleStateData;
			battleStateData =
					(BattleStateData *) NetConnection_getStateData (conn);
			battleStateData->getMeleeState = NULL;
		}
	}
#endif

	return ok;
}

BOOLEAN
GetInitialMeleeStarShips (HSTARSHIP *result)
{
	COUNT playerI;
	COUNT playerMask;

	for (playerI = 0; playerI < NUM_PLAYERS; playerI++)
	{
		FRAME frame;
		frame = SetAbsFrameIndex (PickMeleeFrame, playerI);
		UpdatePickMeleeFleetValue (frame, playerI);
		DrawPickMeleeFrame (playerI);
	}

	// Fade in
	{
		BYTE fade_buf[] = {FadeAllToColor};
		SleepThreadUntil (XFormColorMap
				((COLORMAPPTR) fade_buf, ONE_SECOND / 2) + ONE_SECOND / 60);
		FlushColorXForms ();
	}

	playerMask = 0;
	for (playerI = 0; playerI < NUM_PLAYERS; playerI++)
		playerMask |= (1 << playerI);

	return GetMeleeStarShips (playerMask, result);
}

BOOLEAN
GetNextMeleeStarShip (COUNT which_player, HSTARSHIP *result)
{
	COUNT playerMask;
	HSTARSHIP ships[NUM_PLAYERS];
	BOOLEAN ok;

	DrawPickMeleeFrame (which_player);

	playerMask = 1 << which_player;
	ok = GetMeleeStarShips (playerMask, ships);
	if (ok)
		*result = ships[which_player];

	return ok;
}

#ifdef NETPLAY
// Called when a ship selection has arrived from a remote player.
bool
updateMeleeSelection (GETMELEE_STATE *gms, COUNT playerI, COUNT ship)
{
	if (gms == NULL || !gms->player[playerI].selecting ||
			gms->player[playerI].done)
	{
		// This happens when we get an update message from a connection
		// for who we are not selecting a ship.
		log_add (log_Warning, "Unexpected ship selection packet "
				"received.\n");
		return false;
	}

	if (!setShipSelected (gms, playerI, ship, false))
	{
		log_add (log_Warning, "Invalid ship selection received from remote "
				"party.\n");
		return false;
	}

	gms->player[playerI].remoteSelected = TRUE;
	return true;
}

static void
reportShipSelected (GETMELEE_STATE *gms, COUNT index)
{
	size_t playerI;
	for (playerI = 0; playerI < NUM_PLAYERS; playerI++)
	{
		NetConnection *conn = netConnections[playerI];

		if (conn == NULL)
			continue;

		if (!NetConnection_isConnected (conn))
			continue;

		Netplay_selectShip (conn, index);
	}
	(void) gms;
}
#endif

