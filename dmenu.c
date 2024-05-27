/* See LICENSE file for copyright and license details. */

#include <ctype.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define __USE_MISC
#include <dirent.h>
#undef __USE_MISC
#include <sys/types.h>
#include <pwd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include <X11/extensions/Xrender.h>
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

/* macros */
#define INTERSECT(x, y, w, h, r) \
	(MAX(0, MIN((x) + (w), (r).x_org + (r).width) - MAX((x), (r).x_org)) \
	* MAX(0, MIN((y) + (h), (r).y_org + (r).height) - MAX((y), (r).y_org)))
#define TEXTW(X)              (drw_fontset_getwidth(drw, (X)) + lrpad)
#define NUMBERSMAXDIGITS      100
#define NUMBERSBUFSIZE        (NUMBERSMAXDIGITS * 2) + 1

#define OPAQUE                0xffu

/* enums */
enum { SchemeNorm, SchemeSel, SchemeNormHighlight, SchemeSelHighlight,
       SchemeLast }; /* color schemes */

struct item {
	char *text;
	unsigned int len;
	struct item *left, *right;
	double distance;
	unsigned char hp : 1;
	unsigned char file : 1;
	unsigned char folder : 1;
};

static char numbers[NUMBERSBUFSIZE] = "";
static char text[BUFSIZ] = "";
static char *embed;
static int bh, mw, mh;
static int inputw = 0, promptw;
static int lrpad; /* sum of left and right padding */
static int ctrlpressed = 0;
static int filesiz = 0;
static size_t cursor;
static struct item *items = NULL;
static struct item *files = NULL;
static struct item *matches, *matchend;
static struct item *prev, *curr, *next, *sel;
static int mon = -1, screen;

static Atom clip, utf8;
static Display *dpy;
static Window root, parentwin, win;
static XIC xic;

static Drw *drw;
static Clr *scheme[SchemeLast];

static int useargb = 0;
static Visual *visual;
static int depth;
static Colormap cmap;

#include "config.h"

static int (*fstrncmp)(const char *, const char *, size_t);
static char *(*fstrstr)(const char *, const char *);

static void xinitvisual();

static unsigned int textw_clamp(const char *str, unsigned int n) {
	unsigned int w = drw_fontset_getwidth_clamp(drw, str, n) + lrpad;
	return MIN(w, n);
}

static void freefilenames() {
	if (!files) return;
	for (struct item *it = files; it && it->text; ++it) {
		free(it->text);
		it->text = NULL;
		it->len = 0;
	}
}

static void readfolder(const char *path) {
	struct dirent *ent = NULL;
	struct item *it = NULL;
	DIR *dir = opendir(path);
	int i = 0;
	if (!dir) return;
	if (files) {
		freefilenames();
	} else {
		filesiz = 16;
		if (!(files = malloc(filesiz * sizeof(*files))))
			die("cannot realloc %zu bytes:", filesiz * sizeof(*files));
	}
	while ((ent = readdir(dir))) {
		if (ent->d_name[0] == '\0') continue;
		if (ent->d_name[0] == '.') {
			if (ent->d_name[1] == '.') {
				if (ent->d_name[2] == '\0') continue;
			} else if (ent->d_name[1] == '\0') continue;
		}
		// TODO recursivly search sub folders
		if (i + 1 >= filesiz) {
			filesiz += 16;
			if (!(files = realloc(files, filesiz * sizeof(*files))))
				die("cannot realloc %zu bytes:", filesiz * sizeof(*files));
		}
		it = files + i;
		it->len = strlen(ent->d_name);
		if (!(it->text = malloc(it->len + 1))) die("malloc");
		memcpy(it->text, ent->d_name, it->len + 1);
		it->left = NULL;
		it->right = NULL;
		it->distance = 0;
		it->hp = 0;
		it->file = 1;
		it->folder = ent->d_type == DT_DIR;
		i += 1;
	}
	it = files + i;
	it->text = NULL;

	closedir(dir);
}

static void appenditem(struct item *item, struct item **list, struct item **last) {
	if (*last)
		(*last)->right = item;
	else
		*list = item;
	item->left = *last;
	item->right = NULL;
	*last = item;
}

static void calcoffsets(void) {
	int i, n;
	if (lines > 0)
		n = lines * columns * bh;
	else
		n = mw - (promptw + inputw + TEXTW("<") + TEXTW(">") + TEXTW(numbers));
	/* calculate which items will begin the next page and previous page */
	for (i = 0, next = curr; next; next = next->right)
		if ((i += (lines > 0) ? bh : textw_clamp(next->text, n)) > n)
			break;
	for (i = 0, prev = curr; prev && prev->left; prev = prev->left)
		if ((i += (lines > 0) ? bh : textw_clamp(prev->left->text, n)) > n)
			break;
}

static int max_textw(void) {
	int len = 0;
	for (struct item *item = items; item && item->text; item++)
		len = MAX(TEXTW(item->text), len);
	return len;
}

static void cleanup(void) {
	size_t i;
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (i = 0; i < SchemeLast; i++)
		free(scheme[i]);
	for (i = 0; items && items[i].text; ++i)
		free(items[i].text);
	free(items);
	if (files) {
		for (struct item *it = files; it && it->text; ++it)
			free(it->text);
		free(files);
	}
	drw_free(drw);
	XSync(dpy, False);
	XCloseDisplay(dpy);
}

static char *cistrstr(const char *h, const char *n) {
	size_t i;
	if (!n[0]) return (char *)h;
	for (/* empty */; *h; ++h) {
		for (i = 0; n[i] && tolower((unsigned char)n[i]) == tolower((unsigned char)h[i]); ++i) {}
		if (n[i] == '\0')
			return (char *)h;
	}
	return NULL;
}

static int drawitem(struct item *item, char *search, int x, int y, int w) {
	char *itemtext = item->text;
	char *inputtext = search;
	char temp[2] = " ";
	int ellipsis_w = TEXTW("...") - lrpad;
	int tw = TEXTW(itemtext) - lrpad;
	drw_setscheme(drw, scheme[item == sel ? SchemeSel : SchemeNorm]);
	drw_rect(drw, x, y, w, bh, 1, 1);
	x += lrpad / 2;
	w -= lrpad;
	if (tw < w - ellipsis_w) {
		tw = w / 2 - tw / 2;
		w -= tw * 2;
		x += tw;
	} else {
		w -= ellipsis_w;
	}
	for (/* empty */; *itemtext != '\0'; ++itemtext) {
		temp[0] = itemtext[0];
		tw = TEXTW(temp) - lrpad;
		if (tw > w) {
			drw_setscheme(drw, scheme[item == sel ? SchemeSel : SchemeNorm]);
			x = drw_text(drw, x, y, ellipsis_w, bh, 0, "...", 0);
			break;
		} else {
			if (*inputtext != '\0' && fstrncmp(itemtext, inputtext, 1) == 0) {
				drw_setscheme(drw, scheme[item == sel ? SchemeSelHighlight : SchemeNormHighlight]);
				inputtext += 1;
			} else {
				drw_setscheme(drw, scheme[item == sel ? SchemeSel : SchemeNorm]);
			}
			x = drw_text(drw, x, y, tw, bh, 0, temp, 0);
			w -= tw;
		}
	}
	return x;
}

static void recalculatenumbers() {
	unsigned int numer = 0, denom = 0;
	struct item *item;
	if (matchend) {
		numer++;
		for (item = matchend; item && item->left; item = item->left)
			numer++;
	}
	for (item = items; item && item->text; item++)
		denom++;
	snprintf(numbers, NUMBERSBUFSIZE, "%d/%d", numer, denom);
}

static void drawmenu(void) {
	unsigned int curpos;
	struct item *item;
	int x = 0, y = 0, w, tw;

	drw_setscheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, 0, 0, mw, mh, 1, 1);

	if (prompt && *prompt) {
		drw_setscheme(drw, scheme[SchemeSel]);
		x = drw_text(drw, x, 0, promptw, bh, lrpad / 2, prompt, 0);
	}
	w = lines > 0 ? mw - x : inputw;
	/* draw numbers */
	recalculatenumbers();
	drw_setscheme(drw, scheme[SchemeNorm]);
	tw = TEXTW(numbers);
	drw_text(drw, mw - tw, 0, tw, bh, lrpad / 2, numbers, 0);
	w -= tw;
	/* draw input field */
	tw = TEXTW(text);
	curpos = tw - TEXTW(&text[cursor]);
	drw_setscheme(drw, scheme[ctrlpressed ? SchemeSel : SchemeNorm]);
	if (tw > w) {
		drw_text(drw, x, 0, w, bh, lrpad / 2 - (tw - w), text, 0);
		curpos -= tw - w;
	} else {
		drw_text(drw, x, 0, w, bh, lrpad / 2, text, 0);
	}
	if ((curpos += lrpad / 2 - 1) < w) {
		drw_setscheme(drw, scheme[SchemeNorm]);
		drw_rect(drw, curpos, 2, 2, bh - 4, 1, 0);
	}
	// TODO put all of this into configs
	char *search = text;
	if (1) {
		char *lastword = search;
		for (/* empty */; *search; ++search) {
			if (*search == ' ') lastword = search + 1;
			else if (*search == '/') lastword = search + 1;
		}
		search = lastword;
	}

	if (lines > 0) {
		w += x;
		x = 0;
		/* draw grid */
		int tempcolumns = columns;
		if (autocolumns) {
			int total = 0;
			for (item = curr; item != next; item = item->right, total++) {}
			while (tempcolumns * lines > total && tempcolumns > 1) tempcolumns -= 1;
		}
		int i = 0;
		for (item = curr; item != next; item = item->right, i++)
			drawitem(
				item, search,
				((i / lines) *  ((mw - x) / tempcolumns)),
				y + (((i % lines) + 1) * bh),
				(mw - x) / tempcolumns
			);
	} else if (matches) {
		/* draw horizontal list */
		x += inputw;
		w = TEXTW("<");
		if (curr->left) {
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_text(drw, x, 0, w, bh, lrpad / 2, "<", 0);
		}
		x += w;
		for (item = curr; item != next; item = item->right)
			x = drawitem(item, search, x, 0, textw_clamp(item->text, mw - x - TEXTW(">") - TEXTW(numbers)));
		if (next) {
			w = TEXTW(">");
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_text(drw, mw - w - TEXTW(numbers), 0, w, bh, lrpad / 2, ">", 0);
		}
	}
	drw_map(drw, win, 0, 0, mw, mh);
}

static void grabfocus(void) {
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000  };
	Window focuswin;
	int i, revertwin;

	for (i = 0; i < 100; ++i) {
		XGetInputFocus(dpy, &focuswin, &revertwin);
		if (focuswin == win)
			return;
		XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
		nanosleep(&ts, NULL);
	}
	die("cannot grab focus");
}

static void grabkeyboard(void) {
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000  };
	int i;

	if (embed)
		return;
	/* try to grab keyboard, we may have to wait for another process to ungrab */
	for (i = 0; i < 100; i++) {
		if (XGrabKeyboard(dpy, DefaultRootWindow(dpy), True, GrabModeAsync,
		                  GrabModeAsync, CurrentTime) == GrabSuccess)
			return;
		nanosleep(&ts, NULL);
	}
	die("cannot grab keyboard");
}

int compare_distance(const void *a, const void *b) {
	struct item *da = *(struct item **) a;
	struct item *db = *(struct item **) b;
	if (!db) return 1;
	if (!da) return -1;
	return da->distance == db->distance ? 0 : da->distance < db->distance ? -1 : 1;
}

static inline void fuzzymatchdoitem(char *search, int search_len, struct item *it, int *number_of_matches, int matching_path) {
	it->distance = 0;
	if (search_len) {
		if (search_len > it->len) return;
		int i = 0, j = 0;
		int match = 0;
		int matchci = 0;
		int matchdis = 0;
		int matchcontinuous = 0;
		int continuous = 0;
		for (char *c = it->text; *c; ++c, ++j) {
			if (search[i] == *c) {
				match += 1; matchci += 1;
				matchdis += j;
				matchcontinuous += continuous;
			} else if (!casesensitive && tolower(search[i]) == tolower(*c)) {
				matchci += 1;
				matchdis += j;
				matchcontinuous += continuous;
			} else {
				continuous = 0;
				continue;
			}
			++continuous;
			++i;
		}
		if (search[i] != '\0') return;
		if (match == it->len) it->distance += score_exact_match;
		if (matchci == it->len) it->distance += score_close_match;
		it->distance += (float)match * score_letter_match;
		it->distance += (float)matchci * score_letterci_match;
		it->distance += (float)matchcontinuous * score_continuous;
		if (matchci > 0) it->distance -= (float)matchdis / (float)matchci / (float)it->len * score_near_start;
	}
	if (it->hp) it->distance += score_hp;
	if (it->file) it->distance += it->folder ? score_folder : score_file;
	if (it->file && matching_path) it->distance += score_path;
	appenditem(it, &matches, &matchend);
	(*number_of_matches)++;
}

void fuzzymatch(void) {
	/* bang - we have so much memory */
	struct item *it;
	struct item **fuzzymatches = NULL;
	int number_of_matches = 0, i;

	// TODO put all of this into configs
	int matching_path = 0;
	char *word = text;
	char *base = text;
	int base_len = 0;
	for (char *c = text; *c; ++c) {
		if (*c == ' ')
			word = base = c + 1;
	}
	for (char *c = base; *c; ++c) {
		if (*c == '/') {
			base = c + 1;
			matching_path = 1;
		}
	}
	base_len = strlen(base);

	matches = matchend = NULL;

	/* walk through all items */
	for (it = items; it && it->text; ++it) {
		fuzzymatchdoitem(base, base_len, it, &number_of_matches, matching_path);
	}

	/* walk through directory */
	if (1) {
		char *path = NULL;
		if (word[0] == '/') {
			path = "";
		} else {
			if ((path = getenv("HOME")) == NULL)
				if ((path = getpwuid(getuid())->pw_dir) == NULL)
					path = "";
		}
		if (matching_path) {
			static char buf[512];
			snprintf(buf, sizeof buf, "%s/%.*s", path, (int)(base - word), word);
			path = buf;
		}
		readfolder(path);
		for (it = files; it && it->text; ++it) {
			fuzzymatchdoitem(base, base_len, it, &number_of_matches, matching_path);
		}
	}

	if (number_of_matches) {
		/* initialize array with matches */
		if (!(fuzzymatches = realloc(fuzzymatches, number_of_matches * sizeof(struct item*))))
			die("cannot realloc %u bytes:", number_of_matches * sizeof(struct item*));
		for (i = 0, it = matches; it && i < number_of_matches; i++, it = it->right) {
			fuzzymatches[i] = it;
		}
		/* sort matches according to distance */
		qsort(fuzzymatches, number_of_matches, sizeof(struct item*), compare_distance);
		/* rebuild list of matches */
		matches = matchend = NULL;
		for (i = 0; i < number_of_matches; ++i) {
			it = fuzzymatches[i];
			if (!it || !it->text) continue;
			appenditem(it, &matches, &matchend);
		}
		free(fuzzymatches);
	}
	curr = sel = matches;
	calcoffsets();
}

static void match(void) {
	if (fuzzy) {
		fuzzymatch();
		return;
	}
	static char **tokv = NULL;
	static int tokn = 0;

	char buf[sizeof text], *s;
	int i, tokc = 0;
	size_t len, textsize;
	struct item *item, *lprefix, *lsubstr, *prefixend, *substrend;

	strcpy(buf, text);
	/* separate input text into tokens to be matched individually */
	for (s = strtok(buf, " "); s; tokv[tokc - 1] = s, s = strtok(NULL, " "))
		if (++tokc > tokn && !(tokv = realloc(tokv, ++tokn * sizeof *tokv)))
			die("cannot realloc %zu bytes:", tokn * sizeof *tokv);
	len = tokc ? strlen(tokv[0]) : 0;

	matches = lprefix = lsubstr = matchend = prefixend = substrend = NULL;
	textsize = strlen(text) + 1;
	for (item = items; item && item->text; item++) {
				break;
		if (i != tokc) /* not all tokens match */
			continue;
		/* exact matches go first, then prefixes, then substrings */
		if (!tokc || !fstrncmp(text, item->text, textsize))
			appenditem(item, &matches, &matchend);
		else if (!fstrncmp(tokv[0], item->text, len))
			appenditem(item, &lprefix, &prefixend);
		else
			appenditem(item, &lsubstr, &substrend);
	}
	if (lprefix) {
		if (matches) {
			matchend->right = lprefix;
			lprefix->left = matchend;
		} else
			matches = lprefix;
		matchend = prefixend;
	}
	if (lsubstr) {
		if (matches) {
			matchend->right = lsubstr;
			lsubstr->left = matchend;
		} else
			matches = lsubstr;
		matchend = substrend;
	}
	curr = sel = matches;
	calcoffsets();
}

static void insert(const char *str, ssize_t n) {
	int text_len = strnlen(text, sizeof text - 1);
	if (text_len + n > sizeof text - 1) return;
	if (n < 0) {
		memmove(&text[cursor + n], &text[cursor], text_len - cursor - n);
	} else {
		/* move existing text out of the way, insert new text, and update cursor */
		memmove(&text[cursor + n], &text[cursor], text_len - cursor + n);
		memcpy(&text[cursor], str, n);
	}
	cursor += n;
	match();
}

static size_t nextrune(int inc) {
	ssize_t n;
	/* return location of next utf8 rune in the given direction (+1 or -1) */
	for (n = cursor + inc; n + inc >= 0 && (text[n] & 0xc0) == 0x80; n += inc) {}
	return n;
}

static void movewordedge(int dir) {
	if (dir < 0) { /* move cursor to the start of the word*/
		while (cursor > 0 && strchr(worddelimiters, text[nextrune(-1)]))
			cursor = nextrune(-1);
		while (cursor > 0 && !strchr(worddelimiters, text[nextrune(-1)]))
			cursor = nextrune(-1);
	} else { /* move cursor to the end of the word */
		while (text[cursor] && strchr(worddelimiters, text[cursor]))
			cursor = nextrune(+1);
		while (text[cursor] && !strchr(worddelimiters, text[cursor]))
			cursor = nextrune(+1);
	}
}

static void docommand(int forcetext, int terminal) {
	char *cmd = NULL;
	if (forcetext) {
		cmd = text;
	} else {
		for (char *c = text; *c; ++c) {
			if (*c == ' ') {
				if (*(c + 1) == '\0')
					*c = '\0';
				else
					cmd = text;
				break;
			} else if (*c == '/') {
				cmd = text;
				break;
			}
		}
		if (!cmd) {
			if (sel && sel->text && sel->folder == 0)
				cmd = sel->text;
		}
	}
	char *format = terminal ? TERMFORMAT : CMDFORMAT;
	int percentage = 0;
	for (char *p = format; *p != '\0'; ++p) {
		if (*p == '%') {
			percentage = 1;
		} else if (percentage == 1) {
			if (*p == 's') {
				fputs(cmd, stdout);
			} else if (*p == 'e') {
				for (char *q = cmd; *q != '\0'; ++q) {
					if (*q == '\'') {
						putchar('\\');
						putchar('\'');
					} else {
						putchar(*q);
					}
				}
			} else {
				putchar('%');
				putchar(*p);
			}
			percentage = 0;
		} else {
			putchar(*p);
		}
	}
	if (percentage == 1) putchar('%');
	putchar('\n');
}

static void keypress(XKeyEvent *ev) {
	char buf[64];
	int len;
	KeySym ksym = NoSymbol;
	Status status;
	int i, offscreen = 0;
	struct item *tmpsel;

	len = XmbLookupString(xic, ev, buf, sizeof buf, &ksym, &status);
	switch (status) {
	default: /* XLookupNone, XBufferOverflow */
		return;
	case XLookupChars: /* composed string from input method */
		goto insert;
	case XLookupKeySym:
	case XLookupBoth: /* a KeySym and a string are returned: use keysym */
		break;
	}
	
	ctrlpressed = ev->state & ControlMask;

	if (ctrlpressed) {
		switch(ksym) {
		case XK_a: ksym = XK_Home;      break;
		case XK_b: ksym = XK_Left;      break;
		// case XK_c: ksym = XK_Escape;    break;
		// case XK_d: ksym = XK_Delete;    break;
		case XK_e: ksym = XK_End;       break;
		case XK_f: ksym = XK_Right;     break;
		case XK_g: ksym = XK_Escape;    break;
		case XK_h: ksym = XK_BackSpace; break;
		case XK_i: ksym = XK_Tab;       break;
		case XK_j: /* fallthrough */
		case XK_J: /* fallthrough */
		case XK_m: /* fallthrough */
		case XK_M: ksym = XK_Return; ctrlpressed = 1; break;
		case XK_n: ksym = XK_Down;      break;
		case XK_p: ksym = XK_Up;        break;

		case XK_k: /* delete right */
			text[cursor] = '\0';
			match();
			break;
		case XK_u: /* delete left */
			insert(NULL, 0 - cursor);
			break;
		/*case XK_w: / delete word /
			while (cursor > 0 && strchr(worddelimiters, text[nextrune(-1)]))
				insert(NULL, nextrune(-1) - cursor);
			while (cursor > 0 && !strchr(worddelimiters, text[nextrune(-1)]))
				insert(NULL, nextrune(-1) - cursor);
			break;*/
		case XK_y: /* paste selection */
		case XK_Y:
			XConvertSelection(dpy, (ev->state & ShiftMask) ? clip : XA_PRIMARY,
			                  utf8, utf8, win, CurrentTime);
			return;
		case XK_v: /* paste clipboard */
		case XK_V:
			XConvertSelection(dpy, (ev->state & ShiftMask) ? clip : XA_SECONDARY,
			                  utf8, utf8, win, CurrentTime);
			return;
		case XK_Left:
		case XK_KP_Left:
			movewordedge(-1);
			goto draw;
		case XK_Right:
		case XK_KP_Right:
			movewordedge(+1);
			goto draw;
		case XK_Return:
		case XK_KP_Enter:
			docommand(ev->state & ShiftMask, 1);
			cleanup();
			exit(0);
			break;
		case XK_bracketleft:
		case XK_w:
		case XK_W:
		case XK_x:
		case XK_X:
		case XK_d:
		case XK_D:
		case XK_c:
		case XK_C:
			cleanup();
			exit(1);
		default:
			return;
		}
	} else if (ev->state & Mod1Mask) {
		switch(ksym) {
		case XK_b:
			movewordedge(-1);
			goto draw;
		case XK_f:
			movewordedge(+1);
			goto draw;
		case XK_g: ksym = XK_Home;  break;
		case XK_G: ksym = XK_End;   break;
		case XK_h: ksym = XK_Up;    break;
		case XK_j: ksym = XK_Next;  break;
		case XK_k: ksym = XK_Prior; break;
		case XK_l: ksym = XK_Down;  break;
		default:
			return;
		}
	}

	switch(ksym) {
	default:
insert:
		if (!iscntrl((unsigned char)*buf))
			insert(buf, len);
		break;
	case XK_Control_L:
	case XK_Control_R:
		ctrlpressed = 1;
		goto draw;
	case XK_Delete:
	case XK_KP_Delete:
		if (text[cursor] == '\0')
			return;
		cursor = nextrune(+1);
		/* fallthrough */
	case XK_BackSpace:
		if (cursor == 0)
			return;
		insert(NULL, nextrune(-1) - cursor);
		break;
	case XK_End:
	case XK_KP_End:
		if (text[cursor] != '\0') {
			cursor = strlen(text);
			break;
		}
		if (next) {
			/* jump to end of list and position items in reverse */
			curr = matchend;
			calcoffsets();
			curr = prev;
			calcoffsets();
			while (next && (curr = curr->right))
				calcoffsets();
		}
		sel = matchend;
		break;
	case XK_Escape:
		cleanup();
		exit(1);
	case XK_Home:
	case XK_KP_Home:
		if (sel == matches) {
			cursor = 0;
			break;
		}
		sel = curr = matches;
		calcoffsets();
		break;
	case XK_Left:
		if (columns > 1 && sel) {
			tmpsel = sel;
			for (i = 0; i < lines; i++) {
				if (!tmpsel->left || tmpsel->left->right != tmpsel) {
					if (offscreen)
						break;
					return;
				}
				if (tmpsel == curr)
					offscreen = 1;
				tmpsel = tmpsel->left;
			}
			sel = tmpsel;
			if (offscreen) {
				curr = prev;
				calcoffsets();
			}
			break;
		}
	case XK_KP_Left:
		if (cursor > 0 && (!sel || !sel->left || lines > 0)) {
			cursor = nextrune(-1);
			break;
		}
		if (lines > 0)
			return;
		/* fallthrough */
	case XK_Up:
	case XK_KP_Up:
		if (sel && sel->left && (sel = sel->left)->right == curr) {
			curr = prev;
			calcoffsets();
		}
		break;
	case XK_Next:
	case XK_KP_Next:
		if (!next)
			return;
		sel = curr = next;
		calcoffsets();
		break;
	case XK_Prior:
	case XK_KP_Prior:
		if (!prev)
			return;
		sel = curr = prev;
		calcoffsets();
		break;
	case XK_Return:
	case XK_KP_Enter:
		docommand(ev->state & ShiftMask, 0);
		cleanup();
		exit(0);
		break;
	case XK_Right:
		if (columns > 1 && sel) {
			tmpsel = sel;
			for (i = 0; i < lines; i++) {
				if (!tmpsel->right ||  tmpsel->right->left != tmpsel) {
					if (offscreen)
						break;
					return;
				}
				tmpsel = tmpsel->right;
				if (tmpsel == next)
					offscreen = 1;
			}
			sel = tmpsel;
			if (offscreen) {
				curr = next;
				calcoffsets();
			}
			break;
		}
	case XK_KP_Right:
		if (text[cursor] != '\0') {
			cursor = nextrune(+1);
			break;
		}
		if (lines > 0)
			return;
		/* fallthrough */
	case XK_Down:
	case XK_KP_Down:
		if (sel && sel->right && (sel = sel->right) == next) {
			curr = next;
			calcoffsets();
		}
		break;
	case XK_Tab:
		if (!sel || matches == NULL) { addspace: (void)0;
			int text_len = strlen(text);
			if (cursor != text_len) break;
			if (text[text_len - 1] != ' ')
				insert(" ", 1);
			break;
		}
		char *lastslash = NULL;
		for (char *c = text; *c; ++c) {
			if (*c == ' ') lastslash = c;
			else if (*c == '/') lastslash = c;
		}
		lastslash = lastslash ? lastslash + 1 : text;
		if (sel && strcmp(sel->text, lastslash) == 0) goto addspace;
		if (sel->file) {
			memcpy(lastslash, sel->text, sel->len);
			cursor = strnlen(text, sizeof text - 1);
			if (sel->folder) {
				memcpy(text + cursor, "/", 1);
				cursor += 1;
			}
		} else {
			memcpy(lastslash, sel->text, sel->len);
			cursor = strnlen(text, sizeof text - 1);
		}
		text[cursor] = '\0';
		match();
		break;
	}

draw:
	drawmenu();
}

static void keyrelease(XKeyEvent *ev) {
	int temp;
	KeySym ksym = XLookupKeysym(ev, 0);
	if (ksym == XK_Control_L || ksym == XK_Control_R) {
		temp = 0;
	} else {
		temp = ev->state & ControlMask;
	}
	if (ctrlpressed != temp) {
		ctrlpressed = temp;
		drawmenu();
	}
}

static void paste() {
	char *p, *q;
	int di;
	unsigned long dl;
	Atom da;

	/* we have been given the current selection, now insert it into input */
	if (XGetWindowProperty(dpy, win, utf8, 0, (sizeof text / 4) + 1, False,
	                   utf8, &da, &di, &dl, &dl, (unsigned char **)&p)
	    == Success && p) {
		insert(p, (q = strchr(p, '\n')) ? q - p : (ssize_t)strlen(p));
		XFree(p);
	}
	drawmenu();
}

static void readstdin(void) {
	char *line = NULL;
	size_t i, itemsiz = 0, linesiz = 0;
	ssize_t len;
	
	itemsiz = 1024;
	if (!(items = malloc(itemsiz * sizeof(*items))))
		die("cannot realloc %zu bytes:", itemsiz * sizeof(*items));

	/* read each line from stdin and add it to the item list */
	for (i = 0; (len = getline(&line, &linesiz, stdin)) != -1; i++) {
		if (line[len - 1] == '\n') {
			line[len - 1] = '\0';
			len -= 1;
		}
		if (len == 0) {
			i -= 1;
			continue;
		}
		if (i + 1 >= itemsiz) {
			itemsiz += 256;
			if (!(items = realloc(items, itemsiz * sizeof(*items))))
				die("cannot realloc %zu bytes:", itemsiz * sizeof(*items));
		}
		items[i].folder = items[i].file = 0;
		items[i].hp = line[0] == hpchar;
		items[i].len = strlen(line + items[i].hp);
		if (!(items[i].text = malloc(items[i].len + 1))) die("malloc");
		memcpy(items[i].text, line + items[i].hp, items[i].len + 1);
	}
	free(line);
	if (items) {
		items[i].text = NULL;
		items[i].len = 0;
	}
	lines = MIN(lines, i);
}

static void run(void) {
	XEvent ev;

	while (!XNextEvent(dpy, &ev)) {
		if (XFilterEvent(&ev, win))
			continue;
		switch(ev.type) {
		case DestroyNotify:
			if (ev.xdestroywindow.window != win)
				break;
			cleanup();
			exit(1);
		case FocusIn:
			/* regrab focus from parent window */
			if (ev.xfocus.window != win)
				grabfocus();
			break;
		case KeyPress:
			keypress(&ev.xkey);
			break;
		case KeyRelease:
			keyrelease(&ev.xkey);
			break;
		case SelectionNotify:
			if (ev.xselection.property == utf8)
				paste();
			break;
		case Expose:
			if (ev.xexpose.count == 0)
				drw_map(drw, win, 0, 0, mw, mh);
			/* Fall through */
		case VisibilityNotify:
			XRaiseWindow(dpy, win);
			break;
		}
	}
}

static void setup(void) {
	int x, y, i = 0, j;
	unsigned int du;
	XSetWindowAttributes swa;
	XIM xim;
	Window w, dw, *dws;
	XWindowAttributes wa;
	XClassHint ch = {"dmenu", "dmenu"};
#ifdef XINERAMA
	XineramaScreenInfo *info;
	Window pw;
	int a, di, n, area = 0;
#endif
	/* init appearance */
	unsigned int alphas[2] = { OPAQUE, alpha };
	for (j = 0; j < SchemeLast; j++)
		scheme[j] = drw_scm_create(drw, colors[j], alphas, 2);

	clip = XInternAtom(dpy, "CLIPBOARD",   False);
	utf8 = XInternAtom(dpy, "UTF8_STRING", False);

	/* calculate menu geometry */
	bh = drw->fonts->h + 2;
	lines = MAX(lines, 0);
	mh = (lines + 1) * bh;
	promptw = (prompt && *prompt) ? TEXTW(prompt) - lrpad / 4 : 0;
#ifdef XINERAMA
	i = 0;
	if (parentwin == root && (info = XineramaQueryScreens(dpy, &n))) {
		XGetInputFocus(dpy, &w, &di);
		if (mon >= 0 && mon < n)
			i = mon;
		else if (w != root && w != PointerRoot && w != None) {
			/* find top-level window containing current input focus */
			do {
				if (XQueryTree(dpy, (pw = w), &dw, &w, &dws, &du) && dws)
					XFree(dws);
			} while (w != root && w != pw);
			/* find xinerama screen with which the window intersects most */
			if (XGetWindowAttributes(dpy, pw, &wa))
				for (j = 0; j < n; j++)
					if ((a = INTERSECT(wa.x, wa.y, wa.width, wa.height, info[j])) > area) {
						area = a;
						i = j;
					}
		}
		/* no focused window is on screen, so use pointer location instead */
		if (mon < 0 && !area && XQueryPointer(dpy, root, &dw, &dw, &x, &y, &di, &di, &du))
			for (i = 0; i < n; i++)
				if (INTERSECT(x, y, 1, 1, info[i]) != 0)
					break;

		if (centered) {
			mw = MIN(MAX(max_textw() + promptw, min_width), info[i].width);
			x = info[i].x_org + ((info[i].width  - mw) / 2);
			y = info[i].y_org + ((info[i].height - mh) / 2);
		} else {
			x = info[i].x_org;
			y = info[i].y_org + (topbar ? 0 : info[i].height - mh);
			mw = info[i].width;
		}

		XFree(info);
	} else
#endif
	{
		if (!XGetWindowAttributes(dpy, parentwin, &wa))
			die("could not get embedding window attributes: 0x%lx",
			    parentwin);

		if (centered) {
			mw = MIN(MAX(max_textw() + promptw, min_width), wa.width);
			x = (wa.width  - mw) / 2;
			y = (wa.height - mh) / 2;
		} else {
			x = 0;
			y = topbar ? 0 : wa.height - mh;
			mw = wa.width;
		}
	}
	promptw = (prompt && *prompt) ? TEXTW(prompt) - lrpad / 4 : 0;
	inputw = mw / 3; /* input width: ~33% of monitor width */
	match();

	/* create menu window */
	swa.override_redirect = True;
	swa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
	swa.border_pixel = 0;
	swa.colormap = cmap;
	swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | VisibilityChangeMask;
	win = XCreateWindow(dpy, root, x, y, mw, mh, 0,
	                    depth, CopyFromParent, visual,
	                    CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWColormap | CWEventMask, &swa);
	XSetClassHint(dpy, win, &ch);

	/* input methods */
	if ((xim = XOpenIM(dpy, NULL, NULL, NULL)) == NULL)
		die("XOpenIM failed: could not open input device");

	xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
	                XNClientWindow, win, XNFocusWindow, win, NULL);

	XMapRaised(dpy, win);
	if (embed) {
		XReparentWindow(dpy, win, parentwin, x, y);
		XSelectInput(dpy, parentwin, FocusChangeMask | SubstructureNotifyMask);
		if (XQueryTree(dpy, parentwin, &dw, &w, &dws, &du) && dws) {
			for (i = 0; i < du && dws[i] != win; ++i)
				XSelectInput(dpy, dws[i], FocusChangeMask);
			XFree(dws);
		}
		grabfocus();
	}
	drw_resize(drw, mw, mh);
	drawmenu();
}

static void usage(void) {
	die("usage: dmenu [-bfiv] [-p prompt] [-fn font] [-m monitor]\n"
	    "             [-l lines] [-g colums] [-w windowid] [-a alpha 0-255]\n"
	    "             [-nb color] [-nf color] [-sb color] [-sf color]\n"
	    "             [-nhb color] [-nhf color] [-shb color] [-shf color]");
}

int main(int argc, char *argv[]) {
	XWindowAttributes wa;
	int i, fast = 0;

	for (i = 1; i < argc; i++)
		/* these options take no arguments */
		if (!strcmp(argv[i], "-v")) {      /* prints version information */
			puts("dmenu-"VERSION);
			exit(0);
		} else if (!strcmp(argv[i], "-b")) /* appears at the bottom of the screen */
			topbar = 0;
		else if (!strcmp(argv[i], "-f"))   /* grabs keyboard before reading stdin */
			fast = 1;
		else if (!strcmp(argv[i], "-F"))   /* fuzzy search */
			fuzzy = 0;
		else if (!strcmp(argv[i], "-c"))   /* centers dmenu on screen */
			centered = 1;
		else if (!strcmp(argv[i], "-i"))   /* case-insensitive item matching */
			casesensitive = 1;
		else if (!strcmp(argv[i], "-I"))   /* case-sensitive item matching */
			casesensitive = 0;
		else if (i + 1 == argc)
			usage();
		/* these options take one argument */
		else if (!strcmp(argv[i], "-g")) {   /* number of columns in grid */
			columns = atoi(argv[++i]);
			if (lines == 0) lines = 1;
		} else if (!strcmp(argv[i], "-l")) { /* number of lines in grid */
			lines = atoi(argv[++i]);
			if (columns == 0) columns = 1;
		} else if (!strcmp(argv[i], "-m"))
			mon = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-p"))   /* adds prompt to left of input field */
			prompt = argv[++i];
		else if (!strcmp(argv[i], "-fn"))  /* font or font set */
			fonts[0] = argv[++i];
		else if (!strcmp(argv[i], "-a"))
			alpha = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-nb"))  /* normal background color */
			colors[SchemeNorm][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-nf"))  /* normal foreground color */
			colors[SchemeNorm][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-sb"))  /* selected background color */
			colors[SchemeSel][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-sf"))  /* selected foreground color */
			colors[SchemeSel][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-nhb")) /* normal hi background color */
			colors[SchemeNormHighlight][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-nhf")) /* normal hi foreground color */
			colors[SchemeNormHighlight][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-shb")) /* selected hi background color */
			colors[SchemeSelHighlight][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-shf")) /* selected hi foreground color */
			colors[SchemeSelHighlight][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-w"))   /* embedding window id */
			embed = argv[++i];
		else
			usage();

	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("cannot open display");
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	if (!embed || !(parentwin = strtol(embed, NULL, 0)))
		parentwin = root;
	if (!XGetWindowAttributes(dpy, parentwin, &wa))
		die("could not get embedding window attributes: 0x%lx",
		    parentwin);
	xinitvisual();
	drw = drw_create(dpy, screen, root, wa.width, wa.height, visual, depth, cmap);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h;

#ifdef __OpenBSD__
	if (pledge("stdio rpath", NULL) == -1)
		die("pledge");
#endif

	if (fast && !isatty(0)) {
		grabkeyboard();
		readstdin();
	} else {
		readstdin();
		grabkeyboard();
	}

	if (casesensitive) {
		fstrncmp = strncmp;
		fstrstr = strstr;
	} else {
		fstrncmp = strncasecmp;
		fstrstr = cistrstr;
	}
	
	setup();
	run();

	return 1; /* unreachable */
}

void xinitvisual() {
	XVisualInfo *infos;
	XRenderPictFormat *fmt;
	int nitems;
	int i;

	XVisualInfo tpl = {
		.screen = screen,
		.depth = 32,
		.class = TrueColor
	};
	long masks = VisualScreenMask | VisualDepthMask | VisualClassMask;

	infos = XGetVisualInfo(dpy, masks, &tpl, &nitems);
	visual = NULL;
	for(i = 0; i < nitems; i ++) {
		fmt = XRenderFindVisualFormat(dpy, infos[i].visual);
		if (fmt->type == PictTypeDirect && fmt->direct.alphaMask) {
			 visual = infos[i].visual;
			 depth = infos[i].depth;
			 cmap = XCreateColormap(dpy, root, visual, AllocNone);
			 useargb = 1;
			 break;
		}
	}

	XFree(infos);

	if (!visual) {
		visual = DefaultVisual(dpy, screen);
		depth = DefaultDepth(dpy, screen);
		cmap = DefaultColormap(dpy, screen);
	}
}
