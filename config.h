/* See LICENSE file for copyright and license details. */
/* Default settings; can be overriden by command line. */

#define TERM "kitty"

/* Formats used to run commands
 * CMDFORMAT is used for commands run normally
 * TERMFORMAT is used for commands run in terminal
 * %s is replaced with the command
 * %e is replaced with the escaped command (should be wrapped in '')
 */
#define CMDFORMAT "cd; %s"
#define TERMFORMAT "exec " TERM " --hold $SHELL -c \'cd;echo %e; %e\'"

/*
 * Scores are used in the fuzzy match to determine the sorting of elements
 * Negative is better
 */
static float score_exact_match    = -4096.0; /* Exact match */
static float score_close_match    = -2048.0; /* Exact match, including varying case */
static float score_letter_match   = -32.0;   /* Score of each exact matching letter */
static float score_letterci_match = -16.0;   /* Score of each matching letter, inclduing varying case */
static float score_near_start     = -32.0;   /* Score of each letter near the start */
static float score_continuous     = -2.0;    /* Score of each letter in a continuous match*/
static float score_hp             = -16.0;   /* Score of a high priority match*/
static float score_file           = 8.0;     /* Score of a match which is a file */
static float score_folder         = 8.0;     /* Score of a match which is a folder*/
static float score_path           = -1024.0; /* Score of a file or folder when completing path */

static const char hpchar = '*';
static int topbar = 1;                      /* -b option; if 0, dmenu appears at bottom */
static int centered = 1;                    /* -c option; centers dmenu on screen */
static int min_width = 500;                 /* minimum width when centered */
static int fuzzy = 1;                       /* -F option; if 0, dmenu doesn't use fuzzy matching, file completion is only available in fuzzy */
static int casesensitive = 0;               /* Whether to be case-sensitive or not */
static unsigned int alpha = 0xff * 0.7;     /* Amount of opacity. 0xff is opaque */
/* -fn option overrides fonts[0]; default X11 font or font set */
static const char *fonts[] = {
	"monospace:size=10"
};
static const char *prompt      = NULL;      /* -p  option; prompt to the left of input field */
static const char *colors[SchemeLast][2] = {
	/*     fg         bg       */
	[SchemeNorm] = { "#bbbbbb", "#222222" },
	[SchemeSel] = { "#eeeeee", "#005577" },
	[SchemeSelHighlight] = { "#c978ff", "#005577" },
	[SchemeNormHighlight] = { "#c978ff", "#222222" }
};

/* -l and -g options; controls number of lines and columns in grid if > 0 */
static unsigned int lines      = 10;
static unsigned int columns    = 3;
static int autocolumns         = 1; /* Whether to reduce the number of columns automatically */

/*
 * Characters not considered part of a word while deleting words
 * for example: " /?\"&[]"
 */
static const char worddelimiters[] = " ";
