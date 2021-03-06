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

#include "genall.h"
#include "../build.h"
#include "../encount.h"
#include "../ipdisp.h"
#include "../state.h"
#include "libs/mathlib.h"


void
GenerateIlwrath (BYTE control)
{
	switch (control)
	{
		case GENERATE_ENERGY:
			if (pSolarSysState->pOrbitalDesc == &pSolarSysState->PlanetDesc[0])
			{
				COUNT i, which_node;
				DWORD rand_val, old_rand;

				old_rand = TFB_SeedRandom (
						pSolarSysState->SysInfo.PlanetInfo.ScanSeed[ENERGY_SCAN]
						);

				which_node = i = 0;
				do
				{
					rand_val = TFB_Random ();
					pSolarSysState->SysInfo.PlanetInfo.CurPt.x =
							(LOBYTE (LOWORD (rand_val)) % (MAP_WIDTH - (8 << 1))) + 8;
					pSolarSysState->SysInfo.PlanetInfo.CurPt.y =
							(HIBYTE (LOWORD (rand_val)) % (MAP_HEIGHT - (8 << 1))) + 8;
					pSolarSysState->SysInfo.PlanetInfo.CurType = 1;
					pSolarSysState->SysInfo.PlanetInfo.CurDensity = 0;
					if (which_node >= pSolarSysState->CurNode
							&& !(pSolarSysState->SysInfo.PlanetInfo.ScanRetrieveMask[ENERGY_SCAN]
							& (1L << i)))
						break;
					++which_node;
				} while (++i < 16);
				pSolarSysState->CurNode = which_node;

				TFB_SeedRandom (old_rand);
				break;
			}
			pSolarSysState->CurNode = 0;
			break;
		case GENERATE_MOONS:
			GenerateRandomIP (GENERATE_MOONS);
			if (CurStarDescPtr->Index == ILWRATH_DEFINED &&
					pSolarSysState->pBaseDesc == &pSolarSysState->PlanetDesc[0])
			{
				pSolarSysState->MoonDesc[0].data_index =
						(ActivateStarShip (ILWRATH_SHIP, SPHERE_TRACKING)) ?
						HIERARCHY_STARBASE : DESTROYED_STARBASE;
				pSolarSysState->MoonDesc[0].radius = MIN_MOON_RADIUS;
				pSolarSysState->MoonDesc[0].location.x =
						COSINE (OCTANT, pSolarSysState->MoonDesc[0].radius);
				pSolarSysState->MoonDesc[0].location.y =
						SINE (OCTANT, pSolarSysState->MoonDesc[0].radius);
			}
			break;
		case GENERATE_PLANETS:
		{
			COUNT angle;

			GenerateRandomIP (GENERATE_PLANETS);
			pSolarSysState->PlanetDesc[0].data_index = PRIMORDIAL_WORLD;
			pSolarSysState->PlanetDesc[0].radius = EARTH_RADIUS * 204L / 100;
			angle = ARCTAN (
					pSolarSysState->PlanetDesc[0].location.x,
					pSolarSysState->PlanetDesc[0].location.y
					);
			pSolarSysState->PlanetDesc[0].location.x =
					COSINE (angle, pSolarSysState->PlanetDesc[0].radius);
			pSolarSysState->PlanetDesc[0].location.y =
					SINE (angle, pSolarSysState->PlanetDesc[0].radius);
			pSolarSysState->PlanetDesc[0].NumPlanets = 1;
			break;
		}
		case GENERATE_ORBITAL:
			if ((CurStarDescPtr->Index == ILWRATH_DEFINED) &&
					(pSolarSysState->pOrbitalDesc == &pSolarSysState->MoonDesc[0]) &&
					(pSolarSysState->pOrbitalDesc->pPrevDesc == &pSolarSysState->PlanetDesc[0]))
				if (VisitHomeWorldStarBase (ActivateStarShip (ILWRATH_SHIP, SPHERE_TRACKING)))
					break;

			if (pSolarSysState->pOrbitalDesc == &pSolarSysState->PlanetDesc[0])
			{
				if (ActivateStarShip (ILWRATH_SHIP, SPHERE_TRACKING))
				{
					NotifyOthers (ILWRATH_SHIP, IPNL_ALL_CLEAR);
					PutGroupInfo (GROUPS_RANDOM, GROUP_SAVE_IP);
					ReinitQueue (&GLOBAL (ip_group_q));
					assert (CountLinks (&GLOBAL (npc_built_ship_q)) == 0);

					CloneShipFragment (ILWRATH_SHIP,
							&GLOBAL (npc_built_ship_q), INFINITE_FLEET);

					pSolarSysState->MenuState.Initialized += 2;
					GLOBAL (CurrentActivity) |= START_INTERPLANETARY;
					SET_GAME_STATE (GLOBAL_FLAGS_AND_DATA, 1 << 7);
					InitCommunication (ILWRATH_CONVERSATION);
					pSolarSysState->MenuState.Initialized -= 2;

					if (!(GLOBAL (CurrentActivity) & (CHECK_ABORT | CHECK_LOAD)))
					{
						GLOBAL (CurrentActivity) &= ~START_INTERPLANETARY;
						ReinitQueue (&GLOBAL (npc_built_ship_q));
						GetGroupInfo (GROUPS_RANDOM, GROUP_LOAD_IP);
					}
					break;
				}
				else
				{
					LoadStdLanderFont (&pSolarSysState->SysInfo.PlanetInfo);
					pSolarSysState->PlanetSideFrame[1] =
							CaptureDrawable (
							LoadGraphic (RUINS_MASK_PMAP_ANIM)
							);
					pSolarSysState->SysInfo.PlanetInfo.DiscoveryString =
							CaptureStringTable (
									LoadStringTable (RUINS_STRTAB)
									);
				}
			}
			GenerateRandomIP (GENERATE_ORBITAL);
			if (pSolarSysState->pOrbitalDesc == &pSolarSysState->PlanetDesc[0])
			{
				pSolarSysState->SysInfo.PlanetInfo.Weather = 2;
				pSolarSysState->SysInfo.PlanetInfo.Tectonics = 3;
			}
			break;
		default:
			GenerateRandomIP (control);
			break;
	}
}

