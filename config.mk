# dmenu version
VERSION = 5.3

# paths
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib

# Xinerama, comment if you don't want it
XINERAMALIBS  = -lXinerama
XINERAMAFLAGS = -DXINERAMA

# freetype
FREETYPELIBS = -lfontconfig -lXft
FREETYPEINC = /usr/include/freetype2
# OpenBSD (uncomment)
#FREETYPEINC = $(X11INC)/freetype2
#MANPREFIX = ${PREFIX}/man

# includes and libs
INCS = -I$(X11INC) -I$(FREETYPEINC)
LIBS = -L$(X11LIB) -lX11 $(XINERAMALIBS) $(FREETYPELIBS) -lXrender -lm

# flags
# DEBUGFLAGS = -O0 -g -fsanitize=address -fno-omit-frame-pointer
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809 -pedantic -Wall -DVERSION=\"$(VERSION)\" $(XINERAMAFLAGS) -O4 $(INCS) $(DEBUGFLAGS) 
LDFLAGS = $(LIBS) $(DEBUGFLAGS)

# compiler and linker
CC = cc
