#include "../libs/pinc.h"
#include "version.h"

#include <stdio.h>
#include <pthread.h>
#include <sqlite3.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

sqlite3* db = NULL;
time_t startTime;
volatile int voteInProgress = 0;
volatile int runTimer = 1;

void* timer(void* arg);
void* cVoteTable(void* arg);
void invalidUse();
void voteSystem();
int checkAndInsertPlayer(client_t* playerName);
const int votedPlayers();
const int allPlayers();
int deleteAllTables();
int clearVoteTable();
const int isPlayerVoted(const char* playerName);
void changeMap(const char* map, const char* mode);


PCL int OnInit() {
	pthread_t timerThread;
	if (pthread_create(&timerThread, NULL, timer, NULL) != 0) {
		Plugin_PrintError("Failed to create timer thread");
        return 1;

    }

	pthread_t cVoteTableThread;
	if (pthread_create(&cVoteTableThread, NULL, cVoteTable, NULL) != 0) {
		Plugin_PrintError("Failed to create timer thread");
        return 1;

    }

	pthread_detach(timerThread);
	pthread_detach(cVoteTableThread);

	int rc = sqlite3_open("VoteTmp.db", &db);
	char* errMsg = NULL;
	if (rc) {
		Plugin_PrintError("Vote Plugin: Error occurred on opening database.\n");
		return 1;

	} else {
		Plugin_Printf("Vote Plugin: Database opened successfully.\n");

	}

	const char* sqlCreateTable = "CREATE TABLE IF NOT EXISTS voteTable ("
								"ID INTEGER PRIMARY KEY AUTOINCREMENT,"
								"PlayerName TEXT NOT NULL"
								"); CREATE TABLE IF NOT EXISTS joinedPlayers ("
								"ID INTEGER PRIMARY KEY AUTOINCREMENT,"
								"PlayerName TEXT NOT NULL"
								");";

	rc = sqlite3_exec(db, sqlCreateTable, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        Plugin_Printf("SQL error: %s\n", errMsg);
        sqlite3_free(errMsg);
        return 1;

    } else {
        Plugin_Printf("Tables created successfully\n");

	}

    Plugin_AddCommand("vote", voteSystem, 1);

	return 0;

}

PCL void OnPlayerDC(client_t* client, const char* reason) {
	int rc = 0;
    const char *sql1 = "DELETE FROM joinedPlayers WHERE PlayerName = ?;";
    sqlite3_stmt *stmt;

    rc = sqlite3_prepare_v2(db, sql1, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
    	Plugin_PrintError("Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return;
    }

    sqlite3_bind_text(stmt, 1, Plugin_GetPlayerName(NUMFORCLIENT(client)), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
    	Plugin_PrintError("Execution failed: %s\n", sqlite3_errmsg(db));
    }

    Plugin_Printf("Vote Plugin: NOTICE: Disconnected player deleted from db");

    sqlite3_reset(stmt);

    const char* sql2 = "DELETE FROM voteTable WHERE PlayerName = ?;";

    rc = sqlite3_prepare_v2(db, sql2, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
      	Plugin_PrintError("Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return;
    }

    sqlite3_bind_text(stmt, 1, Plugin_GetPlayerName(NUMFORCLIENT(client)), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      	Plugin_PrintError("Execution failed: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);

}

PCL void OnClientCommand(client_t* client, const char* command) {
	if (strcasecmp(command, "vote") == 0) {

	}
}

PCL void OnClientEnterWorld(client_t* client) {
	checkAndInsertPlayer(client);

}

void* timer(void* arg) {
	struct timespec dur200000000 = {0, 200000000};
	struct timespec dur500000000 = {0, 500000000};

	while (runTimer) {
		if (voteInProgress) {
			while(difftime(time(NULL), startTime) < 30.0) {
				nanosleep(&dur200000000, NULL);

			}
			voteInProgress = 0;

		}
		nanosleep(&dur500000000, NULL);

	}

	return NULL;

}

void* cVoteTable(void* arg) {
	struct timespec dur500000000 = {0, 500000000};
	int isCleared = 1;

	while(1) {
		if(!voteInProgress && !isCleared) {
			clearVoteTable();
			isCleared = 1;

		} else if(voteInProgress) {
			isCleared = 0;

		}

		nanosleep(&dur500000000, NULL);

	}

	return NULL;

}

void invalidUse(int islot) {
	Plugin_ChatPrintf(islot, "Vote: ^1Incorrect usage, ^7usage: %s map <mapname>", Plugin_Cmd_Argv(0));

}

void voteSystem() {
	int slot = Plugin_Cmd_GetInvokerSlot();
	const char* playerName = Plugin_GetPlayerName(Plugin_Cmd_GetInvokerSlot());

	unsigned int matched = 0;
	int rc = 0;

	char* map = NULL;
	char* gameMode = NULL;

	int vPlayers = votedPlayers();
	if (vPlayers == -1) {
		Plugin_PrintError("Vote Plugin: Error occurred on reading the voted players.\n");
		return;

	}

	int aPlayers = allPlayers();
	if (aPlayers == -1) {
		Plugin_PrintError("Vote Plugin: Error occurred on reading the number of all players.\n");
		return;

	}

	if (Plugin_Cmd_Argc() == 4) {
		if (strcmp("map", Plugin_Cmd_Argv(1)) != 0) {
			invalidUse(slot);
			return;
		}

		const char* maps[9] = {
				"crash",
				"crossfire",
				"backlot",
				"strike",
				"vacant",
				"killhouse",
				"shipment",
				"bloc",
				"broadcast"
		};

		const char* mapTypes[3] = {
				"war", // Team Death Match
				"sd", // Search and Destroy
				"dm", // Free for All Death Match
		};

		for (int i = 0; i < sizeof(maps) / sizeof(maps[0]); i++) {
			if (strcasecmp(maps[i], Plugin_Cmd_Argv(2)) == 0) {
				matched = 1;
				map = (char*)maps[i];
				break;
			}

		}

		if (!matched) {
			invalidUse(slot);
			return;
		}

		matched = 0;
		for (int i = 0; i < sizeof(mapTypes) / sizeof(mapTypes[0]); i++) {
			if (strcasecmp(mapTypes[i], Plugin_Cmd_Argv(3)) == 0) {
				matched = 1;
				gameMode = (char*)mapTypes[i];
				break;

			}

		}

		if (!matched) {
			invalidUse(slot);
			return;
		}

		if(isPlayerVoted(playerName) == 1) {
			Plugin_ChatPrintf(slot, "Vote: You have already voted!\n");
			return;

		}

		if(voteInProgress) {
			Plugin_ChatPrintf(slot, "Vote: ^1Another vote is in progress.\n");
			return;

		} else {
			voteInProgress = 1;
			startTime = time(NULL);

		}


		Plugin_ChatPrintf(-1, "^4Vote ^7started for the map ^1%s^7! GameMode: ^1%s^7. Vote by typing $vote.", map, gameMode);

		char* sqlInsert = "INSERT INTO voteTable (PlayerName) VALUES (?);";
		sqlite3_stmt* stmt = NULL;

		rc = sqlite3_prepare_v2(db, sqlInsert, -1, &stmt, 0);
		if (rc != SQLITE_OK) {
			Plugin_PrintError("Failed to prepare insert statement: %s\n", sqlite3_errmsg(db));
			return;
		}

		sqlite3_bind_text(stmt, 1, playerName, -1, SQLITE_STATIC);

		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			Plugin_PrintError("Failed to prepare insert statement: %s\n", sqlite3_errmsg(db));
			return;

		}

		sqlite3_finalize(stmt);

		vPlayers++;
		Plugin_ChatPrintf(-1, "Players voted: %i of %i\n", vPlayers, aPlayers);


		if (
			aPlayers != 0 &&
			vPlayers >= aPlayers / 2
		) {
			Plugin_ChatPrintf(-1, "Changing map...\n");
			changeMap(map, gameMode);

		}

	} else if ((Plugin_Cmd_Argc() == 1) && voteInProgress) {
		if (isPlayerVoted(playerName) == 1) {
			Plugin_ChatPrintf(slot, "Vote: You have already voted!\n");
			return;

		}

		sqlite3_stmt* stmt = NULL;
		const char* sqlInsert = "INSERT INTO voteTable (PlayerName) VALUES (?);";

		rc = sqlite3_prepare_v2(db, sqlInsert, -1, &stmt, 0);
		if (rc != SQLITE_OK) {
			Plugin_PrintError("Failed to prepare insert statement: %s\n", sqlite3_errmsg(db));
			return;
		}

		sqlite3_bind_text(stmt, 1, playerName, -1, SQLITE_STATIC);

		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			Plugin_PrintError("Failed to prepare insert statement: %s\n", sqlite3_errmsg(db));
			return;

		}

		sqlite3_finalize(stmt);

		aPlayers = allPlayers();

		Plugin_ChatPrintf(-1, "Players voted: %i of %i\n", vPlayers, aPlayers);

		if (
			aPlayers != 0 &&
			vPlayers >= aPlayers / 2
		) {
			changeMap(map, gameMode);
			return;

		}

	} else {
		invalidUse(slot);
		return;

	}
}

int checkAndInsertPlayer(client_t* player) {
	int rc;
	const char *sqlCheck = "SELECT COUNT(*) FROM joinedPlayers WHERE PlayerName = ?;";
    sqlite3_stmt *stmt;

    const char* playerName = Plugin_GetPlayerName(NUMFORCLIENT(player));

    rc = sqlite3_prepare_v2(db, sqlCheck, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
    	Plugin_PrintError("Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    sqlite3_bind_text(stmt, 1, playerName, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        if (count == 0) {
        	sqlite3_reset(stmt);
            const char *sqlInsert = "INSERT INTO joinedPlayers (PlayerName) VALUES (?);";

            rc = sqlite3_prepare_v2(db, sqlInsert, -1, &stmt, 0);
            if (rc != SQLITE_OK) {
            	Plugin_PrintError("Failed to prepare insert statement: %s\n", sqlite3_errmsg(db));
                return rc;

            }

            sqlite3_bind_text(stmt, 1, playerName, -1, SQLITE_STATIC);

            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                Plugin_PrintError("Failed to insert player: %s\n", sqlite3_errmsg(db));
                sqlite3_finalize(stmt);
                return -1;

	        }

            sqlite3_finalize(stmt);

        } else {
            Plugin_Printf("Player '%s^7' already exists.\n", Plugin_GetPlayerName(NUMFORCLIENT(player)));
            sqlite3_finalize(stmt);
            return -1;

        }

    } else {
    	Plugin_PrintError("Failed to execute check statement: %s\n", sqlite3_errmsg(db));
    	sqlite3_finalize(stmt);
    	return -1;
    }


    return 0;
}

const int votedPlayers() {
	int rc = 0;
	const char* selectSql = "SELECT COUNT(*) FROM voteTable;";
	sqlite3_stmt* stmt = NULL;
	int result = 0;

	rc = sqlite3_prepare_v2(db, selectSql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		Plugin_PrintError("Failed to prepare player select statement: %s\n", sqlite3_errmsg(db));
		return -1;
	}

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		Plugin_PrintError("Failed to execute statement: %s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}

	result = sqlite3_column_int(stmt, 0);

	sqlite3_finalize(stmt);

	return result;

}

const int allPlayers() {
	int rc = 0;
	const char* selectSql = "SELECT COUNT(*) FROM joinedPlayers;";
	sqlite3_stmt* stmt = NULL;
	int result = 0;

	rc = sqlite3_prepare_v2(db, selectSql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		Plugin_PrintError("Failed to prepare player select statement: %s\n", sqlite3_errmsg(db));
		return -1;
	}

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		Plugin_PrintError("Failed to execute statement: %s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}

	result = sqlite3_column_int(stmt, 0);

	sqlite3_finalize(stmt);

	return result;
}

void changeMap(const char* map, const char* mode) {
	const char gametypeCmd[16] = "set g_gametype ";
	const char mapNameCmd[8] = "map mp_";
	char result[128];

	memset(result, 0, sizeof(result));
	strcpy(result, gametypeCmd);
	strcat(result, mode);
	strcat(result, "; ");
	strcat(result, mapNameCmd);
	strcat(result, map);
	strcat(result, ";");

	Plugin_Printf(result);
	Plugin_Cbuf_AddText(result);

}

PCL void OnInfoRequest(pluginInfo_t *info){

    info->handlerVersion.major = PLUGIN_HANDLER_VERSION_MAJOR;
    info->handlerVersion.minor = PLUGIN_HANDLER_VERSION_MINOR;

    info->pluginVersion.major = Cod4x_Vote_Plugin_VERSION_MAJOR;
    info->pluginVersion.minor = Cod4x_Vote_Plugin_VERSION_MINOR;
    strncpy(info->fullName,"Cod4X Vote Plugin",sizeof(info->fullName));
    strncpy(info->shortDescription,"Vote Plugin for changing maps.",sizeof(info->shortDescription));
    strncpy(info->longDescription,"This plugin is used to change the map of the game. Coded my LM40 ( DevilHunter )",sizeof(info->longDescription));
}

int deleteAllTables() {
    char* errMsg = NULL;
    int rc;
    const char* sqlDelete = "DELETE FROM joinedPlayers; DELETE FROM voteTable;";

    rc = sqlite3_exec(db, sqlDelete, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
    	Plugin_PrintError("Failed to delete all players: %s\n", errMsg);
        sqlite3_free(errMsg);
        return -1;

    }

    Plugin_Printf("All tables deleted successfully.\n");
    return 0;
}

int clearVoteTable() {
	char* errMsg = NULL;
    int rc;
    const char* sqlDelete = "DELETE FROM voteTable;";

	rc = sqlite3_exec(db, sqlDelete, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
    	Plugin_PrintError("Failed to delete all players: %s\n", errMsg);
        sqlite3_free(errMsg);
        return -1;

    }

    Plugin_Printf("Vote table deleted successfully.\n");
    return 0;

}

const int isPlayerVoted(const char* playerName) {
	const char* sqlFind = "SELECT * FROM voteTable WHERE PlayerName = ?;";
	sqlite3_stmt* stmt;
	int rc = 0;

	rc = sqlite3_prepare_v2(db, sqlFind, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		Plugin_PrintError("Failed to prepare player find statement: %s\n", sqlite3_errmsg(db));
		return -1;

	}

	sqlite3_bind_text(stmt, 1, playerName, -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return 1;

	}

	sqlite3_finalize(stmt);
	return 0;
}

PCL void OnTerminate() {
	runTimer = 0;

	deleteAllTables();
	sqlite3_close(db);

}
