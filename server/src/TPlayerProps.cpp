#include "IDebug.h"
#include <vector>

#include "TServer.h"
#include "TPlayer.h"
#include "IEnums.h"
#include "IUtil.h"
#include "TMap.h"
#include "TLevel.h"

#define serverlog	server->getServerLog()
#define rclog		server->getRCLog()
extern bool __sendLocal[propscount];
extern int __attrPackets[30];

/*
	TPlayer: Prop-Manipulation
*/
void TPlayer::getProp(CString& buffer, int pPropId) const
{
	switch (pPropId)
	{
		case PLPROP_NICKNAME:
			buffer >> (char)nickName.length() << nickName;
			return;

		case PLPROP_MAXPOWER:
			buffer >> (char)maxPower;
			return;

		case PLPROP_CURPOWER:
			buffer >> (char)(power * 2);
			return;

		case PLPROP_RUPEESCOUNT:
			buffer >> (int)gralatc;
			return;

		case PLPROP_ARROWSCOUNT:
			buffer >> (char)arrowc;
			return;

		case PLPROP_BOMBSCOUNT:
			buffer >> (char)bombc;
			return;

		case PLPROP_GLOVEPOWER:
			buffer >> (char)glovePower;
			return;

		case PLPROP_BOMBPOWER:
			buffer >> (char)bombPower;
			return;

		case PLPROP_SWORDPOWER:
			buffer >> (char)(swordPower + 30) >> (char)swordImg.length() << swordImg;
			return;

		case PLPROP_SHIELDPOWER:
			buffer >> (char)(shieldPower + 10) >> (char)shieldImg.length() << shieldImg;
			return;

		case PLPROP_GANI:
		{
			if (isClient() && versionID < CLVER_2_1)
			{
				if (!bowImage.isEmpty())
					buffer >> (char)(10 + bowImage.length()) << bowImage;
				else
					buffer >> (char)bowPower;
				return;
			}

			buffer >> (char)gani.length() << gani;
			return;
		}

		case PLPROP_HEADGIF:
			buffer >> (char)(headImg.length() + 100) << headImg;
			return;

		case PLPROP_CURCHAT:
			buffer >> (char)chatMsg.length() << chatMsg;
			return;

		case PLPROP_COLORS:
			buffer >> (char)colors[0] >> (char)colors[1] >> (char)colors[2] >> (char)colors[3] >> (char)colors[4];
			return;

		case PLPROP_ID:
			buffer >> (short)id;
			return;

		case PLPROP_X:
			buffer >> (char)(x * 2);
			return;

		case PLPROP_Y:
			buffer >> (char)(y * 2);
			return;

		case PLPROP_Z:
			buffer >> (char)((z + 0.5f) + 50);
			return;

		case PLPROP_SPRITE:
			buffer >> (char)sprite;
			return;

		case PLPROP_STATUS:
			buffer >> (char)status;
			return;

		case PLPROP_CARRYSPRITE:
			buffer >> (char)carrySprite;
			return;

		case PLPROP_CURLEVEL:
		{
			if (isClient())// || type == PLTYPE_AWAIT)
			{
				if (pmap && pmap->getType() == MAPTYPE_GMAP)
					buffer >> (char)pmap->getMapName().length() << pmap->getMapName();
				else
				{
					if (level != 0 && level->isSingleplayer() == true)
						buffer >> (char)(levelName.length() + 13) << levelName << ".singleplayer";
					else
						buffer >> (char)levelName.length() << levelName;
				}
				return;
			}
			else
				buffer >> (char)1 << " ";
			return;
		}

		case PLPROP_HORSEGIF:
			buffer >> (char)horseImg.length() << horseImg;
			return;

		case PLPROP_HORSEBUSHES:
			buffer >> (char)horsec;
			return;

		case PLPROP_EFFECTCOLORS:
			buffer >> (char)0;
			return;

		case PLPROP_CARRYNPC:
			buffer >> (int)carryNpcId;
			return;

		case PLPROP_APCOUNTER:
			buffer >> (short)(apCounter + 1);
			return;

		case PLPROP_MAGICPOINTS:
			buffer >> (char)mp;
			return;

		case PLPROP_KILLSCOUNT:
			buffer >> (int)kills;
			return;

		case PLPROP_DEATHSCOUNT:
			buffer >> (int)deaths;
			return;

		case PLPROP_ONLINESECS:
			buffer >> (int)onlineTime;
			return;

		case PLPROP_IPADDR:
			buffer.writeGInt5(accountIp);
			return;

		case PLPROP_UDPPORT:
			buffer >> (int)udpport;
			return;

		case PLPROP_ALIGNMENT:
			buffer >> (char)ap;
			return;

		case PLPROP_ADDITFLAGS:
			buffer >> (char)additionalFlags;
			return;

		case PLPROP_ACCOUNTNAME:
			buffer >> (char)accountName.length() << accountName;
			return;

		case PLPROP_BODYIMG:
			buffer >> (char)bodyImg.length() << bodyImg;
			return;

		case PLPROP_RATING:
		{
			int temp = (((int)rating & 0xFFF) << 9) | ((int)deviation & 0x1FF);
			buffer >> (int)temp;
			return;
		}

		case PLPROP_ATTACHNPC:
		{
			// Only attach type 0 (NPC) supported.
			buffer >> (char)0 >> (int)attachNPC;
			return;
		}

		// Simplifies login.
		// Manually send prop if you are leaving the level.
		// 1 = join level, 0 = leave level.
		case PLPROP_JOINLEAVELVL:
			buffer >> (char)1;
			return;

		case PLPROP_PCONNECTED:
			//return CString();
			return;

		case PLPROP_PLANGUAGE:
			buffer >> (char)language.length() << language;
			return;

		case PLPROP_PSTATUSMSG:
		{
			//if (id == -1)
			//	break;

			if (statusMsg > server->getStatusList()->size() - 1)
				buffer >> (char)0;
			else
				buffer >> (char)statusMsg;
			return;
		}

		// OS type.
		// Windows: wind
		case PLPROP_OSTYPE:
			buffer >> (char)os.length() << os;
			return;

		// Text codepage.
		// Example: 1252
		case PLPROP_TEXTCODEPAGE:
			buffer.writeGInt(codepage);
			return;

		case PLPROP_X2:
		{
			unsigned short val = abs(x2) << 1;
			if (x2 < 0) val |= 0x0001;
			buffer.writeGShort(val);
			return;
		}

		case PLPROP_Y2:
		{
			unsigned short val = abs(y2) << 1;
			if (y2 < 0) val |= 0x0001;
			buffer.writeGShort((short)val);
			return;
		}

		case PLPROP_Z2:
		{
			unsigned short val = abs(z2) << 1;
			if (z2 < 0) val |= 0x0001;
			buffer.writeGShort(val);
			return;
		}

		case PLPROP_GMAPLEVELX:
			buffer >> (char)gmaplevelx;
			return;

		case PLPROP_GMAPLEVELY:
			buffer >> (char)gmaplevely;
			return;

		// TODO(joey): figure this out. Something to do with guilds? irc-related
		//	(char)(some bitflag for something, uses the first 3 bits im not sure)
		//		okay i tested some flags, 1 removes the channel. 3 adds it. not sure what third bit does.
		case PLPROP_UNKNOWN81:
			//return CString();
			return;

		case PLPROP_COMMUNITYNAME:
			buffer >> (char)communityName.length() << communityName;
			return;

		default:
			break;
	}

	if (inrange(pPropId, 37, 41) || inrange(pPropId, 46, 49) || inrange(pPropId, 54, 74))
	{
		for (unsigned int i = 0; i < sizeof(__attrPackets) / sizeof(int); i++)
		{
			if (__attrPackets[i] == pPropId)
			{
				auto len = std::min(attrList[i].length(), 223);
				buffer >> (char)len << attrList[i].subString(0, len);
				return;
			}
		}
	}
}

void TPlayer::setProps(CString& pPacket, bool pForward, bool pForwardToSelf, TPlayer *rc)
{
	CSettings *settings = server->getSettings();
	CString globalBuff, levelBuff, levelBuff2, selfBuff;
	bool doSignCheck = false;
	bool doTouchTest = false;
	int len = 0;
	bool sentInvalid = false;

	while (pPacket.bytesLeft() > 0)
	{
		unsigned char propId = pPacket.readGUChar();
		
		switch (propId)
		{
			case PLPROP_NICKNAME:
			{
				CString nick = pPacket.readChars(pPacket.readGUChar());

				// Word filter.
				int filter = server->getWordFilter()->apply(this, nick, FILTER_CHECK_NICK);
				if (filter & FILTER_ACTION_WARN)
				{
					if (nickName.isEmpty())
						setNick("unknown");
				}
				else setNick(nick, (rc != nullptr));
				
				globalBuff >> (char)propId << getProp(propId);

				// Send this if the player is located on another server
				// globalBuff >> (char)81;

				if (!pForwardToSelf)
					selfBuff >> (char)propId << getProp(propId);
			}
			break;

			case PLPROP_MAXPOWER:
			{
				auto newMaxPower = pPacket.readGUChar();
				setMaxPower(newMaxPower);
				setPower((float)maxPower);

#ifdef V8NPCSERVER
				levelBuff >> (char)PLPROP_MAXPOWER << getProp(PLPROP_MAXPOWER);
				selfBuff >> (char)PLPROP_MAXPOWER << getProp(PLPROP_MAXPOWER);
#endif
				levelBuff >> (char)PLPROP_CURPOWER << getProp(PLPROP_CURPOWER);
				selfBuff >> (char)PLPROP_CURPOWER << getProp(PLPROP_CURPOWER);
				break;
			}

			case PLPROP_CURPOWER:
			{
				float p = (float)pPacket.readGUChar() / 2.0f;
				if (ap < 40 && p > power) break;
				//if ((status & PLSTATUS_HIDESWORD) != 0)
				//	break;
				setPower(p);
				break;
			}

			case PLPROP_RUPEESCOUNT:
				if (rc != 0)
				{
					if (server->getSettings()->getBool("normaladminscanchangegralats", true) || (rc->isStaff() && rc->hasRight(PLPERM_SETRIGHTS)))
					{
						gralatc = pPacket.readGUInt();
						gralatc = clip(gralatc, 0, 9999999);
					}
					else
						pPacket.readGUInt();

				}
				else
				{
					gralatc = pPacket.readGUInt();
					gralatc = clip(gralatc, 0, 9999999);
				}
			break;

			case PLPROP_ARROWSCOUNT:
				arrowc = pPacket.readGUChar();
				arrowc = clip(arrowc, 0, 99);
			break;

			case PLPROP_BOMBSCOUNT:
				bombc = pPacket.readGUChar();
				bombc = clip(bombc, 0, 99);
			break;

			case PLPROP_GLOVEPOWER:
				glovePower = pPacket.readGUChar();
				glovePower = clip(glovePower, 0, 3);
			break;

			case PLPROP_BOMBPOWER:
				bombPower = pPacket.readGUChar();
				bombPower = clip(bombPower, 0, 3);
			break;

			case PLPROP_SWORDPOWER:
			{
				int sp = pPacket.readGUChar();
				CString img;

				if (sp <= 4)
				{
					sp = clip(sp, 0, settings->getInt("swordlimit", 3));
					img = CString() << "sword" << CString(sp) << (versionID < CLVER_2_1 ? ".gif" : ".png");
				}
				else
				{
					sp -= 30;
					len = pPacket.readGUChar();
					if (len > 0)
					{
						img = pPacket.readChars(len);
						if (!img.isEmpty() && versionID < CLVER_2_1 && getExtension(img).isEmpty())
							img << ".gif";
					}
					else img = "";
				}

				setSwordPower(sp);
				setSwordImage(img);
			}
			break;

			case PLPROP_SHIELDPOWER:
			{
				int sp = pPacket.readGUChar();
				CString img;

				if (sp <= 3)
				{
					sp = clip(sp, 0, settings->getInt("shieldlimit", 3));
					img = CString() << "shield" << CString(sp) << (versionID < CLVER_2_1 ? ".gif" : ".png");
				}
				else
				{
					// This fixes an odd bug with the 1.41 client.
					if (pPacket.bytesLeft() == 0) continue;

					sp -= 10;
					if (sp < 0) break;
					len = pPacket.readGUChar();
					if (len > 0)
					{
						img = pPacket.readChars(len);
						if (!img.isEmpty() && versionID < CLVER_2_1 && getExtension(img).isEmpty())
							img << ".gif";
					}
					else img = "";
				}

				setShieldPower(sp);
				setShieldImage(img);
			}
			break;

			case PLPROP_GANI:
			{
				if (isClient() && versionID < CLVER_2_1)
				{
					int sp = pPacket.readGUChar();
					if (sp < 10)
					{
						bowPower = sp;
						bowImage.clear();
					}
					else
					{
						bowPower = 10;
						sp -= 10;
						if (sp < 0) break;
						bowImage = CString() << pPacket.readChars(sp);
						if (!bowImage.isEmpty() && versionID < CLVER_2_1 && getExtension(bowImage).isEmpty())
							bowImage << ".gif";
					}
					break;
				}

				setGani(pPacket.readChars(pPacket.readGUChar()));
				if (gani == "spin")
				{
					CString nPacket;
					nPacket >> (char)PLO_HITOBJECTS >> (short)id >> (char)swordPower;
					char hx = (char)((x + 1.5f) * 2);
					char hy = (char)((y + 2.0f) * 2);
					server->sendPacketToLevel(CString() << nPacket >> (char)(hx) >> (char)(hy - 4), 0, level, this);
					server->sendPacketToLevel(CString() << nPacket >> (char)(hx) >> (char)(hy + 4), 0, level, this);
					server->sendPacketToLevel(CString() << nPacket >> (char)(hx - 4) >> (char)(hy), 0, level, this);
					server->sendPacketToLevel(CString() << nPacket >> (char)(hx + 4) >> (char)(hy), 0, level, this);
				}
			}
			break;

			case PLPROP_HEADGIF:
			{
				len = pPacket.readGUChar();
				CString img;
				if (len < 100)
				{
					img = CString() << "head" << CString(len) << (versionID < CLVER_2_1 ? ".gif" : ".png");
				}
				else if (len > 100)
				{
					img = pPacket.readChars(len - 100);

					// TODO(joey): We need to check properties for newline, especially if they are sending to other clients
					//	as it causes havoc on the client...
					int check = img.find("\n", 0);
					if (check > 0)
						img = img.readChars(check);

					if (!img.isEmpty() && versionID < CLVER_2_1 && getExtension(img).isEmpty())
						img << ".gif";
				}

				if (len != 100)
				{
					setHeadImage(img);
					globalBuff >> (char)propId << getProp(propId);
				}

				break;
			}

			case PLPROP_CURCHAT:
			{
				len = pPacket.readGUChar();
				chatMsg = pPacket.readChars(std::min(len, 223));
				lastChat = time(0);

				// Try to process the chat.  If it wasn't processed, apply the word filter to it.
				if (!processChat(chatMsg))
				{
					int found = server->getWordFilter()->apply(this, chatMsg, FILTER_CHECK_CHAT);
					if (pForwardToSelf == false && ((found & FILTER_ACTION_REPLACE) || (found & FILTER_ACTION_WARN)))
						selfBuff >> (char)propId << getProp(propId);
				}

#ifdef V8NPCSERVER
				// Send chat to npcs if this wasn't changed by the npcserver
				if (!rc && !chatMsg.isEmpty())
				{
					if (level != nullptr)
						level->sendChatToLevel(this, chatMsg.text());
				}
#endif
			}
			break;

			case PLPROP_COLORS:
				for (unsigned int i = 0; i < sizeof(colors) / sizeof(unsigned char); ++i)
					colors[i] = pPacket.readGUChar();
			break;

			case PLPROP_ID:
				pPacket.readGUShort();
			break;

			case PLPROP_X:
				x = (float)(pPacket.readGUChar() / 2.0f);
				status &= (~PLSTATUS_PAUSED);
				lastMovement = time(0);
				grMovementUpdated = true;
				doTouchTest = true;

				// Let 2.30+ clients see pre-2.30 movement.
				x2 = (int)(x * 16);
				levelBuff2 >> (char)PLPROP_X2 << getProp(PLPROP_X2);
			break;

			case PLPROP_Y:
				y = (float)(pPacket.readGUChar() / 2.0f);
				status &= (~PLSTATUS_PAUSED);
				lastMovement = time(0);
				grMovementUpdated = true;
				doTouchTest = true;

				// Let 2.30+ clients see pre-2.30 movement.
				y2 = (int)(y * 16);
				levelBuff2 >> (char)PLPROP_Y2 << getProp(PLPROP_Y2);

				// Do collision testing.
				doSignCheck = true;
			break;

			case PLPROP_Z:
				z = (float)pPacket.readGUChar() - 50.0f;
				status &= (~PLSTATUS_PAUSED);
				lastMovement = time(0);
				grMovementUpdated = true;
				doTouchTest = true;

				// Let 2.30+ clients see pre-2.30 movement.
				z2 = (int)(z * 16);
				levelBuff2 >> (char)PLPROP_Z2 << getProp(PLPROP_Z2);
			break;

			case PLPROP_SPRITE:
				sprite = pPacket.readGUChar();

				// Do collision testing.
				doSignCheck = true;
			break;

			case PLPROP_STATUS:
			{
				int oldStatus = status;
				status = pPacket.readGUChar();
				//printf("%s: status: %d, oldStatus: %d\n", accountName.text(), status, oldStatus );

				if (id == -1) break;

				// When they come back to life, give them hearts.
				if ((oldStatus & PLSTATUS_DEAD) > 0 && (status & PLSTATUS_DEAD) == 0)
				{
					auto newPower = clip((ap < 20 ? 3 : (ap < 40 ? 5 : maxPower)), 0.5f, maxPower);
					setPower(newPower);

					selfBuff >> (char)PLPROP_CURPOWER >> (char)(power * 2.0f);
					levelBuff >> (char)PLPROP_CURPOWER >> (char)(power * 2.0f);

					if (level != 0 && level->getPlayer(0) == this)
						sendPacket(CString() >> (char)PLO_ISLEADER);
					/*
					// If we are the leader of the level, call warp().  This will fix NPCs not
					// working again after we respawn.
					if (level != 0 && level->getPlayer(0) == this)
						warp(levelName, x, y, time(0));
					*/
				}

				// When they die, increase deaths and make somebody else level leader.
				if ((oldStatus & PLSTATUS_DEAD) == 0 && (status & PLSTATUS_DEAD) > 0)
				{
					if (level->isSparringZone() == false)
					{
						deaths++;
						dropItemsOnDeath();
					}

					// If we are the leader and there are more players on the level, we want to remove
					// ourself from the leader position and tell the new leader that they are the leader.
					if (level->getPlayer(0) == this && level->getPlayer(1) != 0)
					{
						level->removePlayer(this);
						level->addPlayer(this);
						level->getPlayer(0)->sendPacket(CString() >> (char)PLO_ISLEADER);
					}
				}
			}
			break;

			case PLPROP_CARRYSPRITE:
				carrySprite = pPacket.readGUChar();
			break;

			case PLPROP_CURLEVEL:
				len = pPacket.readGUChar();
				levelName = pPacket.readChars(len);
			break;

			case PLPROP_HORSEGIF:
				len = pPacket.readGUChar();
				horseImg = pPacket.readChars(std::min(len, 219)); // limit is 219 in case it appends .gif
				if (!horseImg.isEmpty() && versionID < CLVER_2_1 && getExtension(horseImg).isEmpty())
					horseImg << ".gif";
			break;

			case PLPROP_HORSEBUSHES:
				horsec = pPacket.readGUChar();
			break;

			case PLPROP_EFFECTCOLORS:
				len = pPacket.readGUChar();
				if (len > 0)
					pPacket.readGInt4();
			break;

			case PLPROP_CARRYNPC:
			{
				carryNpcId = pPacket.readGUInt();

				// TODO: Remove when an npcserver is created.
				if (server->getSettings()->getBool("duplicatecanbecarried", false) == false)
				{
					bool isOwner = true;
					{
						std::vector<TPlayer*>* playerList = server->getPlayerList();
						for (std::vector<TPlayer*>::iterator i = playerList->begin(); i != playerList->end(); ++i)
						{
							TPlayer* other = *i;
							if (other == this) continue;
							if (other->getProp(PLPROP_CARRYNPC).readGUInt() == carryNpcId)
							{
								// Somebody else got this NPC first.  Force the player to throw his down
								// and tell the player to remove the NPC from memory.
								carryNpcId = 0;
								isOwner = false;
								sendPacket(CString() >> (char)PLO_PLAYERPROPS >> (char)PLPROP_CARRYNPC >> (int)0);
								sendPacket(CString() >> (char)PLO_NPCDEL2 >> (char)level->getLevelName().length() << level->getLevelName() >> (int)carryNpcId);
								server->sendPacketToLevel(CString() >> (char)PLO_OTHERPLPROPS >> (short)id >> (char)PLPROP_CARRYNPC >> (int)0, pmap, this);
								break;
							}
						}
					}
					if (isOwner)
					{
						// We own this NPC now so remove it from the level and have everybody else delete it.
						TNPC* npc = server->getNPC(carryNpcId);
						level->removeNPC(npc);
                        server->sendPacketToAll(CString() >> (char)PLO_NPCDEL2 >> (char)level->getLevelName().length() << level->getLevelName() >> (int)carryNpcId, nullptr);
					}
				}
			}
			break;

			case PLPROP_APCOUNTER:
				apCounter = pPacket.readGUShort();
			break;

			case PLPROP_MAGICPOINTS:
				mp = pPacket.readGUChar();
				mp = clip(mp, 0, 100);
			break;

			case PLPROP_KILLSCOUNT:
				pPacket.readGInt();
			break;

			case PLPROP_DEATHSCOUNT:
				pPacket.readGInt();
			break;

			case PLPROP_ONLINESECS:
				pPacket.readGInt();
			break;

			case PLPROP_IPADDR:
				pPacket.readGInt5();
			break;

			case PLPROP_UDPPORT:
				udpport = pPacket.readGInt();
				if (id != -1 && loaded)
					server->sendPacketTo(PLTYPE_ANYCLIENT, CString() >> (char)PLO_OTHERPLPROPS >> (short)id >> (char)PLPROP_UDPPORT >> (int)udpport, this);
				// TODO: udp support.
			break;

			case PLPROP_ALIGNMENT:
				ap = pPacket.readGUChar();
				ap = clip(ap, 0, 100);
			break;

			case PLPROP_ADDITFLAGS:
				additionalFlags = pPacket.readGUChar();
			break;

			case PLPROP_ACCOUNTNAME:
				len = pPacket.readGUChar();
				pPacket.readChars(len);
			break;

			case PLPROP_BODYIMG:
				len = pPacket.readGUChar();
				setBodyImage(pPacket.readChars(len));
			break;

			case PLPROP_RATING:
				len = pPacket.readGInt();
				//rating = (float)((len >> 9) & 0xFFF);
			break;

			case PLPROP_ATTACHNPC:
			{
				// Only supports object_type 0 (NPC).
				unsigned char object_type = pPacket.readGUChar();
				unsigned int npcID = pPacket.readGUInt();
				attachNPC = npcID;
				levelBuff >> (char)PLPROP_ATTACHNPC << getProp(PLPROP_ATTACHNPC);
				break;
			}

			case PLPROP_GMAPLEVELX:
			{
				gmaplevelx = pPacket.readGUChar();
				if (pmap)
				{
					levelName = pmap->getLevelAt(gmaplevelx, gmaplevely);
					leaveLevel();
					setLevel(levelName, -1);
				}
#ifdef DEBUG
				printf("gmap level x: %d\n", gmaplevelx);
#endif
				break;
			}

			case PLPROP_GMAPLEVELY:
			{
				gmaplevely = pPacket.readGUChar();
				if (pmap)
				{
					levelName = pmap->getLevelAt(gmaplevelx, gmaplevely);
					leaveLevel();
					setLevel(levelName, -1);
				}
#ifdef DEBUG
				printf("gmap level y: %d\n", gmaplevely);
#endif
				break;
			}

/*
			case PLPROP_UNKNOWN50:
				break;
*/
			case PLPROP_PCONNECTED:
			break;

			case PLPROP_PLANGUAGE:
				len = pPacket.readGUChar();
				language = pPacket.readChars(len);
			break;

			case PLPROP_PSTATUSMSG:
				statusMsg = pPacket.readGUChar();
				if (id == -1 || !loaded)
					break;

				server->sendPacketToAll(CString() >> (char)PLO_OTHERPLPROPS >> (short)id >> (char)PLPROP_PSTATUSMSG >> (char)statusMsg, this);
			break;

			case PLPROP_GATTRIB1:  attrList[0]  = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB2:  attrList[1]  = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB3:  attrList[2]  = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB4:  attrList[3]  = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB5:  attrList[4]  = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB6:  attrList[5]  = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB7:  attrList[6]  = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB8:  attrList[7]  = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB9:  attrList[8]  = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB10: attrList[9]  = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB11: attrList[10] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB12: attrList[11] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB13: attrList[12] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB14: attrList[13] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB15: attrList[14] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB16: attrList[15] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB17: attrList[16] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB18: attrList[17] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB19: attrList[18] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB20: attrList[19] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB21: attrList[20] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB22: attrList[21] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB23: attrList[22] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB24: attrList[23] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB25: attrList[24] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB26: attrList[25] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB27: attrList[26] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB28: attrList[27] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB29: attrList[28] = pPacket.readChars(pPacket.readGUChar()); break;
			case PLPROP_GATTRIB30: attrList[29] = pPacket.readChars(pPacket.readGUChar()); break;

			// OS type.
			// Windows: wind
			case PLPROP_OSTYPE:
				os = pPacket.readChars(pPacket.readGUChar());
				break;

			// Text codepage.
			// Example: 1252
			case PLPROP_TEXTCODEPAGE:
				codepage = pPacket.readGInt();
				break;

			// Location, in pixels, of the player on the level in 2.30+ clients.
			// Bit 0x0001 controls if it is negative or not.
			// Bits 0xFFFE are the actual value.
			case PLPROP_X2:
				x2 = len = pPacket.readGUShort();
				status &= (~PLSTATUS_PAUSED);
				lastMovement = time(0);
				grMovementUpdated = true;
				doTouchTest = true;

				// If the first bit is 1, our position is negative.
				x2 >>= 1;
				if ((short)len & 0x0001) x2 = -x2;

				// Let pre-2.30+ clients see 2.30+ movement.
				x = (float)x2 / 16.0f;
				levelBuff2 >> (char)PLPROP_X << getProp(PLPROP_X);
				break;

			case PLPROP_Y2:
				y2 = len = pPacket.readGUShort();
				status &= (~PLSTATUS_PAUSED);
				lastMovement = time(0);
				grMovementUpdated = true;
				doTouchTest = true;

				// If the first bit is 1, our position is negative.
				y2 >>= 1;
				if ((short)len & 0x0001) y2 = -y2;

				// Let pre-2.30+ clients see 2.30+ movement.
				y = (float)y2 / 16.0f;
				levelBuff2 >> (char)PLPROP_Y << getProp(PLPROP_Y);

				// Do collision testing.
				doSignCheck = true;
				break;

			case PLPROP_Z2:
				z2 = len = pPacket.readGUShort();
				status &= (~PLSTATUS_PAUSED);
				lastMovement = time(0);
				grMovementUpdated = true;
				doTouchTest = true;

				// If the first bit is 1, our position is negative.
				z2 >>= 1;
				if ((short)len & 0x0001) z2 = -z2;

				// Let pre-2.30+ clients see 2.30+ movement.
				z = (float)(int)(((float)z2 / 16.0f) + 0.5f);
				levelBuff2 >> (char)PLPROP_Z << getProp(PLPROP_Z);
				break;

			case PLPROP_COMMUNITYNAME:
				pPacket.readChars(pPacket.readGUChar());
				break;

			default:
			{
#ifdef DEBUG
				printf("Unidentified PLPROP: %i, readPos: %d\n", propId, pPacket.readPos());
				for (int i = 0; i < pPacket.length(); ++i)
					printf("%02x ", (unsigned char)pPacket[i]);
				printf("\n");
#endif
				sentInvalid = true;
			}
			return;
		}

		if (pForward && __sendLocal[propId] == true)
			levelBuff >> (char)propId << getProp(propId);

		if (pForwardToSelf)
			selfBuff >> (char)propId << getProp(propId);
	}

	// Send Buffers Out
	if (isLoggedIn() && isLoaded())
	{
		if (globalBuff.length() > 0)
			server->sendPacketToAll(CString() >> (char)PLO_OTHERPLPROPS >> (short)this->id << globalBuff, this);
		if (levelBuff.length() > 0)
		{
			// We need to arrange the props packet in a certain way depending
			// on if our client supports precise movement or not.  Versions 2.3+
			// support precise movement.
			bool MOVE_PRECISE = false;
			if (versionID >= CLVER_2_3) MOVE_PRECISE = true;

			server->sendPacketToLevel(CString() >> (char)PLO_OTHERPLPROPS >> (short)this->id << (!MOVE_PRECISE ? levelBuff : levelBuff2) << (!MOVE_PRECISE ? levelBuff2 : levelBuff), pmap, this, false);
		}
		if (selfBuff.length() > 0)
			this->sendPacket(CString() >> (char)PLO_PLAYERPROPS << selfBuff);

		// Movement check.
		if (!rc)
		{
			if (doSignCheck)
				testSign();

#ifdef V8NPCSERVER
			if (doTouchTest)
				testTouch();
#endif
		}
	}

	if (sentInvalid)
	{
		// If we are getting a whole bunch of invalid packets, something went wrong.  Disconnect the player.
		invalidPackets++;
		if (invalidPackets > 5)
		{
			serverlog.out("[%s] Player %s is sending invalid packets.\n", server->getName().text(), nickName.text());
			sendPacket(CString() >> (char)PLO_DISCMESSAGE << "Disconnected for sending invalid packets.");
			server->deletePlayer(this);
		}
	}
}

void TPlayer::sendProps(const bool *pProps, int pCount)
{
	// Definition
	CString propPacket;

	// Create Props
	if (isClient() && versionID < CLVER_2_1) pCount = 37;
	for (int i = 0; i < pCount; ++i)
	{
		if (pProps[i])
			propPacket >> (char)i << getProp(i);
	}

	// Send Packet
	sendPacket(CString() >> (char)PLO_PLAYERPROPS << propPacket);
}

CString TPlayer::getProps(const bool *pProps, int pCount)
{
	CString propPacket;

	// Start the prop packet.
	propPacket >> (char)PLO_OTHERPLPROPS >> (short)this->id;

	if (pCount > 0)
	{
		// Check if PLPROP_JOINLEAVELVL is set.
		if (isClient() && pProps[PLPROP_JOINLEAVELVL])
			propPacket >> (char)PLPROP_JOINLEAVELVL >> (char)1;

		// Create Props
		if (isClient() && versionID < CLVER_2_1) pCount = 37;
		for (int i = 0; i < pCount; ++i)
		{
			if (i == PLPROP_JOINLEAVELVL) continue;
			
			if (i == PLPROP_ATTACHNPC && attachNPC != 0)
			{
				propPacket >> (char)i;
				getProp(propPacket, i);
			}

			if (pProps[i])
			{
				propPacket >> (char)i;
				getProp(propPacket, i);
			}
		}

	}

	if (isExternal)
		propPacket >> (char)81 << "!";
	
	return propPacket;
}
