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

const char *fatName = "fat:";
const char *fileName = "testprogram.nds";
const size_t downloadBufferSize = 4096;

enum STATE {
    INIT,
    CONNECT_WIFI,
    ADDR_INPUT,
    CONNECT,
    REQUEST,
    DOWNLOAD,
    DEINIT,
};
enum STATE state = INIT;

void deinit() {
    if (INIT < state) {
        char label[32] = {0}; // FIXME buffer overflow?
        fatGetVolumeLabel(fatName, label);
        printf("Unmounting fat: %s\n", label);
        fatUnmount(fatName);
        fatGetVolumeLabel(fatName, label);
        printf(WHT "\t fat now has label: %s\n" RESET, label);
    }
}

void chattyShutdown() {
    iprintf(CYN "Goodbye\n" RESET);
    swiWaitForVBlank();
    systemShutDown();
}

void errorShutdown(bool do_perror, char *msg) {
    iprintf(YEL);
    switch (state) {
        case INIT:         printf("Error starting up:\n");          break;
        case CONNECT_WIFI: printf("Error connecting to wifi:\n");   break;
        case ADDR_INPUT:   printf("Error reading address:\n");      break;
        case CONNECT:      printf("Error connecting to server:\n"); break;
        case REQUEST:      printf("Error sending request:\n");      break;
        case DOWNLOAD:     printf("Error receiving response:\n");   break;
        case DEINIT:       printf("Error wrapping up:\n");          break;
        default:           printf("Error in state %d", state);      break;
    }
    iprintf(RED);
    iprintf("%s\n", msg);
    if (do_perror) perror("perror");
    iprintf(RESET);
    deinit();
    iprintf(GRN "Press start to shut down\n" RESET);
    while(1) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_START) chattyShutdown();
    }
}

int main() {
    consoleDemoInit();

    const s16 emulatorName[10] = {'D','e','S','m','u','M','E'};
    const bool inEmulator = ! memcmp(emulatorName, PersonalData->name,
            PersonalData->nameLen < 10 ? PersonalData->nameLen : 10);
    if (inEmulator) iprintf("!! Running in emulator !!\n");

    iprintf("Install exception handler...\n");
    defaultExceptionHandler();

    iprintf("Load keyboard demo...\n");
    Keyboard *keyboard = keyboardDemoInit();
    if (NULL == keyboard) {
        errorShutdown(true, "Couldn't set up keyboard");
    }

    iprintf("Mount fat partition...\n");
    if ( ! inEmulator) {
        if ( ! fatInitDefault()) {
            errorShutdown(true, "Couldn't access storage");
        }
    }

    iprintf(GRN "Start\n" RESET);




    state = CONNECT_WIFI;
    if ( ! inEmulator) {
        if ( ! Wifi_InitDefault(WFC_CONNECT)) {
            errorShutdown(true, "Couldn't connect to wifi");
        }
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
        errorShutdown(true, "Couldn't make a socket");
    }
    {
        unsigned long ip = inet_addr(target_ip);
        if (INADDR_NONE == ip) {
            errorShutdown(true, "Couldn't convert string to address");
        }
        struct sockaddr_in addr = (struct sockaddr_in){
            .sin_family = AF_INET,
            .sin_port = htons(atol(target_port)),
            .sin_addr = (struct in_addr){.s_addr = ip},
        };
        iprintf("Connecting to server...\n");
        iprintf(WHT "\taddr   : %s\n" RESET, inet_ntoa(addr.sin_addr));
        iprintf(WHT "\tport   : %hd\n" RESET, ntohs(addr.sin_port));
        if ( ! inEmulator ) {
            if (-1 == connect(sock, (struct sockaddr *) &addr, sizeof(addr))) {
                errorShutdown(true, "Couldn't connect");
            }
        }
        iprintf(GRN "Connected\n" RESET);
    }


    state = REQUEST;
    {
        iprintf("Making request...\n");
        char request[128]; // FIXME buffer overflow?
        sprintf(request, "GET /%s HTTP/1.1\n\n", fileName);
        ssize_t sent = send(sock, request, strlen(request), 0);
        if (-1 == sent) {
            errorShutdown(true, "Couldn't send request");
        } else if (strlen(request) != sent) {
            errorShutdown(true, "Sent wrong number of bytes");
        }
        iprintf(GRN "Request sent\n" RESET);
    }


    state = DOWNLOAD;
    {
        FILE *file = fopen(fileName, "wb");
        if (NULL == file) {
            errorShutdown(true, "Unable to create download file");
        }
        ssize_t recvd;
        char buffer[downloadBufferSize];
        iprintf("Receiving\n");
        // FIXME: parse, make sure it's a 200, look for the newlines .. then
        // start writing the file
        do {
            recvd = recv(sock, buffer, downloadBufferSize, 0);
            if (-1 == recvd) {
                if (0 != fclose(file)) perror("perror Warning closing file");
                errorShutdown(true, "Couldn't receive");
            }
            iprintf(".");
            if (recvd != fwrite(buffer, sizeof(char), recvd, file)) {
                if (0 != fclose(file)) perror("perror Warning closing file");
                errorShutdown(true, "Couldn't write file");
            }
            fflush(stdout);
        } while (0 < recvd);
        iprintf("\nDone receiving\n");
        if (-1 == close(sock)) perror("perror Warning closing socket");
        iprintf("\nSocket closed\n");
        if (0 != fclose(file)) perror("perror Warning closing file");
        iprintf(GRN "\nFile closed\n" RESET);
    }


    state = DEINIT;
    iprintf("Cleaning up...");
    deinit();
    iprintf(GRN "Press start to shut down\n" RESET);
    while(1) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_START) break;
    }
    chattyShutdown();
    return 0;
}
