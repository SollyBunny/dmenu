
#ifndef _INCLUDE_THEME_H
#define _INCLUDE_THEME_H

#include <stdint.h>
#include <stdbool.h>
#include <poll.h>

struct ThemeColor {
    uint8_t alloced;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};
typedef struct ThemeColor ThemeColor;

struct ThemeFont {
    uint8_t alloced;
    char *family;
    float size;
    float weight;
    char *style;
};
typedef struct ThemeFont ThemeFont;

struct ThemeScheme {
    uint8_t alloced;
    ThemeFont *font;
    ThemeColor *main;
    ThemeColor *dim;
    ThemeColor *dimmer;
    ThemeColor *bright;
    ThemeColor *brighter;
    ThemeColor *bg;
    ThemeColor *bgdim;
    ThemeColor *bgbright;
    ThemeColor *ol;
    ThemeColor *oldim;
    ThemeColor *olbright;
};
typedef struct ThemeScheme ThemeScheme;

struct ThemeNode {
    union {
        float num;
        char *str;
        ThemeColor *color;
        ThemeFont *font;
        ThemeScheme *scheme;
        void *ptr;
        char *parse;
    } data;
    bool parsed;
    char *name;
    uint8_t type;
    struct ThemeNode *l;
    struct ThemeNode *r;
};
typedef struct ThemeNode ThemeNode;

struct Theme {
    ThemeNode *head;
    char *data;
    char *name;
    bool watching;
    bool watchingFDMade;
    int watchingFD;
    int watchingWD;
    struct pollfd watchingFDS[1];
};
typedef struct Theme Theme;

#define THEME_TYPE_NONE 0
#define THEME_TYPE_NUM 1
#define THEME_TYPE_STR 2
#define THEME_TYPE_COLOR 3
#define THEME_TYPE_FONT 4
#define THEME_TYPE_SCHEME 5

#define THEME_ERROR_OKAY_ALLOCED 1
#define THEME_ERROR_OKAY 0
#define THEME_ERROR_INVALID_TYPE -1
#define THEME_ERROR_NOT_FOUND -2
#define THEME_ERROR_WRONG_TYPE -3
#define THEME_ERROR_TOO_DEEP -4

/**
 * Get a value by name and type from the theme
 * @param {Theme *} theme
 * @param {char *} name - The name or iden of the value.
 * @param {uint8_t} type - The type of the value.
 * @param {void *} out - A pointer to the output struct (e.g. ThemeColor **) (output).
 * @return {int} 0 if okay, otherwise an error occured.
 */
int themeGet(Theme *theme, const char *name, uint8_t type, void *out);

/**
 * Get a value and type by name from the theme
 * @param {Theme *} theme
 * @param {char *} name - The name or iden of the value.
 * @param {uint8_t *} type - The type of the value (output).
 * @param {void *} out - A pointer to the output struct (e.g. ThemeColor **) (output).
 * @return {int} 0 if okay, otherwise an error occured.
 */
int themeGetType(Theme *theme, const char *name, uint8_t *type, void *out);

/**
 * Check if the theme's file has changed
 * @param {Theme *} - theme
 * @return {bool} - Whether the file has changed or not
 * @note This does not update the theme if it has changed, use themeUpdate() to do that.
*/
bool themeChanged(Theme *theme);

/**
 * Create a theme
 * @param {char*} - The filename to load the theme from
 * @return {Theme *}
*/
Theme* themeCreate(char *filename);

/**
 * Update a theme, optionally with a new filename
 * @param {Theme *} theme
 * @param {char *} - The new filename to load the theme from (can be NULL to not change)
*/
bool themeUpdate(Theme *theme, char* newname);

/**
 * Destroy a theme
 * @param {Theme *} theme
 * @note This will free all values previously returned, make sure to copy them if you still need them (this doesn't apply to numbers and colors)
*/
void themeDestroy(Theme *theme);

#endif