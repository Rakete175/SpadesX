//Copyright DarkNeutrino 2021
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "Enums.h"
#include "Structs.h"
#include "Queue.h"
#include "Types.h"
#include "Compress.h"
#include "DataStream.h"
#include "Protocol.h"

void SendPlayerLeft(Server* server, uint8 playerID)
{
	STATUS("sending player left event");
	for (uint8 i = 0; i < server->protocol.maxPlayers; ++i) {
		if (i != playerID && server->player[i].state != STATE_DISCONNECTED) {
			ENetPacket* packet = enet_packet_create(NULL, 2, ENET_PACKET_FLAG_RELIABLE);
			packet->data[0]	= PACKET_TYPE_PLAYER_LEFT;
			packet->data[1]	= playerID;

			if (enet_peer_send(server->player[i].peer, 0, packet) != 0) {
				WARNING("failed to send player left event\n");
			}
		}
	}
}

void SendWeaponReload(Server* server, uint8 playerID) {
			ENetPacket* packet = enet_packet_create(NULL, 4, ENET_PACKET_FLAG_RELIABLE);
			DataStream  stream = {packet->data, packet->dataLength, 0};
			WriteByte(&stream, PACKET_TYPE_WEAPON_RELOAD);
			WriteByte(&stream, playerID);
			WriteByte(&stream, server->player[playerID].weaponReserve);
			WriteByte(&stream, server->player[playerID].weaponClip);
			SendPacketExceptSender(server, packet, playerID);
}

void SendWeaponInput(Server *server, uint8 playerID, uint8 wInput) {
				ENetPacket* packet = enet_packet_create(NULL, 3, ENET_PACKET_FLAG_RELIABLE);
				DataStream  stream = {packet->data, packet->dataLength, 0};
				WriteByte(&stream, PACKET_TYPE_WEAPON_INPUT);
				WriteByte(&stream, playerID);
				WriteByte(&stream, wInput);
				SendPacketExceptSender(server, packet, playerID);
}

void SendSetColor(Server *server, uint8 playerID, uint8 R, uint8 G, uint8 B) {
			ENetPacket* packet = enet_packet_create(NULL, 5, ENET_PACKET_FLAG_RELIABLE);
			DataStream  stream = {packet->data, packet->dataLength, 0};
			WriteByte(&stream, PACKET_TYPE_SET_COLOR);
			WriteByte(&stream, playerID);
			WriteByte(&stream, B);
			WriteByte(&stream, G);
			WriteByte(&stream, R);
			SendPacketExceptSender(server, packet, playerID);
}

void SendSetTool(Server *server, uint8 playerID, uint8 tool) {
		ENetPacket* packet = enet_packet_create(NULL, 3, ENET_PACKET_FLAG_RELIABLE);
		DataStream  stream = {packet->data, packet->dataLength, 0};
		WriteByte(&stream, PACKET_TYPE_SET_TOOL);
		WriteByte(&stream, playerID);
		WriteByte(&stream, tool);
		SendPacketExceptSender(server, packet, playerID);
}

void SendBlockLine(Server *server, uint8 playerID, vec3i start, vec3i end) {
			ENetPacket* packet = enet_packet_create(NULL, 26, ENET_PACKET_FLAG_RELIABLE);
			DataStream  stream = {packet->data, packet->dataLength, 0};
			WriteByte(&stream, PACKET_TYPE_BLOCK_LINE);
			WriteByte(&stream, playerID);
			WriteInt(&stream, start.x);
			WriteInt(&stream, start.y);
			WriteInt(&stream, start.z);
			WriteInt(&stream, end.x);
			WriteInt(&stream, end.y);
			WriteInt(&stream, end.z);
			enet_host_broadcast(server->host, 0, packet);
}

void SendBlockAction(Server *server, uint8 playerID, uint8 actionType, int X, int Y, int Z) {
			ENetPacket* packet = enet_packet_create(NULL, 15, ENET_PACKET_FLAG_RELIABLE);
			DataStream  stream = {packet->data, packet->dataLength, 0};
			WriteByte(&stream, PACKET_TYPE_BLOCK_ACTION);
			WriteByte(&stream, playerID);
			WriteByte(&stream, actionType);
			WriteInt(&stream, X);
			WriteInt(&stream, Y);
			WriteInt(&stream, Z);
			enet_host_broadcast(server->host, 0, packet);
}

void SendStateData(Server* server, uint8 playerID)
{
	ENetPacket* packet = enet_packet_create(NULL, 104, ENET_PACKET_FLAG_RELIABLE);
	DataStream  stream = {packet->data, packet->dataLength, 0};
	WriteByte(&stream, PACKET_TYPE_STATE_DATA);
	WriteByte(&stream, playerID);
	WriteColor3i(&stream, server->protocol.colorFog);
	WriteColor3i(&stream, server->protocol.colorTeamA);
	WriteColor3i(&stream, server->protocol.colorTeamB);
	WriteArray(&stream, server->protocol.nameTeamA, 10);
	WriteArray(&stream, server->protocol.nameTeamB, 10);
	WriteByte(&stream, server->protocol.mode);

	// MODE CTF:

	WriteByte(&stream, server->protocol.ctf.scoreTeamA); // SCORE TEAM A
	WriteByte(&stream, server->protocol.ctf.scoreTeamB); // SCORE TEAM B
	WriteByte(&stream, server->protocol.ctf.scoreLimit); // SCORE LIMIT
	WriteByte(&stream, server->protocol.ctf.intelFlags); // INTEL FLAGS

	if ((server->protocol.ctf.intelFlags & 1) == 0) {
		WriteVector3f(&stream, server->protocol.ctf.intelTeamA);
	} else {
		WriteByte(&stream, server->protocol.ctf.playerIntelTeamA);
		StreamSkip(&stream, 11);
	}

	if ((server->protocol.ctf.intelFlags & 2) == 0) {
		WriteVector3f(&stream, server->protocol.ctf.intelTeamB);
	} else {
		WriteByte(&stream, server->protocol.ctf.playerIntelTeamB);
		StreamSkip(&stream, 11);
	}

	WriteVector3f(&stream, server->protocol.ctf.baseTeamA);
	WriteVector3f(&stream, server->protocol.ctf.baseTeamB);

	if (enet_peer_send(server->player[playerID].peer, 0, packet) == 0) {
		server->player[playerID].state = STATE_READY;
	}
}

void SendInputData(Server* server, uint8 playerID)
{
	ENetPacket* packet = enet_packet_create(NULL, 3, ENET_PACKET_FLAG_RELIABLE);
	DataStream  stream = {packet->data, packet->dataLength, 0};
	WriteByte(&stream, PACKET_TYPE_INPUT_DATA);
	WriteByte(&stream, playerID);
	WriteByte(&stream, server->player[playerID].input);
	SendPacketExceptSenderDistCheck(server, packet, playerID);
}

void sendKillPacket(Server* server, uint8 killerID, uint8 playerID, uint8 killReason, uint8 respawnTime) {
	ENetPacket* packet = enet_packet_create(NULL, 5, ENET_PACKET_FLAG_RELIABLE);
	DataStream  stream = {packet->data, packet->dataLength, 0};
	WriteByte(&stream, PACKET_TYPE_KILL_ACTION);
	WriteByte(&stream, playerID); //Player that shot
	WriteByte(&stream, killerID); //playerID
	WriteByte(&stream, killReason); //Killing reason (1 is headshot)
	WriteByte(&stream, respawnTime); //Time before respawn happens
	enet_host_broadcast(server->host, 0, packet);
	server->player[killerID].kills++;
	server->player[playerID].respawnTime = respawnTime;
	server->player[playerID].startOfRespawnWait = time(NULL);
	server->player[playerID].state = STATE_WAITING_FOR_RESPAWN;
}

void sendHP(Server* server, uint8 hitPlayerID, uint8 playerID, uint8 HPChange, uint8 type, uint8 killReason, uint8 respawnTime) {
	ENetPacket* packet = enet_packet_create(NULL, 15, ENET_PACKET_FLAG_RELIABLE);
	DataStream  stream = {packet->data, packet->dataLength, 0};
	server->player[hitPlayerID].HP -= HPChange;
	if ((server->player[hitPlayerID].HP <= 0 || server->player[hitPlayerID].HP > 100) && server->player[playerID].alive == 1) {
		server->player[playerID].alive = 0;
		server->player[playerID].HP = 0;
		sendKillPacket(server, hitPlayerID, playerID, killReason, respawnTime);
	}
	else {
	if (server->player[hitPlayerID].HP >= 1 && server->player[hitPlayerID].HP <= 100 && server->player[playerID].alive == 1 ) {
	WriteByte(&stream, PACKET_TYPE_SET_HP);
	WriteByte(&stream, server->player[hitPlayerID].HP);
	WriteByte(&stream, type);
	WriteFloat(&stream, server->player[playerID].movement.position.x);
	WriteFloat(&stream, server->player[playerID].movement.position.y);
	WriteFloat(&stream, server->player[playerID].movement.position.z);
	enet_peer_send(server->player[playerID].peer, 0, packet);
	}
	}
}

void SendPlayerState(Server* server, uint8 playerID, uint8 otherID)
{
	ENetPacket* packet = enet_packet_create(NULL, 28, ENET_PACKET_FLAG_RELIABLE);
	DataStream  stream = {packet->data, packet->dataLength, 0};
	WriteByte(&stream, PACKET_TYPE_EXISTING_PLAYER);
	WriteByte(&stream, otherID);					// ID
	WriteByte(&stream, server->player[otherID].team);	  // TEAM
	WriteByte(&stream, server->player[otherID].weapon);	// WEAPON
	WriteByte(&stream, server->player[otherID].item);	//HELD ITEM
	WriteInt(&stream, server->player[otherID].kills);	//KILLS
	WriteColor3i(&stream, server->player[otherID].color);	//COLOR
	WriteArray(&stream, server->player[otherID].name, 16); // NAME

	if (enet_peer_send(server->player[playerID].peer, 0, packet) != 0) {
		WARNING("failed to send player state\n");
	}
}

void SendMapStart(Server* server, uint8 playerID)
{
	STATUS("sending map info");
	ENetPacket* packet = enet_packet_create(NULL, 5, ENET_PACKET_FLAG_RELIABLE);
	DataStream  stream = {packet->data, packet->dataLength, 0};
	WriteByte(&stream, PACKET_TYPE_MAP_START);
	WriteInt(&stream, server->map.compressedSize);
	if (enet_peer_send(server->player[playerID].peer, 0, packet) == 0) {
		server->player[playerID].state = STATE_LOADING_CHUNKS;
		
		// map
		uint8* out = (uint8*) malloc(server->map.mapSize);
		libvxl_write(&server->map.map, out, &server->map.mapSize);
		server->map.compressedMap = CompressData(out, server->map.mapSize, DEFAULT_COMPRESSOR_CHUNK_SIZE);
		server->player[playerID].queues = server->map.compressedMap;
	}
}

void SendMapChunks(Server* server, uint8 playerID)
{
	if (server->player[playerID].queues == NULL) {
		server->player[playerID].state = STATE_JOINING;
		STATUS("loading chunks done");
	} else {
		ENetPacket* packet = enet_packet_create(NULL, server->player[playerID].queues->length + 1, ENET_PACKET_FLAG_RELIABLE);
		DataStream  stream = {packet->data, packet->dataLength, 0};
		WriteByte(&stream, PACKET_TYPE_MAP_CHUNK);
		WriteArray(&stream, server->player[playerID].queues->block, server->player[playerID].queues->length);

		if (enet_peer_send(server->player[playerID].peer, 0, packet) == 0) {
			server->player[playerID].queues = server->player[playerID].queues->next;
		}
	}
}

void SendRespawnState(Server* server, uint8 playerID, uint8 otherID)
{
	ENetPacket* packet = enet_packet_create(NULL, 32, ENET_PACKET_FLAG_RELIABLE);
	DataStream  stream = {packet->data, packet->dataLength, 0};
	WriteByte(&stream, PACKET_TYPE_CREATE_PLAYER);
	WriteByte(&stream, otherID);					// ID
	WriteByte(&stream, server->player[otherID].weapon);	// WEAPON
	WriteByte(&stream, server->player[otherID].team);	  // TEAM
	WriteVector3f(&stream, server->player[otherID].movement.position);  // X Y Z
	WriteArray(&stream, server->player[otherID].name, 16); // NAME

	if (enet_peer_send(server->player[playerID].peer, 0, packet) != 0) {
		WARNING("failed to send player state\n");
	}
}

void SendRespawn(Server* server, uint8 playerID)
{
	for (uint8 i = 0; i < server->protocol.maxPlayers; ++i) {
		if (server->player[i].state != STATE_DISCONNECTED) {
			SendRespawnState(server, i, playerID);
		}
	}
	server->player[playerID].state = STATE_READY;
}

void sendMessage(ENetEvent event, DataStream* data, Server* server) {
	uint32 packetSize = event.packet->dataLength + 1;
	int player = ReadByte(data);
	int meantfor = ReadByte(data);
	uint32 length = DataLeft(data);
	char * message = calloc(length + 1, sizeof (char));
	ReadArray(data, message, length);
			message[length] = '\0';
			ENetPacket* packet = enet_packet_create(NULL, packetSize, ENET_PACKET_FLAG_RELIABLE);
			DataStream  stream = {packet->data, packet->dataLength, 0};
			WriteByte(&stream, PACKET_TYPE_CHAT_MESSAGE);
			WriteByte(&stream, player);
			WriteByte(&stream, meantfor);
			WriteArray(&stream, message, length);
			if (message[0] == '/') {
				if (message[1] == 'k' && message[2] == 'i' && message[3] == 'l' && message[4] == 'l' && message[5] == 'p') {
					char uselessString[30];
					int id = 0;
					if (sscanf(message, "%s #%d", uselessString, &id) == 1) {
						sendKillPacket(server, player, player, 0, 5);
					}
					else {
						if (server->player[id].state == STATE_READY) {
							sendKillPacket(server, id, id, 0, 5);
						}
						else {
							sendServerNotice(server, player, "Player does not exist or isnt spawned yet");
						}
					}
				}
				else if (message[1] == 'k' && message[2] == 'i' && message[3] == 'l' && message[4] == 'l') {
					sendKillPacket(server, player, player, 0, 5);
				}
				else if (message[1] == 't' && message[2] == 'k') {
					if (server->player[player].allowTK == 1) {
						SetTeamKillingFlag(server, 0);
						broadcastServerNotice(server, "Team Killing has been disabled");
					}
					else if (server->player[player].allowTK == 0) {
						SetTeamKillingFlag(server, 1);
						broadcastServerNotice(server, "Team Killing has been enabled");
					}
				}
				else if (message[1] == 'u' && message[2] == 'p' && message[3] == 's') {
					float ups = 0;
					char uselessString[30]; // Just to be sure we dont read dumb stuff
					char upsString[4];
					sscanf(message, "%s %f", uselessString, &ups);
					sscanf(message, "%s %s", uselessString, upsString);
					if (ups >=1 && ups <= 300) {
						server->player[player].ups = ups;
						char fullString[32] = "UPS changed to ";
						strcat(fullString, upsString);
						strcat(fullString, " succesufully");
						sendServerNotice(server, player, fullString);
					}
					else {
						sendServerNotice(server, player, "Changing UPS failed. Please select value between 1 and 300");
					}
				}
			}
			else {
				enet_host_broadcast(server->host, 0, packet);
			}
			free(message);
}

void SendWorldUpdate(Server* server, uint8 playerID)
{
	ENetPacket* packet = enet_packet_create(NULL, 1 + (32 * 24), ENET_PACKET_FLAG_UNSEQUENCED);
	DataStream  stream = {packet->data, packet->dataLength, 0};
	WriteByte(&stream, PACKET_TYPE_WORLD_UPDATE);

	for (uint8 j = 0; j < 32; ++j) {
		if (playerToPlayerVisible(server, playerID, j)) {
			WriteVector3f(&stream, server->player[j].movement.position);
			WriteVector3f(&stream, server->player[j].movement.forwardOrientation);
		}
		else {
			Vector3f empty;
			empty.x = 0;
			empty.y = 0;
			empty.z = 0;
			WriteVector3f(&stream, empty);
			WriteVector3f(&stream, empty);
		}
	}
	enet_peer_send(server->player[playerID].peer, 0, packet);
}
