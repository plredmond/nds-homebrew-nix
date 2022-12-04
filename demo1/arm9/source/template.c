#include <nds.h>
#include <fat.h>

#include <dswifi9.h>
#include <netdb.h>
#include <netinet/in.h>

#include <stdio.h>

#define RED   "\x1B[31m"
#define GRN   "\x1B[32m"
#define YEL   "\x1B[33m"
#define BLU   "\x1B[34m"
#define MAG   "\x1B[35m"
#define CYN   "\x1B[36m"
#define WHT   "\x1B[37m"
#define RESET "\x1B[39m"

enum STATE {
    INIT,
    CHECK_EMU,
    CONNECT_WIFI,
    ADDR_INPUT,
    CONNECT,
    REQUEST,
    DOWNLOAD,
    DEINIT,
};
enum STATE state = INIT;

void chattyShutdown() {
    iprintf(YEL "Shut down\n" RESET);
    swiWaitForVBlank();
    systemShutDown();
}

void errorShutdown(char *msg) {
    switch (state) {
        case INIT:
            printf("Error starting up:\n");
            break;
        case CHECK_EMU:
            printf("Error emulator status:\n");
            break;
        case CONNECT_WIFI:
            printf("Error connecting to wifi:\n");
            break;
        case ADDR_INPUT:
            printf("Error reading address:\n");
            break;
        case CONNECT:
            printf("Error connecting to server:\n");
            break;
        case REQUEST:
            printf("Error sending request:\n");
            break;
        case DOWNLOAD:
            printf("Error receiving response:\n");
            break;
        case DEINIT:
            printf("Error wrapping up:\n");
            break;
        default:
            printf("Error in state %d", state);
            break;
    }
    {
        iprintf(RED);
        iprintf("%s\n", msg);
        perror("perror");
        iprintf(RESET);
    }
    iprintf(WHT "Press start to shut down\n" RESET);
    while(1) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_START) chattyShutdown();
    }
}

int main() {
    defaultExceptionHandler();
    consoleDemoInit();
    keyboardDemoInit();
    iprintf(GRN "Start\n" RESET);


    state = CHECK_EMU;
    const s16 emulatorName[10] = {'D','e','S','m','u','M','E'};
    const bool inEmulator = !memcmp(emulatorName, PersonalData->name,
            PersonalData->nameLen < 10 ? PersonalData->nameLen : 10);
    if (inEmulator) iprintf("Running in emulator\n");


    state = CONNECT_WIFI;
    if (!Wifi_InitDefault(WFC_CONNECT) && !inEmulator) {
        errorShutdown("Couldn't connect to wifi");
    }
    {
        iprintf("Connected to wifi\n");
        struct in_addr ip, gateway, mask, dns1, dns2;
        ip = Wifi_GetIPInfo(&gateway, &mask, &dns1, &dns2);
        iprintf(WHT "\tip     : %s\n" RESET, inet_ntoa(ip) );
        //iprintf(WHT "\tgateway: %s\n" RESET, inet_ntoa(gateway) );
        //iprintf(WHT "\tmask   : %s\n" RESET, inet_ntoa(mask) );
        //iprintf(WHT "\tdns1   : %s\n" RESET, inet_ntoa(dns1) );
        //iprintf(WHT "\tdns2   : %s\n" RESET, inet_ntoa(dns2) );
    }


    state = ADDR_INPUT;
    keyboardShow();
    // TODO: load saved addr and port
    // TODO: ask user for ip to connect to
    // TODO: ask user for port to connect to
    // TODO: save addr and port to file
    keyboardHide();
    char *target_ip = "192.168.1.3";
    char *target_port = "8000";


    state = CONNECT;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == sock) {
        errorShutdown("Couldn't make a socket");
    }
    {
        unsigned long ip = inet_addr(target_ip);
        if (INADDR_NONE == ip) {
            errorShutdown("Couldn't convert string to address");
        }
        struct sockaddr_in addr = (struct sockaddr_in){
            .sin_family = AF_INET,
            .sin_port = htons(atol(target_port)),
            .sin_addr = (struct in_addr){.s_addr = ip},
        };
        iprintf("Connecting to server...\n");
        iprintf(WHT "\taddr   : %s\n" RESET, inet_ntoa(addr.sin_addr));
        iprintf(WHT "\tport   : %hd\n" RESET, ntohs(addr.sin_port));
        if (-1 == connect(sock, (struct sockaddr *) &addr, sizeof(addr))) {
            errorShutdown("Couldn't connect");
        }
        iprintf(GRN "Connected\n" RESET);
    }


    state = REQUEST;
    {
        iprintf("Making request...\n");
        char *request = "GET /testprogram.nds HTTP/1.1\n\n";
        ssize_t sent = send(sock, request, strlen(request), 0);
        if (-1 == sent) {
            errorShutdown("Couldn't send request");
        } else if (strlen(request) != sent) {
            errorShutdown("Sent wrong number of bytes");
        }
        iprintf(GRN "Request sent\n" RESET);
    }


    state = DOWNLOAD;
    // TODO: download stuff
    // TODO: write to fat

    state = DEINIT;
    if (-1 == close(sock)) {
        errorShutdown("Couldn't close socket"); // TODO: reduce to a warning
    }

    while(1) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_START) break;
    }
    chattyShutdown();
    return 0;
}
