int min(int a, int b);
void debugBuff(int width, char buff[], size_t buffSize);

// WINDOW/VIEW ABSTRACTION

struct view {
    size_t viewSize;
    char *view;
};
struct window {
    size_t buffSize;
    char buff[4096];
    struct view v;
};
void window_init(struct window *w);

// FUNCTIONS FOR INPUTTING DATA TO WINDOW/VIEW

struct view window_shift_left(struct window *w);
void window_extend_view(struct window *w, const ssize_t count);

// FUNCTIONS FOR PARSING DATA IN WINDOW/VIEW

enum WINDOW_RESULT {
    WINDOW_NEED_INPUT = -2,
    WINDOW_NO_MATCH = -1,
    WINDOW_OK = 0,
};
enum WINDOW_RESULT window_consume_token(
        struct window *w, const char tok[], const size_t tokSize);
enum WINDOW_RESULT window_consume_byte(struct window *w);

// HTTP RESPONSE PARSER USING WINDOW/VIEW

enum PARSER_STATE {
    HTTP_VER = 101,
    HTTP_VER_delim,
    STATUS_CODE,
    STATUS_CODE_delim,
    REASON,
    HEADERS,
    HEADER_NAME,
    HEADER_NAME_delim,
    HEADER_VALUE,
    BODY,
};
enum WINDOW_RESULT http_parser( struct window *w, enum PARSER_STATE *state);
int recv_parse_loop(struct window *w, int sock);
