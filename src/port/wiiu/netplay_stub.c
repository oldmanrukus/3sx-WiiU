/**
 * @file netplay_stub.c
 * @brief Netplay stubs for Wii U — satisfies linker without SDL_net/GekkoNet
 *
 * The initial Wii U port doesn't support netplay. These stubs prevent
 * undefined reference errors from main.c's calls to Netplay_*.
 */
#ifdef NETPLAY_STUB

#include "netplay/netplay.h"
#include <string.h>

void Netplay_SetParams(int player, const char* ip) { (void)player; (void)ip; }
void Netplay_BeginDirectP2P(void) {}
void Netplay_TickDirectP2P(void) {}
void Netplay_SetMatchmakingParams(const char* server_ip, int server_port) {
    (void)server_ip; (void)server_port;
}
void Netplay_BeginMatchmaking(void) {}
void Netplay_TickMatchmaking(void) {}
bool Netplay_IsMatchmakingPending(void) { return false; }
void Netplay_CancelMatchmaking(void) {}
void Netplay_Run(void) {}
NetplaySessionState Netplay_GetSessionState(void) { return NETPLAY_SESSION_IDLE; }
void Netplay_HandleMenuExit(void) {}
void Netplay_GetNetworkStats(NetworkStats* stats) {
    if (stats) memset(stats, 0, sizeof(*stats));
}

/* Stub the netplay screen renderer too */
void NetplayScreen_Render(void) {}
void NetstatsRenderer_Render(void) {}

#endif /* NETPLAY_STUB */
