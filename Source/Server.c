//Copyright DarkNeutrino 2021
#include "Server.h"

#include "Structs.h"
#include "Enums.h"
#include "Types.h"
#include "Line.h"
#include "Conversion.h"
#include "Master.h"
#include "Map.h"
#include "Player.h"
#include "Protocol.h"
#include "Packets.h"
#include "PacketReceive.h"

#include <Compress.h>
#include <DataStream.h>
#include <Queue.h>
#include <enet/enet.h>
#include <libvxl/libvxl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

unsigned long long updateTime;
unsigned long long lastUpdateTime;
unsigned long long timeSinceStart;
Server server;
pthread_t thread[4];

static unsigned long long get_nanos(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (unsigned long long)ts.tv_sec * 1000000000L + ts.tv_nsec;
}

static void* calculatePhysics() {
	while (1) {
		updateTime = get_nanos();
		if (updateTime - lastUpdateTime >= (1000000000/120)) {
			updateMovementAndGrenades(&server, updateTime, lastUpdateTime, timeSinceStart);
			lastUpdateTime = get_nanos();
		} 
	}
}

static void ServerInit(Server* server, uint32 connections, char* map)
{
	char team1[] = "Team A";
	char team2[] = "Team B";
	updateTime = lastUpdateTime = get_nanos();
	server->protocol.numPlayers = 0;
	server->protocol.maxPlayers = (connections <= 32) ? connections : 32;

	server->map.compressedMap  = NULL;
	server->map.compressedSize = 0;
	server->protocol.inputFlags = 0;
	
	for (uint8 i = 0; i < 11; i++) {
		server->protocol.nameTeamA[i] = ' ';
		server->protocol.nameTeamB[i] = ' ';
	}
	Vector3f empty = {0, 0, 0};
	Vector3f forward = {1, 0, 0};
	Vector3f height = {0, 0, 1};
	Vector3f strafe = {0, 1, 0};
	for (uint32 i = 0; i < server->protocol.maxPlayers; ++i) {
		server->player[i].state  = STATE_DISCONNECTED;
		server->player[i].queues = NULL;
		server->player[i].ups = 60;
		server->player[i].timeSinceLastWU = get_nanos();
		server->player[i].input  = 0;
		server->player[i].movement.eyePos = empty;
		server->player[i].movement.forwardOrientation = forward;
		server->player[i].movement.strafeOrientation = strafe;
		server->player[i].movement.heightOrientation = height;
		server->player[i].movement.position = empty;
		server->player[i].movement.velocity = empty;
		server->player[i].airborne = 0;
		server->player[i].wade = 0;
		server->player[i].lastclimb = 0;
		server->player[i].movBackwards = 0;
		server->player[i].movForward = 0;
		server->player[i].movLeft = 0;
		server->player[i].movRight = 0;
		server->player[i].jumping = 0;
		server->player[i].crouching = 0;
		server->player[i].sneaking = 0;
		server->player[i].sprinting = 0;
		server->player[i].primary_fire = 0;
		server->player[i].secondary_fire = 0;
		server->player[i].canBuild = 1;
		server->player[i].allowKilling = 1;
		server->player[i].allowTeamKilling = 0;
		server->player[i].muted = 0;
		server->player[i].toldToMaster = 0;
		memset(server->player[i].name, 0, 17);
	}

	srand(time(NULL));
	server->globalAB = 1;
	server->globalAK = 1;
	server->protocol.spawns[0].from.x = 64.f;
	server->protocol.spawns[0].from.y = 224.f;
	server->protocol.spawns[0].to.x   = 128.f;
	server->protocol.spawns[0].to.y   = 288.f;

	server->protocol.spawns[1].from.x = 382.f;
	server->protocol.spawns[1].from.y = 224.f;
	server->protocol.spawns[1].to.x   = 448.f;
	server->protocol.spawns[1].to.y   = 288.f;

	server->protocol.colorFog[0] = 0x80;
	server->protocol.colorFog[1] = 0xE8;
	server->protocol.colorFog[2] = 0xFF;

	server->protocol.colorTeamA[0] = 0xff;
	server->protocol.colorTeamA[1] = 0x00;
	server->protocol.colorTeamA[2] = 0x00;

	server->protocol.colorTeamB[0] = 0x00;
	server->protocol.colorTeamB[1] = 0xff;
	server->protocol.colorTeamB[2] = 0x00;

	memcpy(server->protocol.nameTeamA, team1, strlen(team1));
	memcpy(server->protocol.nameTeamB, team2, strlen(team2));
	server->protocol.nameTeamA[strlen(team1)] = '\0';
	server->protocol.nameTeamB[strlen(team2)] = '\0';
	
	server->protocol.mode = GAME_MODE_CTF;

	// Init CTF

	server->protocol.ctf.scoreTeamA = 0;
	server->protocol.ctf.scoreTeamB = 0;
	server->protocol.ctf.scoreLimit = 10;
	server->protocol.ctf.intelFlags = 0;
	// intel
	server->protocol.ctf.intelTeamA.x = 120.f;
	server->protocol.ctf.intelTeamA.y = 256.f;
	server->protocol.ctf.intelTeamA.z = 62.f;
	server->protocol.ctf.intelTeamB.x = 110.f;
	server->protocol.ctf.intelTeamB.y = 256.f;
	server->protocol.ctf.intelTeamB.z = 62.f;
	// bases
	server->protocol.ctf.baseTeamA.x = 120.f;
	server->protocol.ctf.baseTeamA.y = 250.f;
	server->protocol.ctf.baseTeamA.z = 62.f;
	server->protocol.ctf.baseTeamB.x = 110.f;
	server->protocol.ctf.baseTeamB.y = 250.f;
	server->protocol.ctf.baseTeamB.z = 62.f;

	LoadMap(server, map);
}

static void SendJoiningData(Server* server, uint8 playerID)
{
	STATUS("sending state");
	for (uint8 i = 0; i < server->protocol.maxPlayers; ++i) {
		if (i != playerID && server->player[i].state != STATE_DISCONNECTED) {
			SendPlayerState(server, playerID, i);
		}
	}
	SendStateData(server, playerID);
}

static uint8 OnConnect(Server* server)
{
	if (server->protocol.numPlayers == server->protocol.maxPlayers) {
		return 0xFF;
	}
	uint8 playerID;
	for (playerID = 0; playerID < server->protocol.maxPlayers; ++playerID) {
		if (server->player[playerID].state == STATE_DISCONNECTED) {
			server->player[playerID].state = STATE_STARTING_MAP;
			break;
		}
	}
	server->protocol.numPlayers++;
	return playerID;
}

static void OnPlayerUpdate(Server* server, uint8 playerID)
{
	switch (server->player[playerID].state) {
		case STATE_STARTING_MAP:
			SendMapStart(server, playerID);
			break;
		case STATE_LOADING_CHUNKS:
			SendMapChunks(server, playerID);
			break;
		case STATE_JOINING:
			SendJoiningData(server, playerID);
			break;
		case STATE_SPAWNING:
			server->player[playerID].HP = 100;
			server->player[playerID].alive = 1;
			SetPlayerRespawnPoint(server, playerID);
			SendRespawn(server, playerID);
			break;
		case STATE_WAITING_FOR_RESPAWN:
		{
			if (time(NULL) - server->player[playerID].startOfRespawnWait >= server->player[playerID].respawnTime) {
				server->player[playerID].state = STATE_SPAWNING;
			}
			break;
		}
		case STATE_READY:
			// send data
			if (server->master.enableMasterConnection == 1) {
				if (server->player[playerID].toldToMaster == 0) {
					updateMaster(server);
					server->player[playerID].toldToMaster = 1;
				}
			}
			break;
		default:
			// disconnected
			break;
	}
}

static void* WorldUpdate() {
	while(1) {
		for (uint8 playerID = 0; playerID < server.protocol.maxPlayers; ++playerID) {
			OnPlayerUpdate(&server, playerID);
			if (server.player[playerID].state == STATE_READY) {
				unsigned long long time = get_nanos();
				if (time - server.player[playerID].timeSinceLastWU >= (1000000000/server.player[playerID].ups)) {
					SendWorldUpdate(&server, playerID);
					server.player[playerID].timeSinceLastWU = get_nanos();
				}
			}
		}
	}
}

static void* ServerUpdate()
{
	ENetEvent event;
	while (enet_host_service(server.host, &event, 0) > 0) {
		uint8 bannedUser = 0;
		uint8 playerID;
		switch (event.type) {
			case ENET_EVENT_TYPE_NONE:
				STATUS("Event of type none received. Ignoring");
			break;
			case ENET_EVENT_TYPE_CONNECT:
				if (event.data != VERSION_0_75) {
					enet_peer_disconnect_now(event.peer, REASON_WRONG_PROTOCOL_VERSION);
					break;
				}
				
				FILE *fp;
				fp = fopen("BanList.txt", "r");
				if (fp == NULL) {
					WARNING("BanList.txt could not be opened for checking ban. PLEASE FIX THIS NOW BY CREATING THIS FILE!!!!");
				}
				unsigned int IP = 0;
				char nameOfPlayer[20];
				while (fscanf(fp, "%d %s", &IP, nameOfPlayer) != EOF) {
					if (IP == event.peer->address.host) {
						enet_peer_disconnect_now(event.peer, REASON_BANNED);
						printf("WARNING: Banned user %s tried to join. IP: %d\n", nameOfPlayer, IP);
						bannedUser = 1;
						break;
					}
				}
				fclose(fp);
				if (bannedUser) {
					break;
				}
				// check peer
				// ...
				// find next free ID
				playerID = OnConnect(&server);
				if (playerID == 0xFF) {
					enet_peer_disconnect_now(event.peer, REASON_SERVER_FULL);
					STATUS("Server full. Kicking player");
					break;
				}
				server.player[playerID].peer = event.peer;
				event.peer->data	   = (void*) ((size_t) playerID);
				server.player[playerID].HP = 100;
				uint32ToUint8(&server, event, playerID);
				printf("INFO: connected %u (%d.%d.%d.%d):%u, id %u\n", event.peer->address.host, server.player[playerID].ip[0], server.player[playerID].ip[1], server.player[playerID].ip[2], server.player[playerID].ip[3], event.peer->address.port, playerID);
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				playerID				= (uint8)((size_t) event.peer->data);
				SendPlayerLeft(&server, playerID);
				Vector3f empty = {0, 0, 0};
				Vector3f forward = {1, 0, 0};
				Vector3f height = {0, 0, 1};
				Vector3f strafe = {0, 1, 0};
				server.player[playerID].state  = STATE_DISCONNECTED;
				server.player[playerID].queues = NULL;
				server.player[playerID].ups = 60;
				server.player[playerID].timeSinceLastWU = get_nanos();
				server.player[playerID].input  = 0;
				server.player[playerID].movement.eyePos = empty;
				server.player[playerID].movement.forwardOrientation = forward;
				server.player[playerID].movement.strafeOrientation = strafe;
				server.player[playerID].movement.heightOrientation = height;
				server.player[playerID].movement.position = empty;
				server.player[playerID].movement.velocity = empty;
				server.player[playerID].airborne = 0;
				server.player[playerID].wade = 0;
				server.player[playerID].lastclimb = 0;
				server.player[playerID].movBackwards = 0;
				server.player[playerID].movForward = 0;
				server.player[playerID].movLeft = 0;
				server.player[playerID].movRight = 0;
				server.player[playerID].jumping = 0;
				server.player[playerID].crouching = 0;
				server.player[playerID].sneaking = 0;
				server.player[playerID].sprinting = 0;
				server.player[playerID].primary_fire = 0;
				server.player[playerID].secondary_fire = 0;
				server.player[playerID].canBuild = 1;
				server.player[playerID].allowKilling = 1;
				server.player[playerID].allowTeamKilling = 0;
				server.player[playerID].muted = 0;
				server.player[playerID].toldToMaster = 0;
				memset(server.player[playerID].name, 0, 17);
				server.protocol.numPlayers--;
				if (server.master.enableMasterConnection == 1) {
					updateMaster(&server);
				}
				break;
			case ENET_EVENT_TYPE_RECEIVE:
			{
				DataStream stream = {event.packet->data, event.packet->dataLength, 0};
				playerID		  = (uint8)((size_t) event.peer->data);
				OnPacketReceived(&server, playerID, &stream, event);
				enet_packet_destroy(event.packet);
				break;
			}
		}
	}
}

void StartServer(uint16 port, uint32 connections, uint32 channels, uint32 inBandwidth, uint32 outBandwidth, uint8 master, char* map)
{
	timeSinceStart = get_nanos();
	STATUS("Welcome to SpadesX server");
	STATUS("Initializing ENet");

	if (enet_initialize() != 0) {
		ERROR("Failed to initalize ENet");
		exit(EXIT_FAILURE);
	}
	atexit(enet_deinitialize);

	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = port;

	printf("Creating server at port %d\n", port);

	//Server server;

	server.host = enet_host_create(&address, connections, channels, inBandwidth, outBandwidth);
	if (server.host == NULL) {
		ERROR("Failed to create server");
		exit(EXIT_FAILURE);
	}

	if (enet_host_compress_with_range_coder(server.host) != 0) {
		WARNING("Compress with range coder failed");
	}

	STATUS("Intializing server");

	ServerInit(&server, connections, map);

	STATUS("Server started");
	server.master.enableMasterConnection = master;
	if (server.master.enableMasterConnection == 1) {
		ConnectMaster(&server, port);
	}
	server.master.timeSinceLastSend = time(NULL);

	int rc;
	rc = pthread_create(&thread[0], NULL, WorldUpdate, NULL);
	if (rc) {
		ERROR("Thread failed");
	}
	rc = pthread_create(&thread[1], NULL, calculatePhysics, NULL);
	if (rc) {
		ERROR("Thread failed");
	}
	rc = pthread_create(&thread[2], NULL, keepMasterAlive, &server);
	if (rc) {
		ERROR("Thread failed");
	}
	while (1) {
		ServerUpdate();
	}
}
