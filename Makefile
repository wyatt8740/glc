
LD = gcc
CC = gcc

CFLAGS = -g -Wall -ansi -D_XOPEN_SOURCE=500
LDFLAGS = -Wall -ansi

MLIBDIR = lib
DESTDIR = 
BUILD = build
SRC = src
LIB = $(SRC)/lib
COMMON = $(SRC)/common
STREAM = $(SRC)/stream
SCRIPTS = scripts

VERSION=0
RELEASE=$(VERSION).3.6

# minilzo is licenced under GPL
# remove following lines to disable minilzo:
MINILZO = support/minilzo/
LZO_OBJ = $(BUILD)/minilzo.o
USE_LZO = -D__MINILZO -I$(MINILZO)

# quicklz is licenced under GPL
# remove following lines to disable quicklz:
QUICKLZ = support/quicklz/
QUICKLZ_OBJ = $(BUILD)/quicklz.o
USE_QUICKLZ = -D__QUICKLZ -I$(QUICKLZ)

LIBS = -lpthread -lpacketstream -lGL -ldl -lelfhacks -lasound

HEADERS = $(COMMON)/glc.h \
	  $(COMMON)/util.h \
	  $(COMMON)/thread.h \
	  $(STREAM)/gl_capture.h \
	  $(STREAM)/gl_play.h \
	  $(STREAM)/pack.h \
	  $(STREAM)/file.h \
	  $(STREAM)/img.h \
	  $(STREAM)/scale.h \
	  $(STREAM)/info.h \
	  $(STREAM)/audio_capture.h \
	  $(STREAM)/audio_play.h \
	  $(STREAM)/wav.h \
	  $(STREAM)/demux.h \
	  $(STREAM)/ycbcr.h \
	  $(STREAM)/yuv4mpeg.h \
	  $(STREAM)/rgb.h

LIB_OBJS = $(BUILD)/gl_capture.o \
           $(BUILD)/gl_play.o \
           $(BUILD)/util.o \
           $(BUILD)/pack.o \
           $(BUILD)/file.o \
           $(BUILD)/img.o \
           $(BUILD)/scale.o \
           $(BUILD)/info.o \
           $(BUILD)/thread.o \
           $(BUILD)/audio_capture.o \
           $(BUILD)/audio_play.o \
           $(BUILD)/wav.o \
           $(BUILD)/demux.o \
           $(BUILD)/ycbcr.o \
           $(BUILD)/yuv4mpeg.o \
           $(BUILD)/rgb.o \
           $(LZO_OBJ) \
           $(QUICKLZ_OBJ)

CAPT_OBJS = $(BUILD)/main.o \
            $(BUILD)/alsa.o \
            $(BUILD)/opengl.o \
            $(BUILD)/x11.o

all: $(BUILD) $(BUILD)/libglc-capture.so.$(RELEASE) $(BUILD)/glc-play $(BUILD)/glc-capture

$(BUILD):
	mkdir $(BUILD)

# capture library
$(BUILD)/libglc-capture.so.$(RELEASE): $(BUILD)/libglc.so.$(RELEASE) $(CAPT_OBJS)
	$(LD) $(LDFLAGS) -Wl,-soname,libglc-capture-so.$(VERSION) -L$(BUILD) -lglc -shared \
		-o $(BUILD)/libglc-capture.so.$(RELEASE) $(CAPT_OBJS)
	ln -sf libglc-capture.so.$(RELEASE) $(BUILD)/libglc-capture.so.$(VERSION)
	ln -sf libglc-capture.so.$(RELEASE) $(BUILD)/libglc-capture.so

$(BUILD)/main.o: $(LIB)/main.c $(LIB)/lib.h $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/main.o -c $(LIB)/main.c

$(BUILD)/alsa.o: $(LIB)/alsa.c $(LIB)/lib.h $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/alsa.o -c $(LIB)/alsa.c

$(BUILD)/opengl.o: $(LIB)/opengl.c $(LIB)/lib.h $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/opengl.o -c $(LIB)/opengl.c

$(BUILD)/x11.o: $(LIB)/x11.c $(LIB)/lib.h $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/x11.o -c $(LIB)/x11.c

# player / tool
$(BUILD)/glc-play: $(BUILD)/play.o $(BUILD)/libglc.so.$(RELEASE)
	$(LD) $(LDFLAGS) -L$(BUILD) -lglc -o $(BUILD)/glc-play $(BUILD)/play.o

$(BUILD)/play.o: $(SRC)/play.c $(HEADERS)
	$(CC) $(CFLAGS) -o $(BUILD)/play.o -c $(SRC)/play.c


# capture app
$(BUILD)/glc-capture: $(BUILD)/capture.o
	$(LD) $(LDFLAGS) -o $(BUILD)/glc-capture $(BUILD)/capture.o

$(BUILD)/capture.o: $(SRC)/capture.c
	$(CC) $(CFLAGS) -o $(BUILD)/capture.o -c $(SRC)/capture.c


# libglc
$(BUILD)/libglc.so.$(RELEASE): $(LIB_OBJS)
	$(LD) $(LDFLAGS) -Wl,-soname,libglc.so.$(VERSION) $(LIBS) -shared \
		$(LIB_OBJS) -o $(BUILD)/libglc.so.$(RELEASE)
	ln -sf libglc.so.$(RELEASE) $(BUILD)/libglc.so.$(VERSION)
	ln -sf libglc.so.$(RELEASE) $(BUILD)/libglc.so


# common objects
$(BUILD)/util.o: $(COMMON)/util.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/util.o -c $(COMMON)/util.c

$(BUILD)/thread.o: $(COMMON)/thread.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/thread.o -c $(COMMON)/thread.c


# stream processor objects
$(BUILD)/gl_capture.o: $(STREAM)/gl_capture.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/gl_capture.o -c $(STREAM)/gl_capture.c

$(BUILD)/gl_play.o: $(STREAM)/gl_play.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/gl_play.o -c $(STREAM)/gl_play.c

$(BUILD)/pack.o: $(STREAM)/pack.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/pack.o -c $(STREAM)/pack.c $(USE_LZO)

$(BUILD)/file.o: $(STREAM)/file.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/file.o -c $(STREAM)/file.c

$(BUILD)/img.o: $(STREAM)/img.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/img.o -c $(STREAM)/img.c

$(BUILD)/scale.o: $(STREAM)/scale.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/scale.o -c $(STREAM)/scale.c

$(BUILD)/info.o: $(STREAM)/info.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/info.o -c $(STREAM)/info.c

$(BUILD)/audio_capture.o: $(STREAM)/audio_capture.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/audio_capture.o -c $(STREAM)/audio_capture.c

$(BUILD)/audio_play.o: $(STREAM)/audio_play.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/audio_play.o -c $(STREAM)/audio_play.c

$(BUILD)/wav.o: $(STREAM)/wav.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/wav.o -c $(STREAM)/wav.c

$(BUILD)/demux.o: $(STREAM)/demux.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/demux.o -c $(STREAM)/demux.c

$(BUILD)/ycbcr.o: $(STREAM)/ycbcr.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/ycbcr.o -c $(STREAM)/ycbcr.c

$(BUILD)/yuv4mpeg.o: $(STREAM)/yuv4mpeg.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/yuv4mpeg.o -c $(STREAM)/yuv4mpeg.c

$(BUILD)/rgb.o: $(STREAM)/rgb.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -o $(BUILD)/rgb.o -c $(STREAM)/rgb.c


$(LZO_OBJ): $(MINILZO)minilzo.c $(MINILZO)lzoconf.h $(MINILZO)lzodefs.h $(MINILZO)minilzo.h
	$(CC) $(CFLAGS) -fPIC -o $(LZO_OBJ) -c $(MINILZO)minilzo.c

$(QUICKLZ_OBJ): $(QUICKLZ)quicklz.c $(QUICKLZ)quicklz.h
	$(CC) $(CFLAGS) -fPIC -o $(QUICKLZ_OBJ) -c $(QUICKLZ)quicklz.c


install-scripts: $(SCRIPTS)/glc-encode $(SCRIPTS)/glc-capture
	install -Dm 0644 $(SCRIPTS)/encode.sh $(DESTDIR)/usr/share/glc/encode.sh
	install -Dm 0644 $(SCRIPTS)/capture.sh $(DESTDIR)/usr/share/glc/capture.sh
	install -Dm 0644 $(SCRIPTS)/play.sh $(DESTDIR)/usr/share/glc/play.sh

install-libs: $(BUILD)/libglc.so $(BUILD)/libglc-capture.so 
	install -Dm 0755 $(BUILD)/libglc.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc.so.$(RELEASE)
	ln -sf libglc.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc.so.$(VERSION)
	ln -sf libglc.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc.so

	install -Dm 0755 $(BUILD)/libglc-capture.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-capture.so.$(RELEASE)
	ln -sf libglc-capture.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-capture.so.$(VERSION)
	ln -sf libglc-capture.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-capture.so

install: install-libs $(BUILD)/glc-play
	install -Dm 0755 $(BUILD)/glc-play $(DESTDIR)/usr/bin/glc-play
	install -Dm 0755 $(BUILD)/glc-capture $(DESTDIR)/usr/bin/glc-capture

clean:
	rm -f $(LIB_OBJS) \
	      $(CAPT_OBJS) \
	      $(BUILD)/libglc.so.$(RELEASE) \
	      $(BUILD)/libglc.so.$(VERSION) \
	      $(BUILD)/libglc.so \
	      $(BUILD)/libglc-capture.so.$(RELEASE) \
	      $(BUILD)/libglc-capture.so.$(VERSION) \
	      $(BUILD)/libglc-capture.so \
	      $(BUILD)/play.o \
	      $(BUILD)/glc-play \
	      $(BUILD)/capture.o \
	      $(BUILD)/glc-capture
