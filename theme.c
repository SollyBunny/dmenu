#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/inotify.h>

#include "theme.h"

// Utils

static inline bool isAlpha(char c) {
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 'A' && c <= 'Z') return true;
    if (c == '-') return true;
    return false;
}
static inline bool isSpace(char c) {
    return (c == ' ' || c == '\t' || c == '\n');
}
static inline uint8_t hexChar(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
static inline uint8_t clampedAdd(uint8_t a, int8_t b) {
    if (b > 0) {
        uint8_t c = 255 - a;
        if (b > c) return 255;
    } else if (b > a) return 0;
    return a + b;
}
static inline float _fabs(float f) {
    if (f < 0) return -f;
    return f;
}
static inline float _fmod(float f, float g) {
    while (f > g) f -= g;
    while (f < 0) f += g;
    return f;
}
static inline void hsl_to_rgb(float h, float s, float l, float* r, float* g, float* b) {
    float c = (1.0 - _fabs(2.0 * l - 1.0)) * s;
    float x = c * (1.0 - _fabs(_fmod(h * 6.0, 2.0) - 1.0));
    float m = l - c / 2.0;
    if (h * 6.0 < 1.0) {
        *r = c;
        *g = x;
        *b = 0;
    } else if (h * 6.0 < 2.0) {
        *r = x;
        *g = c;
        *b = 0;
    } else if (h * 6.0 < 3.0) {
        *r = 0;
        *g = c;
        *b = x;
    } else if (h * 6.0 < 4.0) {
        *r = 0;
        *g = x;
        *b = c;
    } else if (h * 6.0 < 5.0) {
        *r = x;
        *g = 0;
        *b = c;
    } else {
        *r = c;
        *g = 0;
        *b = x;
    }
    *r += m;
    *g += m;
    *b += m;
}

static char *fileRead(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        perror("Error opening file");
        return NULL;
    }
    off_t length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    char *content = (char*)malloc(length + 1);
    if (content == NULL) {
        close(fd);
        perror("Error allocating memory");
        return NULL;
    }
    if (read(fd, content, length) == -1) {
        free(content);
        close(fd);
        perror("Error reading file");
        return NULL;
    }
    content[length] = '\0';
    close(fd);
    return content;
}

// Binary search tree

static ThemeNode *themeBSTCreate() {
    ThemeNode *n = calloc(1, sizeof(ThemeNode));
    if (n == NULL) {
        perror("calloc");
        return NULL;
    }
    return n;
}
static void themeBSTDestroy(ThemeNode *head) {
    if (head->l) themeBSTDestroy(head->l);
    if (head->r) themeBSTDestroy(head->r);
    if (head->parsed) {
        switch (head->type) {
            case THEME_TYPE_COLOR:
                if (head->data.color->alloced) free(head->data.color);
                break;
            case THEME_TYPE_FONT:
                if (head->data.font->alloced) free(head->data.font);
                break;
            case THEME_TYPE_SCHEME:
                #define FREECHILD(name) { if (head->data.scheme->name->alloced) free(head->data.scheme->name); }
                FREECHILD(font);
                FREECHILD(main);
                FREECHILD(dim); FREECHILD(dimmer);
                FREECHILD(bright); FREECHILD(brighter);
                FREECHILD(bg); FREECHILD(bgdim); FREECHILD(bgbright);
                FREECHILD(ol); FREECHILD(oldim); FREECHILD(olbright);
                if (head->data.scheme->alloced) free(head->data.scheme);
                break;
        }
    }
    free(head);
}
static ThemeNode *themeBSTAdd(ThemeNode *head, ThemeNode *child) {
    if (head == NULL) return child;
    if (child == NULL) return head;
    ThemeNode *node = head;
    while (1) {
        int v = strcmp(node->name, child->name);
        if (v == 0) {
            return NULL;
        } else if (v < 0) {
            if (node->r) {
                node = node->r;
            } else {
                node->r = child;
                return head;
            }
        } else {
            if (node->l) {
                node = node->l;
            } else {
                node->l = child;
                return head;
            }
        }
    }
}

static ThemeNode *themeBSTFind(ThemeNode *head, const char *name) {
    ThemeNode *node = head;
    while (node) {
        int v = strcmp(node->name, name);
        if (v == 0)
            return node;
        else if (v > 0)
            node = node->l;
        else
            node = node->r;
    }
    return NULL;
}

// Parsing

#define MAXDEPTH 20

int themeGetDepth(Theme *theme, const char *name, uint8_t type, void *out, uint8_t depth);

int themeParseNum(Theme *theme, const char *str, float *out, uint8_t depth) {
    if (depth > MAXDEPTH) return THEME_ERROR_TOO_DEEP;
    if (str) {
        if (str[0] == '@')
            return themeGetDepth(theme, str + 1, THEME_TYPE_NUM, out, depth + 1);
        char *temp;
        float v = strtof(str, &temp);
        if (temp) {
            if (strcmp(temp, "deg") == 0) v /= 360;
            else if (strcmp(temp, "%") == 0) v /= 100;
            else if (strcmp(temp, "b") == 0) v /= 255;
            else if (strcmp(temp, "rad") == 0) v /= 6.28318530718;
        }
        *out = v;
    } else *out = 0;
    return THEME_ERROR_OKAY;
}

int themeParseStr(Theme *theme, char *str, char **out, uint8_t depth) {
    if (depth > MAXDEPTH) return THEME_ERROR_TOO_DEEP;
    if (str) {
        if (str[0] == '@')
            return themeGetDepth(theme, str + 1, THEME_TYPE_STR, out, depth + 1);
        else *out = str;
    } else *out = NULL;
    return THEME_ERROR_OKAY;
}

int themeParseColor(Theme *theme, char *str, ThemeColor *out, uint8_t depth) {
    if (depth > MAXDEPTH) return THEME_ERROR_TOO_DEEP;
    out->a = 255;
    if (!str) { invalid_string:
        out->r = out->g = out->b = 0;
        return THEME_ERROR_OKAY;
    }
    int length = strlen(str);
    if (length == 0) goto invalid_string;
    if (str[0] == '@') {
        return themeGetDepth(theme, str + 1, THEME_TYPE_COLOR, out, depth + 1);
    } else if (str[0] == '#') {
        switch (length) {
            case 5: // #rgba
                out->a = hexChar(str[4]);
                out->a += out->a * 16;
            case 4: // #rgb
                out->r = hexChar(str[1]);
                out->r += out->r * 16;
                out->g = hexChar(str[2]);
                out->g += out->g * 16;
                out->b = hexChar(str[3]);
                out->b += out->b * 16;
                break;
            case 9: // #rrggbbaa
                out->a = hexChar(str[7]) * 16 + hexChar(str[8]);
            case 7: // #rrggbb
                out->r = hexChar(str[1]) * 16 + hexChar(str[2]);
                out->g = hexChar(str[3]) * 16 + hexChar(str[4]);
                out->b = hexChar(str[5]) * 16 + hexChar(str[6]);
                break;
            default:
                goto invalid_string;
        }
    } else {
        char *temp;
        temp = strtok(str, " ");
        if (!temp) goto invalid_string;
        bool hsl;
        if (strcmp(temp, "rgb") == 0) {
            hsl = false;
        } else if (strcmp(temp, "hsl") == 0) {
            hsl = true;
        } else goto invalid_string;
        float r = 0, g = 0, b = 0, a = 1;
        temp = strtok(NULL, " ");
        themeParseNum(theme, temp, &r, depth + 1);
        temp = strtok(NULL, " ");
        if (!temp) goto end;
        themeParseNum(theme, temp, &g, depth + 1);
        temp = strtok(NULL, " ");
        if (!temp) goto end;
        themeParseNum(theme, temp, &b, depth + 1);
        temp = strtok(NULL, " ");
        if (!temp) goto end;
        printf("'%s'\n", temp);
        themeParseNum(theme, temp, &a, depth + 1);
        end:
            if (hsl) hsl_to_rgb(r, g, b, &r, &g, &b);
            #define COLOR(r) { \
                r *= 255; \
                if (r > 255)  r = 255; \
                else if (r < 0) r = 0; \
                out->r = (uint8_t)r; \
            }
            COLOR(r); COLOR(g); COLOR(b); COLOR(a);
            #undef COLOR
    }
    return THEME_ERROR_OKAY;
}

int themeParseFont(Theme *theme, char *str, ThemeFont *out, uint8_t depth) {
    if (depth > MAXDEPTH) return THEME_ERROR_TOO_DEEP;
    out->family = NULL;
    out->style = NULL;
    out->weight = 100;
    out->size = 12;
    if (!str) goto end;
    char *temp, *temptemp;
    temp = strtok(str, " ");
    if (!temp) goto end;
    temptemp = temp;
    temp = strtok(NULL, " ");
    if (!temp && temptemp[0] == '@')
        return themeGetDepth(theme, temptemp + 1, THEME_TYPE_FONT, out, depth + 1);
    themeParseStr(theme, temptemp, &out->family, depth + 1);
    if (!temp) goto end;
    themeParseNum(theme, temp, &out->size, depth + 1);
    temp = strtok(NULL, " ");
    if (!temp) goto end;
    themeParseNum(theme, temp, &out->weight, depth + 1);
    temp = strtok(NULL, "");
    if (!temp) goto end;
    themeParseStr(theme, temp, &out->style, depth + 1);
    end:
        if (!out->style) out->style = "";
        if (!out->family) out->family = "";
    return THEME_ERROR_OKAY;
}

ThemeColor THEME_SCHEME_DEFAULT_MAIN =  { 255, 255, 255, 255 };
#define THEME_SCHEME_DEFAULT_DIM_MOD -10
#define THEME_SCHEME_DEFAULT_BRIGHT_MOD 10
int themeParseScheme(Theme *theme, char *str, ThemeScheme *out, uint8_t depth) {
    if (depth > MAXDEPTH) return THEME_ERROR_TOO_DEEP;
    char *temp = NULL;
    if (str) temp = strtok(str, " ");
    out->font = malloc(sizeof(ThemeFont));
    out->font->alloced = true;
    int ret = themeParseFont(theme, temp, out->font, depth + 1);
    if (ret != THEME_ERROR_OKAY) return ret;
    #define COLOR(name) if (temp) { \
        temp = strtok(NULL, " "); \
        out->name = malloc(sizeof(ThemeColor)); \
        out->name->alloced = true; \
        ret = themeParseColor(theme, temp, out->name, depth + 1); \
        if (ret != THEME_ERROR_OKAY) return ret; \
    } else { out->name = NULL; }
    #define CLONEWITHDIF(from, to, dif) { \
        out->to = malloc(sizeof(ThemeColor)); \
        out->to->alloced = true; \
        out->to->r = clampedAdd(out->from->r, dif); \
        out->to->g = clampedAdd(out->from->g, dif); \
        out->to->b = clampedAdd(out->from->b, dif); \
        out->to->a = out->from->a; \
    }
    #define CLONEWITHINVERSE(from, to) { \
        out->to = malloc(sizeof(ThemeColor)); \
        out->to->alloced = true; \
        out->to->r = 255 - out->from->r; \
        out->to->g = 255 - out->from->g; \
        out->to->b = 255 - out->from->b; \
        out->to->a = out->from->a; \
    }
    COLOR(main);
    if (!out->main) out->main = &THEME_SCHEME_DEFAULT_MAIN;
    COLOR(dim);
    if (!out->dim) CLONEWITHDIF(main, dim, THEME_SCHEME_DEFAULT_DIM_MOD);
    COLOR(dimmer);
    if (!out->dimmer) CLONEWITHDIF(dim, dimmer, THEME_SCHEME_DEFAULT_DIM_MOD);
    COLOR(bright);
    if (!out->bright) CLONEWITHDIF(main, bright, THEME_SCHEME_DEFAULT_BRIGHT_MOD);
    COLOR(brighter);
    if (!out->brighter) CLONEWITHDIF(bright, brighter, THEME_SCHEME_DEFAULT_BRIGHT_MOD);
    COLOR(bg);
    if (!out->bg) CLONEWITHINVERSE(main, bg);
    COLOR(bgdim);
    if (!out->bgdim) CLONEWITHDIF(bg, bgdim, THEME_SCHEME_DEFAULT_DIM_MOD);
    COLOR(bgbright);
    if (!out->bgbright) CLONEWITHDIF(bg, bgbright, THEME_SCHEME_DEFAULT_BRIGHT_MOD);
    COLOR(ol);
    if (!out->ol) {
        CLONEWITHDIF(main, ol, THEME_SCHEME_DEFAULT_BRIGHT_MOD);
        out->ol->r = 255 - out->ol->r;
        out->ol->g = 255 - out->ol->g;
        out->ol->b = 255 - out->ol->b;
    }
    COLOR(oldim);
    if (!out->oldim) CLONEWITHDIF(ol, oldim, THEME_SCHEME_DEFAULT_DIM_MOD);
    COLOR(olbright);
    if (!out->olbright) CLONEWITHDIF(ol, olbright, THEME_SCHEME_DEFAULT_BRIGHT_MOD);
    return THEME_ERROR_OKAY;
    #undef COLOR
    #undef CLONEWITHDIF
    #undef CLONEWITHINVERSE
}

//

ThemeNode *themeParseFile(char *content) {
    char cprev = 0;
    char ccur = *content;
    char *c = content;
    ThemeNode *head = NULL;
    struct Pos {
        uint16_t c;
        uint16_t l;
    } pos = { 0, 1 }, notDoneStart = { 0, 1 };
    bool notDone = false;
    void *notDoneFree = NULL;
    #define ERROR(format, ...) fprintf(stderr, "Error at %u:%u: " format "\n", pos.l, pos.c __VA_OPT__(,) __VA_ARGS__)
    #define WARN(format, ...) fprintf(stderr, "Warn at %u:%u: " format "\n", pos.l, pos.c __VA_OPT__(,) __VA_ARGS__)
    #define NEXTCHAR() { \
        cprev = ccur; \
        ++c; \
        ccur = *c; \
        if (!ccur) { \
            if (notDone) { \
                WARN("Unexpected end of input, starting at %u:%u", notDoneStart.l, notDoneStart.c); \
                if (notDoneFree) { free(notDoneFree); notDoneFree = NULL; } \
            } \
            return head; \
        } \
        if (cprev == '\n' ) { ++pos.l; pos.c = 0; } \
        else ++pos.c; \
    }
    #define NEXTLINE() { \
        while (ccur != '\n') NEXTCHAR(); \
    }
    #define NEXTNONSPACE() if (isSpace(ccur)) { \
        do { NEXTCHAR(); } while (isSpace(ccur)); \
    }
    while (1) {
        switch (ccur) {
            case '\n':
                NEXTCHAR();
                break;
            case '#':
                NEXTLINE();
                break;
            case '/':
                NEXTCHAR();
                if (ccur == '*') {
                    char prev;
                    notDone = true;
                    notDoneStart = pos;
                    NEXTCHAR();
                    do {
                        NEXTCHAR();
                    } while (cprev != '*' || ccur != '/');
                    notDone = false;
                    NEXTCHAR();
                } else {
                    if (ccur != '/') {
                        WARN("Missing / in comment");
                    }
                    NEXTLINE();
                }
                break;
            default:
                NEXTNONSPACE();
                notDone = true;
                notDoneStart = pos;
                char *type = c;
                do {
                    NEXTCHAR();
                } while (!isSpace(ccur));
                *c = '\0';
                NEXTCHAR();
                ThemeNode *n = themeBSTCreate();
                notDoneFree = n;
                if (strcmp("color", type) == 0) n->type = THEME_TYPE_COLOR;
                else if (strcmp("num", type) == 0) n->type = THEME_TYPE_NUM;
                else if (strcmp("str", type) == 0) n->type = THEME_TYPE_STR;
                else if (strcmp("font", type) == 0) n->type = THEME_TYPE_FONT;

                else if (strcmp("scheme", type) == 0) n->type = THEME_TYPE_SCHEME;
                else {
                    WARN("Invalid type %s", type);
                    free(n);
                    n = NULL;
                }
                if (n) {
                    NEXTNONSPACE();
                    n->name = c;
                    do {
                        if (!isAlpha(ccur))
                            WARN("Invalid char %u in identifier", ccur);
                        NEXTCHAR();
                    } while (!isSpace(ccur));
                    *c = '\0';
                    if (n != NULL) {
                        NEXTNONSPACE();
                        n->data.parse = c;
                        NEXTLINE();
                        *c = '\0';
                    }
                }
                notDoneFree = NULL;
                notDone = false;
                head = themeBSTAdd(head, n);
                NEXTLINE();
        }
    }
    return head;
    #undef WARN
    #undef ERROR
    #undef NEXTCHAR
    #undef NEXTLINE
    #undef NEXTNONSPACE
}

const size_t THEME_DATA_SIZE[6] = {
    0,
    sizeof(char*),
    sizeof(float),
    sizeof(ThemeColor),
    sizeof(ThemeFont),
    sizeof(ThemeScheme)
};

int ThemeParseNode(Theme *theme, ThemeNode* node, void *out, uint8_t depth) {
    if (depth > MAXDEPTH) return THEME_ERROR_TOO_DEEP;
    if (!node) return THEME_ERROR_NOT_FOUND;
    uint8_t type = node->type;
    if (node->parsed == false) {
        char *parse = node->data.parse;
        if (type > THEME_TYPE_STR) {
            node->data.ptr = malloc(THEME_DATA_SIZE[type]);
            ((uint8_t*)(node->data.ptr))[0] = true;
        }
        switch (type) {
            case THEME_TYPE_NUM:
                themeParseNum(theme, parse, &node->data.num, depth + 1);
                break;
            case THEME_TYPE_STR:
                themeParseStr(theme, parse, &node->data.str, depth + 1);
                break;
            case THEME_TYPE_COLOR:
                themeParseColor(theme, parse, node->data.color, depth + 1);
                break;
            case THEME_TYPE_FONT:
                themeParseFont(theme, parse, node->data.font, depth + 1);
                break;
            case THEME_TYPE_SCHEME:
                themeParseScheme(theme, parse, node->data.scheme, depth + 1);
                break;
        }
        node->parsed = true;
    }
    switch (type) {
        case THEME_TYPE_NUM:
            *((float*)out) = node->data.num;
            break;
        case THEME_TYPE_STR:
            *((char**)out) = node->data.str;
            break;
        default:
            memcpy(out, node->data.ptr, THEME_DATA_SIZE[type]);
    }
    return THEME_ERROR_OKAY;
}
int themeGetDepth(Theme *theme, const char *name, uint8_t type, void *out, uint8_t depth) {
    if (type > THEME_TYPE_SCHEME) return THEME_ERROR_WRONG_TYPE;
    if (depth > MAXDEPTH) return THEME_ERROR_TOO_DEEP;
    ThemeNode *node = themeBSTFind(theme->head, name);
    if (!node) return THEME_ERROR_NOT_FOUND;
    if (node->type != type) return THEME_ERROR_INVALID_TYPE;
    return ThemeParseNode(theme, node, out, depth + 1);
}
int themeGetTypeDepth(Theme *theme, const char *name, uint8_t *type, void *out, uint8_t depth) {
    if (depth > MAXDEPTH) return THEME_ERROR_TOO_DEEP;
    ThemeNode *node = themeBSTFind(theme->head, name);
    if (!node) return THEME_ERROR_NOT_FOUND;
    *type = node->type;
    return ThemeParseNode(theme, node, out, depth + 1);
}
int themeGet(Theme *theme, const char *name, uint8_t type, void *out) {
    return themeGetDepth(theme, name, type, out, 0);
}
int themeGetType(Theme *theme, const char *name, uint8_t *type, void *out) {
    return themeGetTypeDepth(theme, name, type, out, 0);
}

bool themeChanged(Theme *theme) {
    int fd;
    if (theme->watching) {
        fd = theme->watchingFD;
    } else {
        if (theme->watchingFDMade) {
            fd = theme->watchingFD;
            inotify_rm_watch(fd, theme->watchingWD);
        } else {
            fd = inotify_init();
            if (fd == -1) {
                perror("inotify_init");
                return false;
            }
        }
        int wd = inotify_add_watch(fd, theme->name, IN_MODIFY);
        if (wd == -1) {
            perror("inotify_add_watch");
            return false;
        }            
        theme->watchingFD = fd;
        theme->watchingWD = wd;
        theme->watchingFDS[0].fd = fd;
        theme->watchingFDS[0].events = POLLIN;
        theme->watching = true;
    }
    int events = poll(theme->watchingFDS, 1, 0);
    if (events == -1) {
        perror("poll");
        return false;
    }
    if (events == 0) return false;
    static char buf[sizeof(struct inotify_event)];
    read(fd, buf, sizeof(buf));
    return true;
}

Theme* themeCreate(char *filename) {
    Theme *theme = malloc(sizeof(Theme));
    theme->watching = false;
    theme->watchingFDMade = false;
    theme->watchingFD = -1;
    theme->watchingWD = -1;
    if (filename == NULL) {
        const char *home = getenv("HOME");
        if (home == NULL) {
            perror("getenv");
            return NULL;
        }
        filename = malloc(strlen(home) + sizeof("theme.theme") + 1);
        sprintf(filename, "%s/theme.theme", home);
        theme->name = filename;
    } else {
        theme->name = malloc(strlen(filename) + 1);
        strcpy(theme->name, filename);
    }
    theme->data = fileRead(filename);
    if (!theme->data) {
        free(theme->name);
        free(theme);
        return NULL;
    }
    theme->head = themeParseFile(theme->data);
    if (!theme->head) {
        free(theme->name);
        free(theme->data);
        free(theme);
    }
    return theme;
}
bool themeUpdate(Theme *theme, char* newname) {
    char *name = newname ?: theme->name;
    if (!name) return false;
    char *newdata = fileRead(name);
    if (!newdata) {
        perror("fileRead");
        return false;
    };
    ThemeNode *newhead = themeParseFile(newdata);
    if (!newhead) {
        free(newdata);
        perror("themeParseFile");
        return false;
    }
    if (theme->head) themeBSTDestroy(theme->head);
    if (newname) {
        if (theme->name) free(theme->name);
        theme->name = malloc(strlen(newname) + 1);
        strcpy(theme->name, newname);
        theme->watching = false;
    }
    if (theme->data) free(theme->data);
    theme->data = newdata;
    theme->head = newhead;
    return true;
}
void themeDestroy(Theme *theme) {
    if (theme->head) themeBSTDestroy(theme->head);
    theme->head = NULL;
    free(theme->name);
    if (theme->data) free(theme->data);
    free(theme);
}
