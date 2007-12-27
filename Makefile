
LD = gcc
CC = gcc

CFLAGS = -g -Wall -ansi
LDFLAGS = -Wall -ansi
FEATURES = -D_XOPEN_SOURCE=600 -D_FILE_OFFSET_BITS=64
VISIBILITY = -fvisibility=hidden
SO_CFLAGS = $(CFLAGS) $(VISIBILITY) -fPIC

MLIBDIR = lib
DESTDIR = 
BUILD = build
SRC = src
HOOK = $(SRC)/hook
COMMON = $(SRC)/common
CORE = $(SRC)/core
CAPTURE = $(SRC)/capture
PLAY = $(SRC)/play
EXPORT = $(SRC)/export
SCRIPTS = scripts

VERSION=0
RELEASE=$(VERSION).4.4

# minilzo is licenced under GPL
# remove following lines to disable minilzo:
MINILZO = support/minilzo/
LZO_OBJ = $(BUILD)/minilzo.o
FEATURES += -D__MINILZO -I$(MINILZO)

# quicklz is licenced under GPL
# remove following lines to disable quicklz:
QUICKLZ = support/quicklz/
QUICKLZ_OBJ = $(BUILD)/quicklz.o
FEATURES += -D__QUICKLZ -I$(QUICKLZ)

HEADERS = $(COMMON)/glc.h \
	  $(COMMON)/util.h \
	  $(COMMON)/thread.h \
	  $(CORE)/pack.h \
	  $(CORE)/file.h \
	  $(CORE)/scale.h \
	  $(CORE)/info.h \
	  $(CORE)/ycbcr.h \
	  $(CORE)/rgb.h \
	  $(CORE)/color.h \
	  $(CAPTURE)/gl_capture.h \
	  $(CAPTURE)/audio_hook.h \
	  $(CAPTURE)/audio_capture.h \
	  $(PLAY)/gl_play.h \
	  $(PLAY)/audio_play.h \
	  $(PLAY)/demux.h \
	  $(EXPORT)/img.h \
	  $(EXPORT)/wav.h \
	  $(EXPORT)/yuv4mpeg.h

CORE_OBJS = $(BUILD)/util.o \
	    $(BUILD)/thread.o \
	    $(BUILD)/scale.o \
	    $(BUILD)/info.o \
	    $(BUILD)/rgb.o \
	    $(BUILD)/color.o \
	    $(BUILD)/ycbcr.o \
	    $(BUILD)/pack.o \
	    $(BUILD)/file.o \
	    $(LZO_OBJ) \
	    $(QUICKLZ_OBJ)

CAPTURE_OBJS = $(BUILD)/gl_capture.o \
	       $(BUILD)/audio_hook.o \
	       $(BUILD)/audio_capture.o

PLAY_OBJS = $(BUILD)/demux.o \
	    $(BUILD)/gl_play.o \
	    $(BUILD)/audio_play.o

EXPORT_OBJS = $(BUILD)/img.o \
	      $(BUILD)/wav.o \
	      $(BUILD)/yuv4mpeg.o

HOOK_OBJS = $(BUILD)/main.o \
	    $(BUILD)/alsa.o \
	    $(BUILD)/opengl.o \
	    $(BUILD)/x11.o


all: $(BUILD) \
     $(BUILD)/libglc-core.so.$(RELEASE) \
     $(BUILD)/libglc-capture.so.$(RELEASE) \
     $(BUILD)/libglc-play.so.$(RELEASE) \
     $(BUILD)/libglc-export.so.$(RELEASE) \
     $(BUILD)/libglc-hook.so.$(RELEASE) \
     $(BUILD)/glc-play \
     $(BUILD)/glc-capture

$(BUILD):
	mkdir $(BUILD)


# core library
$(BUILD)/libglc-core.so.$(RELEASE): $(CORE_OBJS)
	$(LD) $(LDFLAGS) -Wl,-soname,libglc-core.so.$(VERSION) -shared \
		 -lpthread -lpacketstream -lm \
		$(CORE_OBJS) -o $(BUILD)/libglc-core.so.$(RELEASE)
	ln -sf libglc-core.so.$(RELEASE) $(BUILD)/libglc-core.so.$(VERSION)
	ln -sf libglc-core.so.$(RELEASE) $(BUILD)/libglc-core.so

# capture library
$(BUILD)/libglc-capture.so.$(RELEASE): $(CAPTURE_OBJS) $(BUILD)/libglc-core.so.$(RELEASE)
	$(LD) $(LDFLAGS) -Wl,-soname,libglc-capture.so.$(VERSION) -shared \
		-L$(BUILD) -lGL -ldl -lasound -lXxf86vm -lglc-core \
		$(CAPTURE_OBJS) -o $(BUILD)/libglc-capture.so.$(RELEASE)
	ln -sf libglc-capture.so.$(RELEASE) $(BUILD)/libglc-capture.so.$(VERSION)
	ln -sf libglc-capture.so.$(RELEASE) $(BUILD)/libglc-capture.so

# playback library
$(BUILD)/libglc-play.so.$(RELEASE): $(PLAY_OBJS) $(BUILD)/libglc-core.so.$(RELEASE)
	$(LD) $(LDFLAGS) -Wl,-soname,libglc-play.so.$(VERSION) -shared \
		-L$(BUILD) -lGL -lasound -lglc-core \
		$(PLAY_OBJS) -o $(BUILD)/libglc-play.so.$(RELEASE)
	ln -sf libglc-play.so.$(RELEASE) $(BUILD)/libglc-play.so.$(VERSION)
	ln -sf libglc-play.so.$(RELEASE) $(BUILD)/libglc-play.so

# export library
$(BUILD)/libglc-export.so.$(RELEASE): $(EXPORT_OBJS) $(BUILD)/libglc-core.so.$(RELEASE)
	$(LD) $(LDFLAGS) -Wl,-soname,libglc-export.so.$(VERSION) -shared \
		-L$(BUILD) -lglc-core \
		$(EXPORT_OBJS) -o $(BUILD)/libglc-export.so.$(RELEASE)
	ln -sf libglc-export.so.$(RELEASE) $(BUILD)/libglc-export.so.$(VERSION)
	ln -sf libglc-export.so.$(RELEASE) $(BUILD)/libglc-export.so

# hook library
$(BUILD)/libglc-hook.so.$(RELEASE): $(BUILD)/libglc-capture.so.$(RELEASE) $(HOOK_OBJS)
	$(LD) $(LDFLAGS) -Wl,-soname,libglc-hook-so.$(VERSION) \
		-L$(BUILD) -lglc-core -lglc-capture -lelfhacks \
		-shared -o $(BUILD)/libglc-hook.so.$(RELEASE) $(HOOK_OBJS)
	ln -sf libglc-hook.so.$(RELEASE) $(BUILD)/libglc-hook.so.$(VERSION)
	ln -sf libglc-hook.so.$(RELEASE) $(BUILD)/libglc-hook.so

# player / tool
$(BUILD)/glc-play: $(BUILD)/play.o $(BUILD)/libglc-play.so.$(RELEASE)
	$(LD) $(LDFLAGS) -L$(BUILD) -lglc-core -lglc-play -lglc-export \
		-o $(BUILD)/glc-play $(BUILD)/play.o

$(BUILD)/play.o: $(SRC)/play.c $(HEADERS)
	$(CC) $(CFLAGS) $(FEATURES) -o $(BUILD)/play.o -c $(SRC)/play.c


# capture app
$(BUILD)/glc-capture: $(BUILD)/capture.o
	$(LD) $(LDFLAGS) -o $(BUILD)/glc-capture $(BUILD)/capture.o

$(BUILD)/capture.o: $(SRC)/capture.c
	$(CC) $(CFLAGS) $(FEATURES) -o $(BUILD)/capture.o -c $(SRC)/capture.c


# common objects
$(BUILD)/util.o: $(COMMON)/util.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/util.o -c $(COMMON)/util.c

$(BUILD)/thread.o: $(COMMON)/thread.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/thread.o -c $(COMMON)/thread.c


# glc core
$(BUILD)/pack.o: $(CORE)/pack.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/pack.o -c $(CORE)/pack.c

$(BUILD)/file.o: $(CORE)/file.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/file.o -c $(CORE)/file.c

$(BUILD)/scale.o: $(CORE)/scale.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/scale.o -c $(CORE)/scale.c

$(BUILD)/info.o: $(CORE)/info.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/info.o -c $(CORE)/info.c

$(BUILD)/ycbcr.o: $(CORE)/ycbcr.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/ycbcr.o -c $(CORE)/ycbcr.c

$(BUILD)/rgb.o: $(CORE)/rgb.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/rgb.o -c $(CORE)/rgb.c

$(BUILD)/color.o: $(CORE)/color.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/color.o -c $(CORE)/color.c


# capture
$(BUILD)/gl_capture.o: $(CAPTURE)/gl_capture.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/gl_capture.o -c $(CAPTURE)/gl_capture.c

$(BUILD)/audio_hook.o: $(CAPTURE)/audio_hook.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/audio_hook.o -c $(CAPTURE)/audio_hook.c

$(BUILD)/audio_capture.o: $(CAPTURE)/audio_capture.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/audio_capture.o -c $(CAPTURE)/audio_capture.c

# play
$(BUILD)/demux.o: $(PLAY)/demux.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/demux.o -c $(PLAY)/demux.c

$(BUILD)/gl_play.o: $(PLAY)/gl_play.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/gl_play.o -c $(PLAY)/gl_play.c

$(BUILD)/audio_play.o: $(PLAY)/audio_play.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/audio_play.o -c $(PLAY)/audio_play.c


# export
$(BUILD)/img.o: $(EXPORT)/img.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/img.o -c $(EXPORT)/img.c

$(BUILD)/wav.o: $(EXPORT)/wav.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/wav.o -c $(EXPORT)/wav.c

$(BUILD)/yuv4mpeg.o: $(EXPORT)/yuv4mpeg.c $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/yuv4mpeg.o -c $(EXPORT)/yuv4mpeg.c


# hook objects
$(BUILD)/main.o: $(HOOK)/main.c $(HOOK)/lib.h $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/main.o -c $(HOOK)/main.c

$(BUILD)/alsa.o: $(HOOK)/alsa.c $(HOOK)/lib.h $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/alsa.o -c $(HOOK)/alsa.c

$(BUILD)/opengl.o: $(HOOK)/opengl.c $(HOOK)/lib.h $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/opengl.o -c $(HOOK)/opengl.c

$(BUILD)/x11.o: $(HOOK)/x11.c $(HOOK)/lib.h $(HEADERS)
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(BUILD)/x11.o -c $(HOOK)/x11.c


# support code
$(LZO_OBJ): $(MINILZO)minilzo.c $(MINILZO)lzoconf.h $(MINILZO)lzodefs.h $(MINILZO)minilzo.h
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(LZO_OBJ) -c $(MINILZO)minilzo.c

$(QUICKLZ_OBJ): $(QUICKLZ)quicklz.c $(QUICKLZ)quicklz.h
	$(CC) $(SO_CFLAGS) $(FEATURES) -o $(QUICKLZ_OBJ) -c $(QUICKLZ)quicklz.c


install-scripts: $(SCRIPTS)/encode.sh $(SCRIPTS)/capture.sh $(SCRIPTS)/play.sh
	install -Dm 0644 $(SCRIPTS)/encode.sh $(DESTDIR)/usr/share/glc/encode.sh
	install -Dm 0644 $(SCRIPTS)/capture.sh $(DESTDIR)/usr/share/glc/capture.sh
	install -Dm 0644 $(SCRIPTS)/play.sh $(DESTDIR)/usr/share/glc/play.sh

install-libs: $(BUILD)/libglc-core.so.$(RELEASE) \
	      $(BUILD)/libglc-capture.so.$(RELEASE) \
	      $(BUILD)/libglc-play.so.$(RELEASE) \
	      $(BUILD)/libglc-export.so.$(RELEASE) \
	      $(BUILD)/libglc-hook.so.$(RELEASE)
	install -Dm 0755 $(BUILD)/libglc-core.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-core.so.$(RELEASE)
	ln -sf libglc-core.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-core.so.$(VERSION)
	ln -sf libglc-core.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-core.so

	install -Dm 0755 $(BUILD)/libglc-capture.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-capture.so.$(RELEASE)
	ln -sf libglc-capture.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-capture.so.$(VERSION)
	ln -sf libglc-capture.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-capture.so

	install -Dm 0755 $(BUILD)/libglc-play.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-play.so.$(RELEASE)
	ln -sf libglc-play.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-play.so.$(VERSION)
	ln -sf libglc-play.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-play.so

	install -Dm 0755 $(BUILD)/libglc-export.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-export.so.$(RELEASE)
	ln -sf libglc-export.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-export.so.$(VERSION)
	ln -sf libglc-export.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-export.so

	install -Dm 0755 $(BUILD)/libglc-hook.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-hook.so.$(RELEASE)
	ln -sf libglc-hook.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-hook.so.$(VERSION)
	ln -sf libglc-hook.so.$(RELEASE) $(DESTDIR)/usr/$(MLIBDIR)/libglc-hook.so

install: install-libs $(BUILD)/glc-play
	install -Dm 0755 $(BUILD)/glc-play $(DESTDIR)/usr/bin/glc-play
	install -Dm 0755 $(BUILD)/glc-capture $(DESTDIR)/usr/bin/glc-capture

clean:
	rm -f $(CORE_OBJS) \
	      $(CAPTURE_OBJS) \
	      $(PLAY_OBJS) \
	      $(EXPORT_OBJS) \
	      $(HOOK_OBJS) \
	      $(BUILD)/libglc-core.so.$(RELEASE) \
	      $(BUILD)/libglc-core.so.$(VERSION) \
	      $(BUILD)/libglc-core.so \
	      $(BUILD)/libglc-capture.so.$(RELEASE) \
	      $(BUILD)/libglc-capture.so.$(VERSION) \
	      $(BUILD)/libglc-capture.so \
	      $(BUILD)/libglc-play.so.$(RELEASE) \
	      $(BUILD)/libglc-play.so.$(VERSION) \
	      $(BUILD)/libglc-play.so \
	      $(BUILD)/libglc-export.so.$(RELEASE) \
	      $(BUILD)/libglc-export.so.$(VERSION) \
	      $(BUILD)/libglc-export.so \
	      $(BUILD)/libglc-hook.so.$(RELEASE) \
	      $(BUILD)/libglc-hook.so.$(VERSION) \
	      $(BUILD)/libglc-hook.so \
	      $(BUILD)/play.o \
	      $(BUILD)/glc-play \
	      $(BUILD)/capture.o \
	      $(BUILD)/glc-capture
