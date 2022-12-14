#include <nds.h>
#include <fat.h>

#include <dswifi9.h>
#include <netdb.h>
#include <netinet/in.h>

#include <stdio.h>

#include "parse.h"

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
const size_t downloadBufferSize = 8192;

enum APP_STATE {
    INIT,
    ADDR_INPUT,
    CONNECT_WIFI,
    CONNECT_SER,
    REQUEST,
    PARSE,
    DOWNLOAD,
    DEINIT,
};
enum APP_STATE app_state = INIT;

void deinit() {
    if (INIT < app_state) {
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
    if (do_perror) perror("perror");
    iprintf(RED);
    iprintf("%s\n", msg);
    switch (app_state) {
        case INIT:         puts("while starting up");          break;
        case CONNECT_WIFI: puts("while connecting to wifi");   break;
        case ADDR_INPUT:   puts("while reading address");      break;
        case CONNECT_SER:  puts("while connecting to server"); break;
        case REQUEST:      puts("while sending request");      break;
        case PARSE:        puts("while parsing response");     break;
        case DOWNLOAD:     puts("while downloading file");     break;
        case DEINIT:       puts("while wrapping up");          break;
        default:           printf("while in state %d\n", app_state);  break;
    }
    iprintf(RESET);
    deinit();
    iprintf(CYN "Press start to shut down\n" RESET);
    while(1) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_START) chattyShutdown();
    }
}

void keypress(int key) {
   if(key > 0)
      iprintf("%c", key);
}

int main() {
    consoleDemoInit();
    defaultExceptionHandler();

    const s16 emulatorName[10] = {'D','e','S','m','u','M','E'};
    const bool inEmulator = ! memcmp(emulatorName, PersonalData->name,
            PersonalData->nameLen < 10 ? PersonalData->nameLen : 10);
    if (inEmulator) {
        puts(YEL "Running in emulator" RESET);
    }

    Keyboard *keyboard = keyboardDemoInit();
    if (NULL == keyboard) {
        errorShutdown(true, "Couldn't set up keyboard");
    }
    keyboard->OnKeyPressed = keypress;

    if ( ! inEmulator && ! fatInitDefault()) {
        errorShutdown(true, "Couldn't access storage");
    }



    app_state = ADDR_INPUT;
    char target_ip[256] = "192.168.1."; // FIXME: buffer overflow?
    char target_port[256] = {0}; // FIXME: buffer overflow?
    {
        size_t buffSize = 256;
        char *after_default_ip = target_ip + strlen(target_ip);

        keyboardShow();

        puts(CYN "What IP to download from?" RESET);
        iprintf("> %s", target_ip);
        if ( ! scanf("%3[^\n]", after_default_ip)) { // FIXME buffer overflow?
            memset(target_ip, 0, buffSize);
            iprintf("> ");
            scanf("%s", target_ip); // FIXME buffer overflow?
        }

        puts(CYN "What PORT to connect on?" RESET);
        iprintf("> ");
        scanf("%s", target_port); // FIXME buffer overflow?

        // TODO: load saved addr and port
        // TODO: ask user for ip to connect to
        // TODO: ask user for port to connect to
        // TODO: save addr and port to file
        keyboardHide();
    }



    app_state = CONNECT_WIFI;
    {
        if ( ! inEmulator && ! Wifi_InitDefault(WFC_CONNECT)) {
            errorShutdown(true, "Couldn't connect to wifi");
        }
        iprintf("Connected to wifi\n");
        struct in_addr ip, gateway, mask, dns1, dns2;
        ip = Wifi_GetIPInfo(&gateway, &mask, &dns1, &dns2);
        iprintf(WHT "\tip     : %s\n" RESET, inet_ntoa(ip) );
        //iprintf(WHT "\tgateway: %s\n" RESET, inet_ntoa(gateway) );
        //iprintf(WHT "\tmask   : %s\n" RESET, inet_ntoa(mask) );
        //iprintf(WHT "\tdns1   : %s\n" RESET, inet_ntoa(dns1) );
        //iprintf(WHT "\tdns2   : %s\n" RESET, inet_ntoa(dns2) );
    }



    app_state = CONNECT_SER;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if ( ! inEmulator && -1 == sock) {
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
        if ( ! inEmulator && -1 == connect(sock, (struct sockaddr *) &addr, sizeof(addr))) {
            errorShutdown(true, "Couldn't connect to server");
        }
        iprintf(GRN "Connected\n" RESET);
    }



    app_state = REQUEST;
    {
        iprintf("Making request...\n");
        char request[128]; // FIXME buffer overflow?
        sprintf(request, "GET /%s HTTP/1.1\n\n", fileName);
        ssize_t sent = send(sock, request, strlen(request), 0);
        if ( ! inEmulator && -1 == sent) {
            errorShutdown(true, "Couldn't send request");
        } else if ( ! inEmulator && strlen(request) != sent) {
            errorShutdown(true, "Sent wrong number of bytes");
        }
        iprintf("Request sent\n");
    }



    app_state = PARSE;
    size_t writeCount = 0;
    FILE *file = fopen(fileName, "wb");
    if (NULL == file) {
        errorShutdown(true, "Unable to create download file");
    }
    {
        struct window resp;
        if( 0 != recv_parse_loop(&resp, sock) ) {
            errorShutdown(false, "Error in recv-parse loop");
        }
        iprintf("Parsed to body\n");
        // write bytes already in the buffer from the parsing step
        if (resp.v.viewSize != fwrite(resp.v.view, sizeof(char), resp.v.viewSize, file)) {
            if (EOF == fclose(file)) perror("perror Warning closing file");
            errorShutdown(true, "Couldn't write bytes from parsing step");
        }
        writeCount = resp.v.viewSize;
    }



    app_state = DOWNLOAD;
    {
        ssize_t recvd;
        char buffer[downloadBufferSize];
        iprintf("Downloading\n");
        do {
            recvd = recv(sock, buffer, downloadBufferSize, 0);
            if (-1 == recvd) {
                if (EOF == fclose(file)) perror("perror Warning closing file");
                if (-1 == close(sock)) perror("perror Warning closing socket");
                errorShutdown(true, "Couldn't receive");
            }
            if (recvd != fwrite(buffer, sizeof(char), recvd, file)) {
                if (EOF == fclose(file)) perror("perror Warning closing file");
                if (-1 == close(sock)) perror("perror Warning closing socket");
                errorShutdown(true, "Couldn't write file");
            }
            writeCount += recvd;
        } while (0 < recvd);
        iprintf("Done downloading: %dK\n", writeCount / 1000);
        if (EOF == fclose(file)) perror("perror Warning closing file");
        iprintf("File closed\n");
        if (-1 == shutdown(sock, 2)) perror("perror Warning closing socket");
        iprintf("Socket closed\n");
        Wifi_DisableWifi();
        iprintf("Wifi disabled\n");
    }



    app_state = DEINIT;
    puts("Cleaning up...");
    deinit();
    puts(GRN "Press start to shut down" RESET);
    while(1) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_START) break;
    }
    chattyShutdown();
    return 0;
}
