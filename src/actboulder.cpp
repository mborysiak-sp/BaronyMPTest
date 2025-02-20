/*-------------------------------------------------------------------------------

	BARONY
	File: actboulder.cpp
	Desc: implements boulder and boulder trap code

	Copyright 2013-2016 (c) Turning Wheel LLC, all rights reserved.
	See LICENSE for details.

-------------------------------------------------------------------------------*/

#include "main.hpp"
#include "game.hpp"
#include "stat.hpp"
#include "entity.hpp"
#include "engine/audio/sound.hpp"
#include "items.hpp"
#include "net.hpp"
#include "monster.hpp"
#include "collision.hpp"
#include "player.hpp"
#include "magic/magic.hpp"
#include "paths.hpp"
#include "scores.hpp"
#include "mod_tools.hpp"

#define BOULDER_STOPPED my->skill[0]
#define BOULDER_AMBIENCE my->skill[1]
#define BOULDER_NOGROUND my->skill[3]
#define BOULDER_ROLLING my->skill[4]
#define BOULDER_ROLLDIR my->skill[5]
#define BOULDER_DESTX my->skill[6]
#define BOULDER_DESTY my->skill[7]
#define BOULDER_PLAYERPUSHED my->skill[8]
#define BOULDER_SPAWNBLOOD my->skill[9]
#define BOULDER_BLOODTIME my->skill[10]
#define BOULDER_INIT my->skill[11]
#define BOULDER_LAVA_EXPLODE my->skill[12]
#define BOULDER_SOUND_ON_PUSH my->skill[13]

const int BOULDER_LAVA_SPRITE = 989;
const int BOULDER_ARCANE_SPRITE = 990;

static ConsoleVariable<bool> cvar_boulderDisableAutoBreak("/boulder_disable_auto_break", false);
bool boulderCheckIfBlockedExit(Entity* my)
{
	if ( *cvar_boulderDisableAutoBreak )
	{
		return true; // skip check if cvar enabled
	}
	//if ( conductGameChallenges[CONDUCT_MODDED] )
	//{
	//	return true; // ignore for custom maps.
	//}
	if ( !gameModeManager.allowsBoulderBreak() )
	{
		return true; // ignore for custom modes.
	}

	bool playerAlive = false;
	for ( int c = 0; c < MAXPLAYERS; ++c )
	{
		if ( players[c] && players[c]->entity )
		{
			playerAlive = true;
		}
	}
	if ( !playerAlive )
	{
		return true;
	}
	// check if this blocked the exit.
	for ( node_t* node = map.entities->first; node != nullptr; node = node->next )
	{
		Entity* ladder = (Entity*)node->element;
		if ( ladder && (ladder->behavior == &actLadder || ladder->behavior == &actPortal) )
		{
			//if ( ladder->behavior == &actPortal && (ladder->portalNotSecret == 0) )
			//{
			//	continue; // secret exit, don't care.
			//}
			for ( int c = 0; c < MAXPLAYERS; ++c )
			{
				if ( players[c] && players[c]->entity )
				{
					list_t* path = generatePath(players[c]->entity->x / 16, players[c]->entity->y / 16, ladder->x / 16, ladder->y / 16,
						players[c]->entity, ladder, GeneratePathTypes::GENERATE_PATH_BOULDER_BREAK, true);
					if ( path != NULL )
					{
						list_FreeAll(path);
						free(path);
						//messagePlayer(0, "found path to exit");
						return true;
					}
				}
			}
		}
	}
	return false;
}

/*-------------------------------------------------------------------------------

doesEntityStopBoulder

checks which objects the boulder breaks when it hits.

-------------------------------------------------------------------------------*/

bool doesEntityStopBoulder(Entity* entity)
{
	if ( !entity )
	{
		return false;
	}
	if ( entity->behavior == &actGate )
	{
		return true;
	}
	else if ( entity->behavior == &actBoulder )
	{
		return true;
	}
	else if ( entity->behavior == &actChest )
	{
		return true;
	}
	else if ( entity->behavior == &actHeadstone )
	{
		return true;
	}
	else if ( entity->behavior == &actFountain )
	{
		return true;
	}
	else if ( entity->behavior == &actSink )
	{
		return true;
	}
	else if ( entity->behavior == &actStalagCeiling )
	{
		if ( entity->z > -8 )
		{
			// not on ceiling layer
			return true;
		}
	}
	else if ( entity->behavior == &actStalagFloor )
	{
		return true;
	}
	else if ( entity->behavior == &actStalagColumn )
	{
		return true;
	}
	else if ( entity->behavior == &actPedestalBase )
	{
		return true;
	}
	else if ( entity->behavior == &actColumn )
	{
		return true;
	}
	else if ( entity->behavior == &actTeleportShrine /*entity->behavior == &actSpellShrine*/ )
	{
		return true;
	}
	else if ( entity->behavior == &actStatue )
	{
		return true;
	}
	else if ( entity->behavior == &actPowerCrystal || entity->behavior == &actPowerCrystalBase )
	{
		return true;
	}
	else if ( entity->behavior == &actPistonBase || entity->behavior == &actPistonCam )
	{
		return true;
	}
	else if ( entity->behavior == &actColliderDecoration && entity->colliderHasCollision != 0 && !(entity->isColliderWeakToBoulders()) )
	{
		return true;
	}
	else if ( entity->behavior == &::actDaedalusShrine )
	{
		return true;
	}
	else if ( entity->behavior == &::actAssistShrine )
	{
		return true;
	}
	return false;
}

/*-------------------------------------------------------------------------------

	boulderCheckAgainstEntity

	causes the boulder given in my to crush the object given in entity
	or vice versa

-------------------------------------------------------------------------------*/

int boulderCheckAgainstEntity(Entity* my, Entity* entity, bool ignoreInsideEntity)
{
	if (!my || !entity)
	{
		return 0;
	}

	if ( entity->behavior == &actPlayer || entity->behavior == &actMonster )
	{
		if ( entity->behavior == &actMonster && entity->isUntargetableBat() && my->z > -2.0 ) // boulder doesnt kill when not in air
		{
			return 0;
		}
		if ( ignoreInsideEntity || entityInsideEntity( my, entity ) )
		{
			Stat* stats = entity->getStats();
			if ( stats )
			{
				if ( entity->behavior == &actPlayer )
				{
					Uint32 color = makeColorRGB(255, 0, 0);
					messagePlayerColor(entity->skill[2], MESSAGE_STATUS, color, Language::get(455));
					if ( players[entity->skill[2]]->isLocalPlayer() )
					{
						cameravars[entity->skill[2]].shakex += .1;
						cameravars[entity->skill[2]].shakey += 10;
					}
					else
					{
						if ( entity->skill[2] > 0 )
						{
							strcpy((char*)net_packet->data, "SHAK");
							net_packet->data[4] = 10; // turns into .1
							net_packet->data[5] = 10;
							net_packet->address.host = net_clients[entity->skill[2] - 1].host;
							net_packet->address.port = net_clients[entity->skill[2] - 1].port;
							net_packet->len = 6;
							sendPacketSafe(net_sock, -1, net_packet, entity->skill[2] - 1);
						}
					}
				}
				playSoundEntity(my, 181, 128);
				playSoundEntity(entity, 28, 64);
				Entity* gib = spawnGib(entity);

				int damage = 80;
				if ( my->sprite == BOULDER_LAVA_SPRITE
					|| my->sprite == BOULDER_ARCANE_SPRITE )
				{
					damage = 50;
				}

				int trapResist = entity->getEntityBonusTrapResist();
				if ( trapResist != 0 )
				{
					real_t mult = std::max(0.0, 1.0 - (trapResist / 100.0));
					damage *= mult;
				}

				if ( stats->helmet )
				{
					bool shapeshifted = (entity->behavior == &actPlayer && entity->effectShapeshift != NOTHING);

					if ( !shapeshifted 
						&& (stats->helmet->type == HELM_MINING || stats->helmet->type == HAT_TOPHAT) )
					{
						if ( stats->helmet->type == HAT_TOPHAT )
						{
							bool cursedItemIsBuff = shouldInvertEquipmentBeatitude(stats);
							if ( stats->helmet->beatitude >= 0 || cursedItemIsBuff )
							{
								if ( stats->HP <= damage )
								{
									// saved us
									steamAchievementEntity(entity, "BARONY_ACH_CRUMPLE_ZONES");
								}
								damage = 0;
							}
							stats->helmet->status = BROKEN;
						}
						else if ( stats->helmet->type == HELM_MINING )
						{
							real_t mult = 0.5;
							bool cursedItemIsBuff = shouldInvertEquipmentBeatitude(stats);
							if ( stats->helmet->beatitude >= 0 || cursedItemIsBuff )
							{
								mult -= 0.25 * abs(stats->helmet->beatitude);
								mult = std::max(0.0, mult);
							}
							else
							{
								mult = 1.0;
								mult += 0.25 * abs(stats->helmet->beatitude);
							}

							if ( stats->HP <= damage )
							{
								// saved us
								if ( stats->HP > (damage * mult) )
								{
									steamAchievementEntity(entity, "BARONY_ACH_CRUMPLE_ZONES");
								}
							}
							damage *= mult;
							if ( stats->helmet->status > BROKEN )
							{
								stats->helmet->status = (Status)((int)stats->helmet->status - 1);
							}
						}

						playSoundEntity(entity, 76, 64);

						if ( entity->behavior == &actPlayer )
						{
							int player = entity->skill[2];
							if ( stats->helmet->status > BROKEN )
							{
								messagePlayer(player, MESSAGE_EQUIPMENT, Language::get(681), stats->helmet->getName());
							}
							else
							{
								messagePlayer(player, MESSAGE_EQUIPMENT, Language::get(682), stats->helmet->getName());
							}

							if ( multiplayer == SERVER && player > 0 && !players[player]->isLocalPlayer() )
							{
								strcpy((char*)net_packet->data, "ARMR");
								net_packet->data[4] = 0;
								net_packet->data[5] = stats->helmet->status;
								net_packet->address.host = net_clients[player - 1].host;
								net_packet->address.port = net_clients[player - 1].port;
								net_packet->len = 6;
								sendPacketSafe(net_sock, -1, net_packet, player - 1);
							}
						}
					}
				}

				Sint32 oldHP = stats->HP;
				if ( my->sprite == BOULDER_LAVA_SPRITE )
				{
					entity->modHP(-damage);
					if ( entity->behavior == &actPlayer && stats->HP < oldHP )
					{
						Compendium_t::Events_t::eventUpdateWorld(entity->skill[2], Compendium_t::CPDM_TRAP_DAMAGE, "brimstone boulder", oldHP - stats->HP);
					}
					entity->setObituary(Language::get(3898));
					stats->killer = KilledBy::BOULDER;
				}
				else if ( my->sprite == BOULDER_ARCANE_SPRITE )
				{
					entity->modHP(-damage);
					if ( entity->behavior == &actPlayer && stats->HP < oldHP )
					{
						Compendium_t::Events_t::eventUpdateWorld(entity->skill[2], Compendium_t::CPDM_TRAP_DAMAGE, "boulder trap", oldHP - stats->HP);
					}
					entity->setObituary(Language::get(3899));
					stats->killer = KilledBy::BOULDER;
				}
				else
				{
					entity->modHP(-damage);
					if ( entity->behavior == &actPlayer && stats->HP < oldHP )
					{
						Compendium_t::Events_t::eventUpdateWorld(entity->skill[2], Compendium_t::CPDM_TRAP_DAMAGE, "boulder trap", oldHP - stats->HP);
					}
					entity->setObituary(Language::get(1505));
					stats->killer = KilledBy::BOULDER;
				}

				bool lifeSaving = (stats->HP <= 0 && stats->amulet && stats->amulet->type == AMULET_LIFESAVING);
				if ( entity->behavior == &actPlayer )
				{
					if ( stats->HP <= 0 )
					{
						if ( stats->type == AUTOMATON )
						{
							entity->playerAutomatonDeathCounter = TICKS_PER_SECOND * 5; // set the death timer to immediately pop for players.
						}
						steamAchievementClient(entity->skill[2], "BARONY_ACH_THROW_ME_THE_WHIP");
						if ( BOULDER_PLAYERPUSHED >= 0 && entity->skill[2] != BOULDER_PLAYERPUSHED )
						{
							steamAchievementClient(BOULDER_PLAYERPUSHED, "BARONY_ACH_MOVED_ITSELF");
						}

						if ( my->sprite == BOULDER_LAVA_SPRITE )
						{
							Compendium_t::Events_t::eventUpdateWorld(entity->skill[2], Compendium_t::CPDM_TRAP_KILLED_BY, "brimstone boulder", 1);
						}
						else
						{
							Compendium_t::Events_t::eventUpdateWorld(entity->skill[2], Compendium_t::CPDM_TRAP_KILLED_BY, "boulder trap", 1);
						}
						achievementObserver.updateGlobalStat(STEAM_GSTAT_BOULDER_DEATHS, entity->skill[2]);
					}
				}
				if ( BOULDER_PLAYERPUSHED >= 0 && oldHP > 0 && stats->HP <= 0 )
				{
					Compendium_t::Events_t::eventUpdateWorld(BOULDER_PLAYERPUSHED, Compendium_t::CPDM_COMBAT_MASONRY_BOULDERS, "masons guild", 1);
				}

				if ( !lifeSaving )
				{
					if ( stats->HP <= 0 && entity->behavior == &actPlayer 
						&& ((stats->playerRace == RACE_SKELETON && stats->stat_appearance == 0) || stats->type == SKELETON) )
					{
						if ( stats->MP >= 75 )
						{
							lifeSaving = true;
						}
						else
						{
							int spellCost = getCostOfSpell(&spell_summon, entity);
							int numSummonedAllies = 0;
							int firstManaToRefund = 0;
							int secondManaToRefund = 0;
							for ( node_t* node = stats->FOLLOWERS.first; node != nullptr; node = node->next )
							{
								Uint32* c = (Uint32*)node->element;
								Entity* mySummon = nullptr;
								if ( c )
								{
									mySummon = uidToEntity(*c);
								}
								if ( mySummon && mySummon->monsterAllySummonRank != 0 )
								{
									Stat* mySummonStats = mySummon->getStats();
									if ( mySummonStats )
									{
										int mp = (mySummonStats->MAXMP * (mySummonStats->HP / static_cast<float>(mySummonStats->MAXHP)));
										if ( numSummonedAllies == 0 )
										{
											firstManaToRefund += std::min(spellCost, static_cast<int>((mp / static_cast<float>(mySummonStats->MAXMP)) * spellCost)); // MP to restore
											++numSummonedAllies;
										}
										else if ( numSummonedAllies == 1 )
										{
											mySummon->setMP(mySummonStats->MAXMP * (mySummonStats->HP / static_cast<float>(mySummonStats->MAXHP)));
											secondManaToRefund += std::min(spellCost, static_cast<int>((mp / static_cast<float>(mySummonStats->MAXMP)) * spellCost)); // MP to restore
											++numSummonedAllies;
											break;
										}
									}
								}
							}

							if ( numSummonedAllies == 2 )
							{
								firstManaToRefund /= 2;
								secondManaToRefund /= 2;
							}

							int manaTotal = stats->MP + firstManaToRefund + secondManaToRefund;
							if ( manaTotal >= 75 )
							{
								lifeSaving = true;
							}
						}
					}
				}

				if ( stats->HP > 0 || lifeSaving )
				{
					// spawn several rock items
					int i = 8 + local_rng.rand() % 4;
					if ( my->sprite == BOULDER_LAVA_SPRITE || my->sprite == BOULDER_ARCANE_SPRITE )
					{
						i = 0;
					}
					int c;
					for ( c = 0; c < i; c++ )
					{
						Entity* entity = newEntity(-1, 1, map.entities, nullptr); //Rock/item entity.
						entity->flags[INVISIBLE] = true;
						entity->flags[UPDATENEEDED] = true;
						entity->x = my->x - 4 + local_rng.rand() % 8;
						entity->y = my->y - 4 + local_rng.rand() % 8;
						entity->z = -6 + local_rng.rand() % 12;
						entity->sizex = 4;
						entity->sizey = 4;
						entity->yaw = local_rng.rand() % 360 * PI / 180;
						entity->vel_x = (local_rng.rand() % 20 - 10) / 10.0;
						entity->vel_y = (local_rng.rand() % 20 - 10) / 10.0;
						entity->vel_z = -.25 - (local_rng.rand() % 5) / 10.0;
						entity->flags[PASSABLE] = true;
						entity->behavior = &actItem;
						entity->flags[USERFLAG1] = true; // no collision: helps performance
						entity->skill[10] = GEM_ROCK;    // type
						entity->skill[11] = WORN;        // status
						entity->skill[12] = 0;           // beatitude
						entity->skill[13] = 1;           // count
						entity->skill[14] = 0;           // appearance
						entity->skill[15] = false;       // identified
					}

					double ox = my->x;
					double oy = my->y;

					boulderLavaOrArcaneOnDestroy(my, my->sprite, entity);

					auto& rng = my->entity_rng ? *my->entity_rng : local_rng;
					Uint32 monsterSpawnSeed = rng.getU32();

					// destroy the boulder
					playSoundEntity(my, 67, 128);
					list_RemoveNode(my->mynode);

					// on sokoban, destroying boulders spawns scorpions / insectoids
					if ( !strcmp(map.name, "Sokoban") )
					{
						Entity* monster = nullptr;
						if ( local_rng.rand() % 2 == 0 )
						{
							monster = summonMonster(INSECTOID, ox, oy);
						}
						else
						{
							monster = summonMonster(SCORPION, ox, oy);
						}
						if ( monster )
						{
							monster->seedEntityRNG(monsterSpawnSeed);
							for ( int c = 0; c < MAXPLAYERS; c++ )
							{
								Uint32 color = makeColorRGB(255, 128, 0);
								messagePlayerColor(c, MESSAGE_HINT, color, Language::get(406));
							}
						}
						boulderSokobanOnDestroy(false);
					}

					return 1;
				}
				else
				{
					if ( Entity* leader = entity->monsterAllyGetPlayerLeader() )
					{
						if ( my->sprite == BOULDER_LAVA_SPRITE )
						{
							Compendium_t::Events_t::eventUpdateWorld(entity->monsterAllyIndex, Compendium_t::CPDM_TRAP_FOLLOWERS_KILLED, "brimstone boulder", 1);
						}
						else
						{
							Compendium_t::Events_t::eventUpdateWorld(entity->monsterAllyIndex, Compendium_t::CPDM_TRAP_FOLLOWERS_KILLED, "boulder trap", 1);
						}
						if ( stats->type == GYROBOT )
						{
							Compendium_t::Events_t::eventUpdate(entity->monsterAllyIndex, Compendium_t::CPDM_GYROBOT_BOULDERS, TOOL_GYROBOT, 1);
							real_t tangent = atan2(leader->y - entity->y, leader->x - entity->x);
							Entity* ohitentity = hit.entity;
							lineTraceTarget(entity, entity->x, entity->y, tangent, 1024, 0, false, leader);
							if ( hit.entity == leader )
							{
								steamAchievementClient(entity->monsterAllyIndex, "BARONY_ACH_GOODNIGHT_SWEET_PRINCE");
							}
							hit.entity = ohitentity;
						}
					}
					if ( gibtype[stats->type] > 0 )
					{
						if ( gibtype[stats->type] == 1 )
						{
							BOULDER_SPAWNBLOOD = 203; //Blood entity.
						}
						else if ( gibtype[stats->type] == 2 )
						{
							BOULDER_SPAWNBLOOD = 213; //Blood entity.
						}
						else if ( gibtype[stats->type] == 4 )
						{
							BOULDER_SPAWNBLOOD = 682; //Blood entity.
						}
						if ( BOULDER_SPAWNBLOOD > 0 )
						{
							BOULDER_BLOODTIME = TICKS_PER_SECOND * 3;
						}
					}
				}
			}
		}
	}
	else if ( doesEntityStopBoulder(entity) )
	{
		if ( !entity->flags[PASSABLE] )
		{
			if ( ignoreInsideEntity || entityInsideEntity( my, entity ) )
			{
				// stop the boulder
				BOULDER_STOPPED = 1;
				my->vel_x = 0.0; // TODOR: Anywhere this is could possible be changed to be a static 'if( BOULDER_ROLLING == 0 ) { vel = 0 }' instead of duplicating code everywhere
				my->vel_y = 0.0;
				BOULDER_ROLLING = 0;
				playSoundEntity(my, 181, 128);
				if ( my->flags[PASSABLE] )
				{
					my->flags[PASSABLE] = false;
					if ( multiplayer == SERVER )
					{
						serverUpdateEntityFlag(my, PASSABLE);
					}
				}
			}
		}
	}
	else if ( entity->behavior == &actDoor )
	{
		if ( ignoreInsideEntity || entityInsideEntity( my, entity ) )
		{
			playSoundEntity(entity, 28, 64);
			entity->skill[4] = 0;
			if ( !entity->skill[0] )
			{
				entity->skill[6] = (my->x > entity->x);
			}
			else
			{
				entity->skill[6] = (my->y < entity->y);
			}
			playSoundEntity(my, 181, 128);
		}
	}
	else if ( entity->behavior == &actFurniture )
	{
		if ( ignoreInsideEntity || entityInsideEntity(my, entity) )
		{
			playSoundEntity(entity, 28, 64);
			entity->furnitureHealth = 0;
			playSoundEntity(my, 181, 128);
		}
	}
	else if ( entity->isDamageableCollider() && entity->isColliderWeakToBoulders() )
	{
		if ( ignoreInsideEntity || entityInsideEntity(my, entity) )
		{
			playSoundEntity(entity, 28, 64);
			entity->colliderCurrentHP = 0;
			entity->colliderKillerUid = 0;
			playSoundEntity(my, 181, 128);
		}
	}
	return 0;
}

/*-------------------------------------------------------------------------------

	act*

	The following function describes an entity behavior. The function
	takes a pointer to the entity that uses it as an argument.

-------------------------------------------------------------------------------*/

void actBoulder(Entity* my)
{
	int i;

	if ( multiplayer == CLIENT )
	{
		if ( my->sprite == 989 ) // boulder_lava.vox
		{
			my->flags[BURNABLE] = true;
		}
		if ( !BOULDER_INIT )
		{
			BOULDER_INIT = 1;
			my->createWorldUITooltip();
		}
		return;
	}
	my->skill[2] = -16; // invokes actBoulder() on clients
	my->flags[UPDATENEEDED] = true;

	bool noground = false;
	int x = std::min<int>(std::max(0, (int)(my->x / 16)), map.width);
	int y = std::min<int>(std::max(0, (int)(my->y / 16)), map.height);
	Uint32 index = y * MAPLAYERS + x * MAPLAYERS * map.height;
	if ( !map.tiles[index] || swimmingtiles[map.tiles[index]] || lavatiles[map.tiles[index]] )
	{
		if ( (swimmingtiles[map.tiles[index]] || lavatiles[map.tiles[index]]) 
			&& (my->sprite == BOULDER_LAVA_SPRITE || my->sprite == BOULDER_ARCANE_SPRITE) )
		{
			// lava/arcane balls, roll over lava.
			noground = false;
		}
		else
		{
			noground = true;
		}
	}

	if ( !BOULDER_INIT )
	{
		BOULDER_LAVA_EXPLODE = -1;
		if ( my->sprite == BOULDER_LAVA_SPRITE )
		{
			if ( local_rng.rand() % 4 == 0 )
			{
				BOULDER_LAVA_EXPLODE = 100 + local_rng.rand() % 150;
			}
		}
		BOULDER_INIT = 1;
		BOULDER_PLAYERPUSHED = -1;
		my->createWorldUITooltip();
	}

	if ( BOULDER_LAVA_EXPLODE > 0 )
	{
		--BOULDER_LAVA_EXPLODE;
		if ( BOULDER_LAVA_EXPLODE == 0 )
		{
			spawnExplosion(my->x, my->y, my->z);
			boulderLavaOrArcaneOnDestroy(my, my->sprite, nullptr);
			for ( int c = 0; c < 8; ++c )
			{
				my->yaw = ((double)c + ((local_rng.rand() % 100) / 100.f)) * (PI * 2) / 8.f;
				castSpell(my->getUID(), &spell_fireball, true, true);
			}
			list_RemoveNode(my->mynode);
			return;
		}
	}

	real_t boulderModifier = 1.0;
	if ( gameModeManager.currentSession.challengeRun.isActive(GameModeManager_t::CurrentSession_t::ChallengeRun_t::CHEVENT_STRONG_TRAPS) )
	{
		boulderModifier = 2.0;
	}

	// gravity
	bool nobounce = true;
	if ( !BOULDER_NOGROUND )
	{
		if ( noground )
		{
			BOULDER_NOGROUND = true;
		}
	}
	if ( my->z < 0 || BOULDER_NOGROUND )
	{
		my->vel_z = std::min<real_t>(my->vel_z + .1, 3.0);
		my->vel_x *= 0.85f;
		my->vel_y *= 0.85f;
		nobounce = true;
		if ( my->z >= 128 )
		{
			list_RemoveNode(my->mynode);
			if ( multiplayer != CLIENT && !strncmp(map.name, "Sokoban", 7) )
			{
				boulderSokobanOnDestroy(true);
			}
			return;
		}
		if ( !BOULDER_NOGROUND )
		{
			if ( my->z >= -8 && fabs(my->vel_z) > 2 )
			{
				std::vector<list_t*> entLists = TileEntityList.getEntitiesWithinRadiusAroundEntity(my, 2);
				for ( std::vector<list_t*>::iterator it = entLists.begin(); it != entLists.end(); ++it )
				{
					list_t* currentList = *it;
					node_t* node;
					for ( node = currentList->first; node != nullptr; node = node->next )
					{
						Entity* entity = (Entity*)node->element;
						if ( entity == my )
						{
							continue;
						}
						if ( boulderCheckAgainstEntity(my, entity, false) )
						{
							return;
						}
						else
						{
							if ( entity->behavior == &actBoulder && entityInsideEntity(my, entity) )
							{
								// destroy this boulder if falling on another boulder.
								Entity* ohitentity = hit.entity;
								hit.entity = my;
								if ( my->sprite == BOULDER_LAVA_SPRITE || my->sprite == BOULDER_ARCANE_SPRITE )
								{
									magicDig(nullptr, nullptr, 0, 1);
								}
								else
								{
									magicDig(nullptr, nullptr, 2, 4);
								}
								hit.entity = ohitentity;
								return;
							}
						}
					}
				}
			}
		}
	}
	else
	{
		if ( fabs(my->vel_z) > 1 )
		{
			playSoundEntity(my, 182, 128);
			my->vel_z = -(my->vel_z / 2) * (1 / boulderModifier);
			for ( int i = 0; i < MAXPLAYERS; ++i )
			{
				if ( players[i]->isLocalPlayer() )
				{
					inputs.addRumbleForHapticType(i, Inputs::HAPTIC_SFX_BOULDER_BOUNCE_VOL, my->getUID());
				}
				else
				{
					inputs.addRumbleRemotePlayer(i, Inputs::HAPTIC_SFX_BOULDER_BOUNCE_VOL, my->getUID());
				}
			}
			nobounce = true;
		}
		else
		{
			if ( my->vel_z )
			{
				playSoundEntity(my, 182, 128);
				for ( int i = 0; i < MAXPLAYERS; ++i )
				{
					if ( players[i]->isLocalPlayer() )
					{
						inputs.addRumbleForHapticType(i, Inputs::HAPTIC_SFX_BOULDER_BOUNCE_VOL, my->getUID());
					}
					else
					{
						inputs.addRumbleRemotePlayer(i, Inputs::HAPTIC_SFX_BOULDER_BOUNCE_VOL, my->getUID());
					}
				}
			}
			my->vel_z = 0;
			nobounce = false;
		}
		my->z = 0;
	}
	my->z += my->vel_z;
	if ( nobounce )
	{
		if ( !my->flags[PASSABLE] )
		{
			my->flags[PASSABLE] = true;
			if ( multiplayer == SERVER )
			{
				serverUpdateEntityFlag(my, PASSABLE);
			}
		}
		if ( !BOULDER_STOPPED )
		{
			my->x += my->vel_x;
			my->y += my->vel_y;
			double dist = sqrt(pow(my->vel_x, 2) + pow(my->vel_y, 2));
			my->pitch += dist * .06;
			my->roll = PI / 2;
		}
	}
	else if ( !BOULDER_STOPPED )
	{
		if ( my->flags[PASSABLE] )
		{
			my->flags[PASSABLE] = false;
			if ( multiplayer == SERVER )
			{
				serverUpdateEntityFlag(my, PASSABLE);
			}
		}

		// horizontal velocity
		my->vel_x += cos(my->yaw) * .1;
		my->vel_y += sin(my->yaw) * .1;
		real_t maxSpeed = 1.5;
		if ( my->sprite == BOULDER_LAVA_SPRITE || my->sprite == BOULDER_ARCANE_SPRITE )
		{
			maxSpeed = 2.5;
		}
		maxSpeed *= boulderModifier;
		if ( my->vel_x > maxSpeed )
		{
			my->vel_x = maxSpeed;
		}
		if ( my->vel_x < -maxSpeed )
		{
			my->vel_x = -maxSpeed;
		}
		if ( my->vel_y > maxSpeed )
		{
			my->vel_y = maxSpeed;
		}
		if ( my->vel_y < -maxSpeed )
		{
			my->vel_y = -maxSpeed;
		}

		/*int x = std::min<int>(std::max<int>(0, (my->x + my->vel_x + 8) / 16), map.width - 1);
		int y = std::min<int>(std::max<int>(0, (my->y + my->vel_y + 8) / 16), map.height - 1);*/
		//int x = std::min<int>(std::max<int>(0, (my->x + my->vel_x * 8) / 16), map.width - 1);
		//int y = std::min<int>(std::max<int>(0, (my->y + my->vel_y * 8) / 16), map.height - 1);

		real_t clipDist = clipMove(&my->x, &my->y, my->vel_x, my->vel_y, my);
		double dist = sqrt(pow(my->vel_x, 2) + pow(my->vel_y, 2));
		if ( clipDist != dist && !hit.entity/*map.tiles[OBSTACLELAYER + y * MAPLAYERS + x * MAPLAYERS * map.height]*/ )
		{
			playSoundEntity(my, 181, 128);
			BOULDER_STOPPED = 1;
			TileEntityList.updateEntity(*my);
			bool foundPathToExit = boulderCheckIfBlockedExit(my);
			
			if ( !foundPathToExit )
			{
				hit.entity = my; // for magicDig

				// spawn luckstone
				Entity* rock = newEntity(-1, 1, map.entities, nullptr); //Rock entity.
				rock->flags[INVISIBLE] = true;
				rock->flags[UPDATENEEDED] = true;
				rock->x = my->x;
				rock->y = my->y;
				rock->z = -6 + local_rng.rand() % 12;
				rock->sizex = 4;
				rock->sizey = 4;
				rock->yaw = local_rng.rand() % 360 * PI / 180;
				rock->vel_z = -.25 - (local_rng.rand() % 5) / 10.0;
				rock->flags[PASSABLE] = true;
				rock->behavior = &actItem;
				rock->flags[USERFLAG1] = true; // no collision: helps performance
				rock->skill[10] = GEM_LUCK;    // type
				rock->skill[11] = WORN;        // status
				rock->skill[12] = 0;           // beatitude
				rock->skill[13] = 1;           // count
				rock->skill[14] = 0;           // appearance
				rock->skill[15] = false;       // identified

				for ( int c = 0; c < MAXPLAYERS; ++c )
				{
					Uint32 color = makeColorRGB(255, 0, 255);
					if ( !client_disconnected[c] )
					{
						messagePlayerColor(c, MESSAGE_HINT, color, Language::get(3401));
					}
				}

				if ( my->sprite == BOULDER_LAVA_SPRITE || my->sprite == BOULDER_ARCANE_SPRITE )
				{
					magicDig(nullptr, nullptr, 0, 1);
				}
				else
				{
					magicDig(nullptr, nullptr, 2, 4);
				}
				hit.entity = nullptr;
				printlog("notice: boulder stopped path to exit, removed.");
				return;
			}
		}
		else
		{
			my->pitch += dist * .06;
			my->roll = PI / 2;

			// crush objects
			if ( dist && !BOULDER_NOGROUND )
			{
				std::vector<list_t*> entLists = TileEntityList.getEntitiesWithinRadiusAroundEntity(my, 2);
				for ( std::vector<list_t*>::iterator it = entLists.begin(); it != entLists.end(); ++it )
				{
					list_t* currentList = *it;
					node_t* node;
					for ( node = currentList->first; node != nullptr; node = node->next )
					{
						Entity* entity = (Entity*)node->element;
						if ( entity == my )
						{
							continue;
						}
						bool wasStopped = (BOULDER_STOPPED == 1);
						if ( clipDist != dist )
						{
							if ( hit.entity )
							{
								if ( boulderCheckAgainstEntity(my, hit.entity, true) )
								{
									return;
								}
								hit.entity = nullptr;
							}
						}
						else
						{
							if ( boulderCheckAgainstEntity(my, entity, false) )
							{
								return;
							}
						}
						if ( BOULDER_STOPPED == 1 && !wasStopped )
						{
							TileEntityList.updateEntity(*my);
							bool foundPathToExit = boulderCheckIfBlockedExit(my);

							if ( !foundPathToExit )
							{
								Entity* ohitentity = hit.entity;
								if ( hit.entity == my )
								{
									ohitentity = nullptr;
								}
								hit.entity = my;

								// spawn luckstone
								Entity* rock = newEntity(-1, 1, map.entities, nullptr); //Rock entity.
								rock->flags[INVISIBLE] = true;
								rock->flags[UPDATENEEDED] = true;
								rock->x = my->x;
								rock->y = my->y;
								rock->z = -6 + local_rng.rand() % 12;
								rock->sizex = 4;
								rock->sizey = 4;
								rock->yaw = local_rng.rand() % 360 * PI / 180;
								rock->vel_z = -.25 - (local_rng.rand() % 5) / 10.0;
								rock->flags[PASSABLE] = true;
								rock->behavior = &actItem;
								rock->flags[USERFLAG1] = true; // no collision: helps performance
								rock->skill[10] = GEM_LUCK;    // type
								rock->skill[11] = WORN;        // status
								rock->skill[12] = 0;           // beatitude
								rock->skill[13] = 1;           // count
								rock->skill[14] = 0;           // appearance
								rock->skill[15] = false;       // identified

								for ( int c = 0; c < MAXPLAYERS; ++c )
								{
									Uint32 color = makeColorRGB(255, 0, 255);
									if ( !client_disconnected[c] )
									{
										messagePlayerColor(c, MESSAGE_HINT, color, Language::get(3401));
									}
								}

								if ( my->sprite == BOULDER_LAVA_SPRITE || my->sprite == BOULDER_ARCANE_SPRITE )
								{
									magicDig(nullptr, nullptr, 0, 1);
								}
								else
								{
									magicDig(nullptr, nullptr, 2, 4);
								}
								hit.entity = ohitentity;
								printlog("notice: boulder stopped path to exit, removed.");
								return;
							}
						}
					}
				}
			}
		}
	}

	// pushing boulders
	if ( BOULDER_STOPPED )
	{
		if ( !BOULDER_ROLLING )
		{
			BOULDER_PLAYERPUSHED = -1;
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if ( selectedEntity[i] == my || client_selected[i] == my )
				{
					if (inrange[i])
					{
                        bool hasRingOfStr = false;
						if ( players[i] && players[i]->entity ) 
						{
                            if ( stats[i]->ring 
								&& stats[i]->ring->type == ItemType::RING_STRENGTH) 
							{
                                hasRingOfStr = true;
                            }
							else if ( stats[i]->gloves 
								&& stats[i]->gloves->type == ItemType::GAUNTLETS_STRENGTH ) 
							{
								hasRingOfStr = true;
							}
							else if ( stats[i]->EFFECTS[EFF_POTION_STR] )
							{
								hasRingOfStr = true;
							}
						}
						if ( !hasRingOfStr )
						{
							messagePlayer(i, MESSAGE_INTERACTION, Language::get(456));
						}
						else
						{
							if (players[i] && players[i]->entity)
							{
								BOULDER_SOUND_ON_PUSH = i + 1;
								BOULDER_ROLLING = 1;
								/*my->x = floor(my->x / 16) * 16 + 8;
								my->y = floor(my->y / 16) * 16 + 8;*/

								BOULDER_DESTX = (int)(my->x / 16) * 16 + 8;
								BOULDER_DESTY = (int)(my->y / 16) * 16 + 8;

								real_t tangent = atan2(players[i]->entity->y - my->y, players[i]->entity->x - my->x);
								while ( tangent >= 2 * PI )
								{
									tangent -= 2 * PI;
								}
								while ( tangent < 0 )
								{
									tangent += 2 * PI;
								}
								real_t angle = tangent * 180.0 / PI;
								if ( (tangent >= PI - PI / 4) && tangent < (PI + PI / 4) )
								{
									BOULDER_ROLLDIR = 0; // east
									//messagePlayer(0, MESSAGE_DEBUG, "GO EAST %.2f", angle);
								}
								else if ( (tangent >= (3 * PI / 2) - PI / 4) && tangent < ((3 * PI / 2) + PI / 4) )
								{
									BOULDER_ROLLDIR = 1; // south
									//messagePlayer(0, MESSAGE_DEBUG, "GO SOUTH %.2f", angle);
								}
								else if ( (tangent >= (3 * PI / 2) + PI / 4) || tangent < (PI / 4) )
								{
									BOULDER_ROLLDIR = 2; // west
									//messagePlayer(0, MESSAGE_DEBUG, "GO WEST %.2f", angle);
								}
								else if ( (tangent >= PI / 4) && tangent < (PI - PI / 4) )
								{
									BOULDER_ROLLDIR = 3; // north
									//messagePlayer(0, MESSAGE_DEBUG, "GO NORTH %.2f", angle);
								}
								//if ( (int)(players[i]->entity->x / 16) < (int)(my->x / 16) )
								//{
								//	BOULDER_ROLLDIR = 0; // east
								//}
								//else if ( (int)(players[i]->entity->y / 16) < (int)(my->y / 16) )
								//{
								//	BOULDER_ROLLDIR = 1; // south
								//}
								//else if ( (int)(players[i]->entity->x / 16) > (int)(my->x / 16) )
								//{
								//	BOULDER_ROLLDIR = 2; // west
								//}
								//else if ( (int)(players[i]->entity->y / 16) > (int)(my->y / 16) )
								//{
								//	BOULDER_ROLLDIR = 3; // north
								//}
								switch ( BOULDER_ROLLDIR )
								{
									case 0:
										BOULDER_DESTX += 16;
										break;
									case 1:
										BOULDER_DESTY += 16;
										break;
									case 2:
										BOULDER_DESTX -= 16;
										break;
									case 3:
										BOULDER_DESTY -= 16;
										break;
								}
								BOULDER_PLAYERPUSHED = i;
							}
						}
					}
				}
			}
		}
		else
		{
			switch ( BOULDER_ROLLDIR )
			{
				case 0:
					my->vel_x = 1;
					my->vel_y = 0;
					break;
				case 1:
					my->vel_x = 0;
					my->vel_y = 1;
					break;
				case 2:
					my->vel_x = -1;
					my->vel_y = 0;
					break;
				case 3:
					my->vel_x = 0;
					my->vel_y = -1;
					break;
			}
			int x = (my->x + my->vel_x * 8) / 16;
			int y = (my->y + my->vel_y * 8) / 16;
			x = std::min<unsigned int>(std::max<int>(0, x), map.width - 1);
			y = std::min<unsigned int>(std::max<int>(0, y), map.height - 1);
			if ( map.tiles[OBSTACLELAYER + y * MAPLAYERS + x * MAPLAYERS * map.height] )
			{
				my->vel_x = 0.0;
				my->vel_y = 0.0;
				BOULDER_ROLLING = 0;
				if ( BOULDER_SOUND_ON_PUSH > 0 )
				{
					messagePlayer(BOULDER_SOUND_ON_PUSH - 1, MESSAGE_HINT, Language::get(3974));
					BOULDER_SOUND_ON_PUSH = 0;
				}
			}
			else
			{
				real_t clipDist = clipMove(&my->x, &my->y, my->vel_x, my->vel_y, my);
				/*my->x += my->vel_x;
				my->y += my->vel_y;*/
				double dist = sqrt(pow(my->vel_x, 2) + pow(my->vel_y, 2));
				if ( clipDist > 0.001 )
				{
					my->pitch += dist * .06;
				}

				if ( BOULDER_ROLLDIR == 0 )
				{
					if ( my->x >= BOULDER_DESTX )
					{
						my->x = BOULDER_DESTX;
						my->vel_x = 0.0;
						my->vel_y = 0.0;
						BOULDER_ROLLING = 0;
					}
				}
				else if ( BOULDER_ROLLDIR == 1 )
				{
					if ( my->y >= BOULDER_DESTY )
					{
						my->y = BOULDER_DESTY;
						my->vel_x = 0.0;
						my->vel_y = 0.0;
						BOULDER_ROLLING = 0;
					}
				}
				else if ( BOULDER_ROLLDIR == 2 )
				{
					if ( my->x <= BOULDER_DESTX )
					{
						my->x = BOULDER_DESTX;
						my->vel_x = 0.0;
						my->vel_y = 0.0;
						BOULDER_ROLLING = 0;
					}
				}
				else if ( BOULDER_ROLLDIR == 3 )
				{
					if ( my->y <= BOULDER_DESTY )
					{
						my->y = BOULDER_DESTY;
						my->vel_x = 0.0;
						my->vel_y = 0.0;
						BOULDER_ROLLING = 0;
					}
				}
				double dir = my->yaw - BOULDER_ROLLDIR * PI / 2;
				while ( dir >= PI )
				{
					dir -= PI * 2;
				}
				while ( dir < -PI )
				{
					dir += PI * 2;
				}

				if ( clipDist > 0.001 )
				{
					my->yaw -= dir / 16;
				}

				while ( my->yaw < 0 )
				{
					my->yaw += 2 * PI;
				}
				while ( my->yaw >= 2 * PI )
				{
					my->yaw -= 2 * PI;
				}

				// crush objects
				if ( !BOULDER_NOGROUND )
				{
					if ( clipDist != dist )
					{
						if ( BOULDER_SOUND_ON_PUSH > 0 )
						{
							if ( clipDist > 0.001 )
							{
								playSoundEntity(my, 151, 128);
							}
							else
							{
								messagePlayer(BOULDER_SOUND_ON_PUSH - 1, MESSAGE_HINT, Language::get(3974));
							}
							BOULDER_SOUND_ON_PUSH = 0;
						}
						if ( hit.entity && boulderCheckAgainstEntity(my, hit.entity, true) )
						{
							return;
						}
					}
					/*std::vector<list_t*> entLists = TileEntityList.getEntitiesWithinRadiusAroundEntity(my, 2);
					for ( std::vector<list_t*>::iterator it = entLists.begin(); it != entLists.end(); ++it )
					{
						list_t* currentList = *it;
						node_t* node;
						for ( node = currentList->first; node != nullptr; node = node->next )
						{
							Entity* entity = (Entity*)node->element;
							if ( entity == my )
							{
								continue;
							}
							if ( boulderCheckAgainstEntity(my, entity) )
							{
								return;
							}
						}
					}*/
				}

				if ( BOULDER_SOUND_ON_PUSH > 0 )
				{
					playSoundEntity(my, 151, 128);
					for ( int i = 0; i < MAXPLAYERS; ++i )
					{
						if ( players[i]->isLocalPlayer() )
						{
							inputs.addRumbleForHapticType(i, Inputs::HAPTIC_SFX_BOULDER_ROLL_LOW_VOL, my->getUID());
						}
						else
						{
							inputs.addRumbleRemotePlayer(i, Inputs::HAPTIC_SFX_BOULDER_ROLL_LOW_VOL, my->getUID());
						}
					}

					Compendium_t::Events_t::eventUpdateWorld(BOULDER_SOUND_ON_PUSH - 1, Compendium_t::CPDM_BOULDERS_PUSHED, "boulder trap", 1);
					BOULDER_SOUND_ON_PUSH = 0;
				}
			}
		}
	}

	// wrap around angles
	while ( my->pitch >= PI * 2 )
	{
		my->pitch -= PI * 2;
	}
	while ( my->pitch < 0 )
	{
		my->pitch += PI * 2;
	}
	while ( my->roll >= PI * 2 )
	{
		my->roll -= PI * 2;
	}
	while ( my->roll < 0 )
	{
		my->roll += PI * 2;
	}

	// rolling sound
	if ( !BOULDER_STOPPED && (fabs(my->vel_x) > 0 || fabs(my->vel_y) > 0) )
	{
		BOULDER_AMBIENCE++;
		if ( !strncmp(map.name, "Hell Boss", 9) )
		{
			if ( BOULDER_AMBIENCE >= TICKS_PER_SECOND / 2)
			{
				BOULDER_AMBIENCE = 0;
				playSoundEntity(my, 151, 64);
			}
		}
		else if ( BOULDER_AMBIENCE >= TICKS_PER_SECOND / 3 )
		{
			BOULDER_AMBIENCE = 0;
			playSoundEntity(my, 151, 128);
			for ( int i = 0; i < MAXPLAYERS; ++i )
			{
				if ( players[i]->isLocalPlayer() )
				{
					inputs.addRumbleForHapticType(i, Inputs::HAPTIC_SFX_BOULDER_ROLL_HIGH_VOL, my->getUID());
				}
				else
				{
					inputs.addRumbleRemotePlayer(i, Inputs::HAPTIC_SFX_BOULDER_ROLL_HIGH_VOL, my->getUID());
				}
			}
		}

		if ( my->sprite == BOULDER_LAVA_SPRITE )
		{
			if ( !my->flags[BURNING] && BOULDER_LAVA_EXPLODE >= 0 )
			{
				my->flags[BURNABLE] = true;
				my->flags[BURNING] = true;
				serverUpdateEntityFlag(my, BURNING);
			}
		}
	}
	if ( (!BOULDER_STOPPED || BOULDER_ROLLING) && (fabs(my->vel_x) > 0 || fabs(my->vel_y) > 0) )
	{
		if ( multiplayer != CLIENT && map.tiles[static_cast<int>(my->y / 16) * MAPLAYERS + static_cast<int>(my->x / 16) * MAPLAYERS * map.height] )
		{
			// spawn blood only if there's a floor!
			if ( BOULDER_SPAWNBLOOD != 0 && BOULDER_BLOODTIME > 0 )
			{
				int rate = 20;
				if ( BOULDER_BLOODTIME > 2 * TICKS_PER_SECOND )
				{
					rate = 7;
				}
				else if ( BOULDER_BLOODTIME > 1 * TICKS_PER_SECOND )
				{
					rate = 15;
				}
				if ( spawn_blood && my->ticks % (rate + local_rng.rand() % 3) == 0 )
				{
					Entity* blood = newEntity(BOULDER_SPAWNBLOOD, 1, map.entities, nullptr); //Gib entity.
					if ( blood != NULL )
					{
						blood->x = my->x - 4 + local_rng.rand() % 9;
						blood->y = my->y - 4 + local_rng.rand() % 9;
						blood->z = 8.0 + (local_rng.rand() % 20) / 100.0;
						blood->parent = my->getUID();
						blood->sizex = 2;
						blood->sizey = 2;
						int randomScale = local_rng.rand() % 10;
						blood->scalex = (100 - randomScale) / 100.f;
						blood->scaley = blood->scalex;
						blood->yaw = (local_rng.rand() % 360) * PI / 180.0;
						blood->flags[UPDATENEEDED] = true;
						blood->flags[PASSABLE] = true;
					}
				}
			}
		}
	}
	if ( BOULDER_BLOODTIME > 0 )
	{
		--BOULDER_BLOODTIME;
	}
}

#define BOULDERTRAP_FIRED my->skill[0]
#define BOULDERTRAP_AMBIENCE my->skill[6]
#define BOULDERTRAPHOLE_INIT my->skill[0]

void actBoulderTrapHole(Entity* my)
{
	if ( multiplayer == CLIENT )
	{
		if ( !BOULDERTRAPHOLE_INIT )
		{
			BOULDERTRAPHOLE_INIT = 1;
			my->createWorldUITooltip();
		}
		return;
	}
	if ( !my ) { return; }

	if ( !BOULDERTRAPHOLE_INIT )
	{
		BOULDERTRAPHOLE_INIT = 1;
		my->createWorldUITooltip();
	}

	if ( my->z > -11.0 && my->z < -10 )
	{
		// in ceiling, delete self if ceiling no longer exists
		int x = ((int)(my->x)) >> 4;
		int y = ((int)(my->y)) >> 4;
		if ( !map.tiles[(MAPLAYERS - 1) + y * MAPLAYERS + x * MAPLAYERS * map.height] )
		{
			list_RemoveNode(my->mynode);
			return;
		}
	}
}

void actBoulderTrap(Entity* my)
{
	int x, y;
	int c;

#ifdef USE_FMOD
		if ( BOULDERTRAP_AMBIENCE == 0 )
		{
			BOULDERTRAP_AMBIENCE--;
			my->stopEntitySound();
			my->entity_sound = playSoundEntityLocal(my, 149, 64);
		}
		if ( my->entity_sound )
		{
			bool playing = false;
			my->entity_sound->isPlaying(&playing);
			if ( !playing )
			{
				my->entity_sound = nullptr;
			}
		}
#else
		BOULDERTRAP_AMBIENCE--;
		if ( BOULDERTRAP_AMBIENCE <= 0 )
		{
			BOULDERTRAP_AMBIENCE = TICKS_PER_SECOND * 30;
			playSoundEntityLocal(my, 149, 64);
		}
#endif

	if ( !my->skill[28] )
	{
		return;
	}

	// received on signal
	if ( my->skill[28] == 2 )
	{
		if ( !BOULDERTRAP_FIRED )
		{
			int foundTrapdoor = -1;
			BOULDERTRAP_FIRED = 1;
			for ( c = 0; c < 4; c++ )
			{
				if ( my->boulderTrapRocksToSpawn & (1 << c) )
				{
					switch ( c )
					{
						case 0:
							x = 16;
							y = 0;
							break;
						case 1:
							x = 0;
							y = 16;
							break;
						case 2:
							x = -16;
							y = 0;
							break;
						case 3:
							x = 0;
							y = -16;
							break;
					}
					x = ((int)(x + my->x)) >> 4;
					y = ((int)(y + my->y)) >> 4;
					if ( x >= 0 && y >= 0 && x < map.width && y < map.height )
					{
						if ( !map.tiles[OBSTACLELAYER + y * MAPLAYERS + x * MAPLAYERS * map.height] )
						{
							list_t* trapdoors = TileEntityList.getTileList(x, y);
							for ( node_t* trapNode = trapdoors->first; trapNode != nullptr; trapNode = trapNode->next )
							{
								Entity* trapEntity = (Entity*)trapNode->element;
								if ( trapEntity && trapEntity->sprite == 252 && trapEntity->z <= -10 )
								{
									foundTrapdoor = c;
									break;
								}
							}
							if ( foundTrapdoor == c )
							{
								Entity* entity = newEntity(245, 1, map.entities, nullptr); // boulder
								entity->parent = my->getUID();
								entity->x = (x << 4) + 8;
								entity->y = (y << 4) + 8;
								entity->z = -64;
								entity->yaw = c * (PI / 2.f);
								entity->sizex = 7;
								entity->sizey = 7;
								if ( checkObstacle(entity->x + cos(entity->yaw) * 16, entity->y + sin(entity->yaw) * 16, entity, NULL) )
								{
									entity->yaw += PI * (local_rng.rand() % 2) - PI / 2;
									if ( entity->yaw >= PI * 2 )
									{
										entity->yaw -= PI * 2;
									}
									else if ( entity->yaw < 0 )
									{
										entity->yaw += PI * 2;
									}
								}
								entity->behavior = &actBoulder;
								entity->flags[UPDATENEEDED] = true;
								entity->flags[PASSABLE] = true;
							}
						}
					}
				}
			}
			if ( foundTrapdoor >= 0 )
			{
				playSoundEntity(my, 150, 128);
				playSoundPlayer(clientnum, 150, 64);
				for ( c = 0; c < MAXPLAYERS; c++ )
				{
					if ( players[c]->isLocalPlayer() )
					{
						inputs.addRumbleForHapticType(c, Inputs::HAPTIC_SFX_BOULDER_LAUNCH_VOL, my->getUID());
					}
					else
					{
						playSoundPlayer(c, 150, 64);
						inputs.addRumbleRemotePlayer(c, Inputs::HAPTIC_SFX_BOULDER_LAUNCH_VOL, my->getUID());
					}
				}
			}
		}
	}
}

void actBoulderTrapEast(Entity* my)
{
	int x, y;
	int c;

#ifdef USE_FMOD
		if ( my->boulderTrapAmbience == 0 )
		{
			my->boulderTrapAmbience--;
			my->stopEntitySound();
			my->entity_sound = playSoundEntityLocal(my, 149, 64);
		}
		if ( my->entity_sound )
		{
			bool playing = false;
			my->entity_sound->isPlaying(&playing);
			if ( !playing )
			{
				my->entity_sound = nullptr;
			}
		}
#else
		my->boulderTrapAmbience--;
		if ( my->boulderTrapAmbience <= 0 )
		{
			my->boulderTrapAmbience = TICKS_PER_SECOND * 30;
			playSoundEntityLocal(my, 149, 64);
		}
#endif

	if ( my->boulderTrapRefireCounter > 0 )
	{
		--my->boulderTrapRefireCounter;
		if ( my->boulderTrapRefireCounter <= 0 )
		{
			my->boulderTrapFired = 0;
			my->boulderTrapRefireCounter = 0;
		}
	}

	if ( !my->skill[28] )
	{
		return;
	}

	// received on signal
	if ( my->skill[28] == 2 )
	{
		if ( !my->boulderTrapFired )
		{
			if ( my->boulderTrapPreDelay > 0 )
			{
				--my->boulderTrapPreDelay;
				return;
			}
			playSoundEntity(my, 150, 128);
			playSoundPlayer(clientnum, 150, 64);
			for ( c = 0; c < MAXPLAYERS; c++ )
			{
				if ( !players[c]->isLocalPlayer() )
				{
					playSoundPlayer(c, 150, 64);
				}
			}
			my->boulderTrapFired = 1;

			c = 0; // direction
			x = ((int)(my->x)) >> 4;
			y = ((int)(my->y)) >> 4;
			if ( !map.tiles[OBSTACLELAYER + y * MAPLAYERS + x * MAPLAYERS * map.height] )
			{
				Entity* entity = newEntity(245, 1, map.entities, nullptr); // boulder
				entity->parent = my->getUID();
				entity->x = (x << 4) + 8;
				entity->y = (y << 4) + 8;
				entity->z = -64;
				entity->yaw = c * (PI / 2.f);
				entity->sizex = 7;
				entity->sizey = 7;
				/*if ( checkObstacle(entity->x + cos(entity->yaw) * 16, entity->y + sin(entity->yaw) * 16, entity, NULL) )
				{
					entity->yaw += PI * (local_rng.rand() % 2) - PI / 2;
					if ( entity->yaw >= PI * 2 )
					{
						entity->yaw -= PI * 2;
					}
					else if ( entity->yaw < 0 )
					{
						entity->yaw += PI * 2;
					}
				}*/
				entity->behavior = &actBoulder;
				entity->flags[UPDATENEEDED] = true;
				entity->flags[PASSABLE] = true;
			}

			if ( my->boulderTrapRefireAmount > 0 )
			{
				--my->boulderTrapRefireAmount;
				my->boulderTrapRefireCounter = my->boulderTrapRefireDelay * TICKS_PER_SECOND;
			}
			else if ( my->boulderTrapRefireAmount == -1 )
			{
				// infinite boulders.
				my->boulderTrapRefireCounter = my->boulderTrapRefireDelay * TICKS_PER_SECOND;
			}
		}
	}
}

void actBoulderTrapSouth(Entity* my)
{
	int x, y;
	int c;

#ifdef USE_FMOD
		if ( my->boulderTrapAmbience == 0 )
		{
			my->boulderTrapAmbience--;
			my->stopEntitySound();
			my->entity_sound = playSoundEntityLocal(my, 149, 64);
		}
		if ( my->entity_sound )
		{
			bool playing = false;
			my->entity_sound->isPlaying(&playing);
			if ( !playing )
			{
				my->entity_sound = nullptr;
			}
		}
#else
		my->boulderTrapAmbience--;
		if ( my->boulderTrapAmbience <= 0 )
		{
			my->boulderTrapAmbience = TICKS_PER_SECOND * 30;
			playSoundEntityLocal(my, 149, 64);
		}
#endif

	if ( my->boulderTrapRefireCounter > 0 )
	{
		--my->boulderTrapRefireCounter;
		if ( my->boulderTrapRefireCounter <= 0 )
		{
			my->boulderTrapFired = 0;
			my->boulderTrapRefireCounter = 0;
		}
	}

	if ( !my->skill[28] )
	{
		return;
	}

	// received on signal
	if ( my->skill[28] == 2 )
	{
		if ( !my->boulderTrapFired )
		{
			if ( my->boulderTrapPreDelay > 0 )
			{
				--my->boulderTrapPreDelay;
				return;
			}
			playSoundEntity(my, 150, 128);
			playSoundPlayer(clientnum, 150, 64);
			for ( c = 0; c < MAXPLAYERS; c++ )
			{
				if ( !players[c]->isLocalPlayer() )
				{
					playSoundPlayer(c, 150, 64);
				}
			}
			my->boulderTrapFired = 1;

			c = 1; // direction
			x = ((int)(my->x)) >> 4;
			y = ((int)(my->y)) >> 4;
			if ( !map.tiles[OBSTACLELAYER + y * MAPLAYERS + x * MAPLAYERS * map.height] )
			{
				Entity* entity = newEntity(245, 1, map.entities, nullptr); // boulder
				entity->parent = my->getUID();
				entity->x = (x << 4) + 8;
				entity->y = (y << 4) + 8;
				entity->z = -64;
				entity->yaw = c * (PI / 2.f);
				entity->sizex = 7;
				entity->sizey = 7;
				/*if ( checkObstacle(entity->x + cos(entity->yaw) * 16, entity->y + sin(entity->yaw) * 16, entity, NULL) )
				{
					entity->yaw += PI * (local_rng.rand() % 2) - PI / 2;
					if ( entity->yaw >= PI * 2 )
					{
						entity->yaw -= PI * 2;
					}
					else if ( entity->yaw < 0 )
					{
						entity->yaw += PI * 2;
					}
				}*/
				entity->behavior = &actBoulder;
				entity->flags[UPDATENEEDED] = true;
				entity->flags[PASSABLE] = true;
			}

			if ( my->boulderTrapRefireAmount > 0 )
			{
				--my->boulderTrapRefireAmount;
				my->boulderTrapRefireCounter = my->boulderTrapRefireDelay * TICKS_PER_SECOND;
			}
			else if ( my->boulderTrapRefireAmount == -1 )
			{
				// infinite boulders.
				my->boulderTrapRefireCounter = my->boulderTrapRefireDelay * TICKS_PER_SECOND;
			}
		}
	}
}

void actBoulderTrapWest(Entity* my)
{
	int x, y;
	int c;

#ifdef USE_FMOD
		if ( my->boulderTrapAmbience == 0 )
		{
			my->boulderTrapAmbience--;
			my->stopEntitySound();
			my->entity_sound = playSoundEntityLocal(my, 149, 64);
		}
		if ( my->entity_sound )
		{
			bool playing = false;
			my->entity_sound->isPlaying(&playing);
			if ( !playing )
			{
				my->entity_sound = nullptr;
			}
		}
#else
		my->boulderTrapAmbience--;
		if ( my->boulderTrapAmbience <= 0 )
		{
			my->boulderTrapAmbience = TICKS_PER_SECOND * 30;
			playSoundEntityLocal(my, 149, 64);
		}
#endif

	if ( my->boulderTrapRefireCounter > 0 )
	{
		--my->boulderTrapRefireCounter;
		if ( my->boulderTrapRefireCounter <= 0 )
		{
			my->boulderTrapFired = 0;
			my->boulderTrapRefireCounter = 0;
		}
	}

	if ( !my->skill[28] )
	{
		return;
	}

	// received on signal
	if ( my->skill[28] == 2 )
	{
		if ( !my->boulderTrapFired )
		{
			if ( my->boulderTrapPreDelay > 0 )
			{
				--my->boulderTrapPreDelay;
				return;
			}
			playSoundEntity(my, 150, 128);
			playSoundPlayer(clientnum, 150, 64);
			for ( c = 0; c < MAXPLAYERS; c++ )
			{
				if ( !players[c]->isLocalPlayer() )
				{
					playSoundPlayer(c, 150, 64);
				}
			}
			my->boulderTrapFired = 1;

			c = 2; // direction
			x = ((int)(my->x)) >> 4;
			y = ((int)(my->y)) >> 4;
			if ( !map.tiles[OBSTACLELAYER + y * MAPLAYERS + x * MAPLAYERS * map.height] )
			{
				Entity* entity = newEntity(245, 1, map.entities, nullptr); // boulder
				entity->parent = my->getUID();
				entity->x = (x << 4) + 8;
				entity->y = (y << 4) + 8;
				entity->z = -64;
				entity->yaw = c * (PI / 2.f);
				entity->sizex = 7;
				entity->sizey = 7;
				/*if ( checkObstacle(entity->x + cos(entity->yaw) * 16, entity->y + sin(entity->yaw) * 16, entity, NULL) )
				{
					entity->yaw += PI * (local_rng.rand() % 2) - PI / 2;
					if ( entity->yaw >= PI * 2 )
					{
						entity->yaw -= PI * 2;
					}
					else if ( entity->yaw < 0 )
					{
						entity->yaw += PI * 2;
					}
				}*/
				entity->behavior = &actBoulder;
				entity->flags[UPDATENEEDED] = true;
				entity->flags[PASSABLE] = true;
			}

			if ( my->boulderTrapRefireAmount > 0 )
			{
				--my->boulderTrapRefireAmount;
				my->boulderTrapRefireCounter = my->boulderTrapRefireDelay * TICKS_PER_SECOND;
			}
			else if ( my->boulderTrapRefireAmount == -1 )
			{
				// infinite boulders.
				my->boulderTrapRefireCounter = my->boulderTrapRefireDelay * TICKS_PER_SECOND;
			}
		}
	}
}

void actBoulderTrapNorth(Entity* my)
{
	int x, y;
	int c;

#ifdef USE_FMOD
		if ( my->boulderTrapAmbience == 0 )
		{
			my->boulderTrapAmbience--;
			my->stopEntitySound();
			my->entity_sound = playSoundEntityLocal(my, 149, 64);
		}
		if ( my->entity_sound )
		{
			bool playing = false;
			my->entity_sound->isPlaying(&playing);
			if ( !playing )
			{
				my->entity_sound = nullptr;
			}
		}
#else
		my->boulderTrapAmbience--;
		if ( my->boulderTrapAmbience <= 0 )
		{
			my->boulderTrapAmbience = TICKS_PER_SECOND * 30;
			playSoundEntityLocal(my, 149, 64);
		}
#endif

	if ( my->boulderTrapRefireCounter > 0 )
	{
		--my->boulderTrapRefireCounter;
		if ( my->boulderTrapRefireCounter <= 0 )
		{
			my->boulderTrapFired = 0;
			my->boulderTrapRefireCounter = 0;
		}
	}

	if ( !my->skill[28] )
	{
		return;
	}

	// received on signal
	if ( my->skill[28] == 2 )
	{
		if ( !my->boulderTrapFired )
		{
			if ( my->boulderTrapPreDelay > 0 )
			{
				--my->boulderTrapPreDelay;
				return;
			}
			playSoundEntity(my, 150, 128);
			playSoundPlayer(clientnum, 150, 64);
			for ( c = 0; c < MAXPLAYERS; c++ )
			{
				if ( !players[c]->isLocalPlayer() )
				{
					playSoundPlayer(c, 150, 64);
				}
			}
			my->boulderTrapFired = 1;

			c = 3; // direction
			x = ((int)(my->x)) >> 4;
			y = ((int)(my->y)) >> 4;
			if ( !map.tiles[OBSTACLELAYER + y * MAPLAYERS + x * MAPLAYERS * map.height] )
			{
				Entity* entity = newEntity(245, 1, map.entities, nullptr); // boulder
				entity->parent = my->getUID();
				entity->x = (x << 4) + 8;
				entity->y = (y << 4) + 8;
				entity->z = -64;
				entity->yaw = c * (PI / 2.f);
				entity->sizex = 7;
				entity->sizey = 7;
			/*	if ( checkObstacle(entity->x + cos(entity->yaw) * 16, entity->y + sin(entity->yaw) * 16, entity, NULL) )
				{
					entity->yaw += PI * (local_rng.rand() % 2) - PI / 2;
					if ( entity->yaw >= PI * 2 )
					{
						entity->yaw -= PI * 2;
					}
					else if ( entity->yaw < 0 )
					{
						entity->yaw += PI * 2;
					}
				}*/
				entity->behavior = &actBoulder;
				entity->flags[UPDATENEEDED] = true;
				entity->flags[PASSABLE] = true;
			}

			if ( my->boulderTrapRefireAmount > 0 )
			{
				--my->boulderTrapRefireAmount;
				my->boulderTrapRefireCounter = my->boulderTrapRefireDelay * TICKS_PER_SECOND;
			}
			else if ( my->boulderTrapRefireAmount == -1 )
			{
				// infinite boulders.
				my->boulderTrapRefireCounter = my->boulderTrapRefireDelay * TICKS_PER_SECOND;
			}
		}
	}
}

void boulderSokobanOnDestroy(bool pushedOffLedge)
{
	if ( multiplayer == CLIENT || strcmp(map.name, "Sokoban") )
	{
		return; // return for client and if map not sokoban.
	}

	int goldToDestroy = 5 + local_rng.rand() % 4; // 5-8 bags destroy
	bool bouldersAround = false;
	node_t* node = nullptr;

	if ( !pushedOffLedge ) // destroy some gold
	{
		for ( node_t* node = map.entities->first; node != nullptr; )
		{
			Entity* entity = (Entity*)node->element;
			node = node->next;
			if ( entity )
			{
				if ( entity->behavior == &actGoldBag && entity->goldSokoban == 1 && goldToDestroy > 0 )
				{
					if ( entity->mynode )
					{
						list_RemoveNode(entity->mynode);
					}
					--goldToDestroy;
				}
			}
		}
	}

	for ( node_t* node = map.entities->first; node != nullptr; node = node->next )
	{
		Entity* entity = (Entity*)node->element;
		if ( entity )
		{
			if ( !bouldersAround && entity->behavior == &actBoulder )
			{
				bouldersAround = true;
				break;
			}
		}
	}

	if ( !bouldersAround )
	{
		int goldCount = 0;
		Entity* sokobanItemReward = nullptr;
		for ( node = map.entities->first; node != nullptr; node = node->next )
		{
			Entity* entity = (Entity*)node->element;
			if ( entity )
			{
				if ( entity->behavior == &actGoldBag && entity->goldSokoban == 1 )
				{
					++goldCount;
				}
				if ( entity->behavior == &actItem && entity->itemSokobanReward == 1 ) // artifact gloves.
				{
					sokobanItemReward = entity;
				}
			}
		}
		//messagePlayer(0, "Solved it!");
		Uint32 playerAliveTicks = 0;
		for ( int c = 0; c < MAXPLAYERS; c++ )
		{
			if ( players[c] && players[c]->entity )
			{
				playerAliveTicks = std::min((Uint32)0x7FFFFFFF, players[c]->entity->ticks);
				break;
			}
		}
		for ( int c = 0; c < MAXPLAYERS; c++ )
		{
			if ( playerAliveTicks > 0 )
			{
				Compendium_t::Events_t::eventUpdateWorld(c, Compendium_t::CPDM_SOKOBAN_SOLVES, "sokoban", 1);
				Compendium_t::Events_t::eventUpdateWorld(c, Compendium_t::CPDM_SOKOBAN_FASTEST_SOLVE, "sokoban", playerAliveTicks);
			}
			Uint32 color = makeColorRGB(255, 128, 0);
			if ( goldCount >= 39 )
			{
				playSoundPlayer(c, 393, 128);
				messagePlayerColor(c, MESSAGE_HINT, color, Language::get(2969));
				if ( playerAliveTicks > 0 )
				{
					Compendium_t::Events_t::eventUpdateWorld(c, Compendium_t::CPDM_SOKOBAN_PERFECT_SOLVES, "sokoban", 1);
				}
			}
			else
			{
				playSoundPlayer(c, 395, 128);
				if ( goldCount < 25 )
				{
					messagePlayerColor(c, MESSAGE_HINT, color, Language::get(2971)); // less than impressed.
				}
				else
				{
					messagePlayerColor(c, MESSAGE_HINT, color, Language::get(2970)); // mildly entertained.
				}
			}
		}
		if ( goldCount >= 25 && sokobanItemReward )
		{
			sokobanItemReward->flags[INVISIBLE] = false;
			serverUpdateEntityFlag(sokobanItemReward, INVISIBLE);
		}
	}
}

bool Entity::isBoulderSprite()
{
	if ( sprite == 245 || sprite == 989 || sprite == 990 )
	{
		return true;
	}
	return false;
}

void boulderLavaOrArcaneOnDestroy(Entity* my, int sprite, Entity* boulderHitEntity)
{
	if ( !boulderHitEntity && !my )
	{
		return;
	}
	if ( sprite != BOULDER_LAVA_SPRITE && sprite != BOULDER_ARCANE_SPRITE )
	{
		return;
	}

	if ( !boulderHitEntity )
	{
		if ( my )
		{
			if ( sprite == BOULDER_LAVA_SPRITE )
			{
				spawnMagicTower(my, my->x, my->y, SPELL_FIREBALL, nullptr);
			}
			else if ( sprite == BOULDER_ARCANE_SPRITE )
			{
				switch ( local_rng.rand() % 4 )
				{
					case 0:
						spawnMagicTower(my, my->x, my->y, SPELL_LIGHTNING, nullptr);
						break;
					case 1:
						spawnMagicTower(my, my->x, my->y, SPELL_COLD, nullptr);
						break;
					case 2:
						spawnMagicTower(my, my->x, my->y, SPELL_FIREBALL, nullptr);
						break;
					case 3:
						spawnMagicTower(my, my->x, my->y, SPELL_MAGICMISSILE, nullptr);
						break;
					default:
						spawnMagicTower(my, my->x, my->y, SPELL_MAGICMISSILE, nullptr);
						break;
				}
			}
		}
	}
	else
	{
		boulderHitEntity->SetEntityOnFire();
		Stat* stats = boulderHitEntity->getStats();
		if ( stats )
		{
		    stats->killer = KilledBy::BOULDER;
		}
		if ( sprite == BOULDER_LAVA_SPRITE )
		{
		    boulderHitEntity->setObituary(Language::get(3898));
			spawnMagicTower(nullptr, boulderHitEntity->x, boulderHitEntity->y, SPELL_FIREBALL, boulderHitEntity);
		}
		else if ( sprite == BOULDER_ARCANE_SPRITE )
		{
		    boulderHitEntity->setObituary(Language::get(3899));
			switch ( local_rng.rand() % 4 )
			{
				case 0:
					spawnMagicTower(nullptr, boulderHitEntity->x, boulderHitEntity->y, SPELL_LIGHTNING, boulderHitEntity);
					break;
				case 1:
					spawnMagicTower(nullptr, boulderHitEntity->x, boulderHitEntity->y, SPELL_COLD, boulderHitEntity);
					break;
				case 2:
					spawnMagicTower(nullptr, boulderHitEntity->x, boulderHitEntity->y, SPELL_FIREBALL, boulderHitEntity);
					break;
				case 3:
					spawnMagicTower(nullptr, boulderHitEntity->x, boulderHitEntity->y, SPELL_MAGICMISSILE, boulderHitEntity);
					break;
				default:
					spawnMagicTower(nullptr, boulderHitEntity->x, boulderHitEntity->y, SPELL_MAGICMISSILE, boulderHitEntity);
					break;
			}
		}
	}
}
