#include <netinet/in.h>

#include <string.h>
#include <stdio.h>

#include "parse.h"

int min(int a, int b) {
    return a < b ? a : b;
}

void debugBuff(int width, char buff[], size_t buffSize) {
    for(int start=0; start<buffSize; start+=width) {
        int end = min(start + width, buffSize);
        for(int i=start; i<end; i+=1) {
            switch (buff[i]) {
                case ' ':
                    iprintf("__");
                    break;
                case '\0':
                    iprintf("!!");
                    break;
                case '\n':
                    iprintf("\\n");
                    break;
                case '\r':
                    iprintf("\\r");
                    break;
                default:
                    iprintf("%2c", buff[i]);
            }
            putc(' ', stdout);
        }
        putc('\n', stdout);
        for(int i=start; i<end; i+=1) {
            iprintf("%02x ", buff[i]);
        }
        putc('\n', stdout);
    }
}

// WINDOW/VIEW ABSTRACTION

void window_init(struct window *w) {
    w->buffSize = 4096; // XXX when debugging the parser, set this small
    w->v.viewSize = 0;
    w->v.view = memset(w->buff, 0, w->buffSize);
}

// FUNCTIONS FOR INPUTTING DATA TO WINDOW/VIEW

struct view window_shift_left(struct window *w) {
    // move the in-view content to the beginning of buffer
    w->v.view = memmove(w->buff, w->v.view, w->v.viewSize);
    // return the writable the part of the buffer as a view
    return (struct view) {
        .view = w->buff + w->v.viewSize,
        .viewSize = w->buffSize - w->v.viewSize,
    };
    // idempotent
}
void window_extend_view(struct window *w, const ssize_t count) {
    size_t space = w->buffSize - w->v.viewSize;
    if (space < count) {
        puts("error: window_extend_view given a too-large count; somebody else might have written past the end of our buffer");
        return;
    }
    w->v.viewSize += count;
}

// FUNCTIONS FOR PARSING DATA IN WINDOW/VIEW

enum WINDOW_RESULT window_consume_token(
        struct window *w, const char tok[], const size_t tokSize) {
    if (w->v.viewSize < tokSize) {
        return WINDOW_NEED_INPUT;
    } else if (0 != strncmp(w->v.view, tok, tokSize)) {
        return WINDOW_NO_MATCH;
    }
    // found token, so advance the view
    w->v.view += tokSize;
    w->v.viewSize -= tokSize;
    return WINDOW_OK;
}
enum WINDOW_RESULT window_consume_byte(struct window *w) {
    const size_t count = 1; // XXX consuming more than one byte could break tokens
    if (w->v.viewSize < count) {
        return WINDOW_NEED_INPUT;
    }
    // advance the view
    w->v.view += count;
    w->v.viewSize -= count;
    return WINDOW_OK;
}

// HTTP RESPONSE PARSER USING WINDOW/VIEW

enum WINDOW_RESULT http_parser(
        struct window *w, enum PARSER_STATE *state) {
    enum WINDOW_RESULT retval = 1;
    // FIXME: can loop infinitely in the "N TIMES" states
    //iprintf("debug: parser state %d\n", *state);
    switch (*state) {

        case HTTP_VER: // MATCH STRING ONCE
            retval = window_consume_token(w, "HTTP/1.0", strlen("HTTP/1.0"));
            if (WINDOW_OK == retval) {
                *state = HTTP_VER_delim;
            }
            break;
        case HTTP_VER_delim: // MATCH STRING N TIMES
            retval = window_consume_token(w, " ", strlen(" "));
            if (WINDOW_NO_MATCH == retval) {
                *state = STATUS_CODE;
                retval = WINDOW_OK;
            }
            break;

        case STATUS_CODE: // MATCH STRING ONCE
            retval = window_consume_token(w, "200", strlen("200"));
            if (WINDOW_OK == retval) {
                *state = STATUS_CODE_delim;
            }
            break;
        case STATUS_CODE_delim: // MATCH STRING N TIMES
            retval = window_consume_token(w, " ", strlen(" "));
            if (WINDOW_NO_MATCH == retval) {
                *state = REASON;
                retval = WINDOW_OK;
            }
            break;

        case REASON: // MATCH STRING ONCE
            retval = window_consume_token(w, "OK\r\n", strlen("OK\r\n"));
            if (WINDOW_OK == retval) {
                *state = HEADERS;
            }
            break;

        case HEADERS: // CHOICE -- MATCH STRING ONCE
            retval = window_consume_token(w, "\r\n", strlen("\r\n"));
            if (WINDOW_OK == retval) {
                *state = BODY;
            } else if (WINDOW_NO_MATCH == retval) {
                *state = HEADER_NAME;
                retval = WINDOW_OK;
            }
            break;

        case HEADER_NAME: // DO-NOT MATCH STRING N TIMES
            retval = window_consume_token(w, ":", strlen(":"));
            if (WINDOW_OK == retval) {
                *state = HEADER_NAME_delim;
            } else if (WINDOW_NO_MATCH == retval) {
                retval = window_consume_byte(w);
            }
            break;
        case HEADER_NAME_delim: // MATCH STRING N TIMES
            retval = window_consume_token(w, " ", strlen(" "));
            if (WINDOW_NO_MATCH == retval) {
                *state = HEADER_VALUE;
                retval = WINDOW_OK;
            }
            break;

        case HEADER_VALUE: // DO-NOT MATCH STRING N TIMES
            retval = window_consume_token(w, "\r\n", strlen("\r\n"));
            if (WINDOW_OK == retval) {
                *state = HEADERS;
            } else if (WINDOW_NO_MATCH == retval) {
                retval = window_consume_byte(w);
            }
            break;

        case BODY:
            // caller should recognize this state as "done"
            break;
    }
    return retval;
}

int recv_parse_loop(struct window *w, int sock) {
    window_init(w);

    enum PARSER_STATE state = HTTP_VER;

    ssize_t readCount = 0;
    do {
        //puts("debug: window buff");
        //debugBuff(16, w->buff, w->buffSize);
        //puts("debug: window view");
        //debugBuff(11, w->v.view, w->v.viewSize);

        switch(http_parser(w, &state)) {
            case WINDOW_NEED_INPUT:
                //puts("debug: WINDOW_NEED_INPUT");
                // shift the view, write into the buffer, and extend the view
                struct view writeable = window_shift_left(w);
                readCount = recv(sock, writeable.view, writeable.viewSize, 0);
                if (-1 == readCount) {
                    perror("couldn't read");
                    return 1;
                } else if (0 == readCount) {
                    puts("warning: parser wanted more data but no bytes read; parser buffer too small?");
                }
                window_extend_view(w, readCount);
                break; // don't skip the loop condition
            case WINDOW_NO_MATCH:
                puts("error: unexpected token");
                return 1;
            case WINDOW_OK:
                //iprintf("debug: WINDOW_OK, state %d\n", state);
                if (BODY == state)
                    return 0; // success
                else
                    break; // more parsing to do
            default:
                puts("error: unexpected parser result");
                return 1;
        }
    } while (readCount);
    puts("warning: read 0 bytes (eof)");
    return 2;
}
