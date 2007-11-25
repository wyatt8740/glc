#!/bin/bash
#
# capture.sh -- example: glc environment variables and usage
# Copyright (C) 2007 Pyry Haulos
# For conditions of distribution and use, see copyright notice in glc.h

# picture stream fps
export GLC_FPS=30

# out filename, %d => getpid()
export GLC_FILE="pid-%d.glc"

# scale pictures
export GLC_SCALE=1.0

# capture audio
export GLC_AUDIO=1

# install custom signal handler
export GLC_SIGHANDLER=0

# captured pictures and audio buffer size, in MiB
export GLC_UNCOMPRESSED_BUFFER_SIZE=25

# unscaled pictures buffer size, in MiB
export GLC_UNSCALED_BUFFER_SIZE=25

# compressed data buffer size, in MiB
export GLC_COMPRESSED_BUFFER_SIZE=50

# take picture at glFinish(), compiz needs this
export GLC_CAPTURE_GLFINISH=0

# take picture from front or back buffer
export GLC_CAPTURE=front

# compress stream using 'lzo', 'quicklz' or 'none'
export GLC_COMPRESS=quicklz

# try GL_ARB_pixel_buffer_object to speed up readback
export GLC_TRY_PBO=1

# Skip audio packets. Not skipping requires some busy
# waiting and can slow program down a quite bit.
export GLC_AUDIO_SKIP=1

# show indicator when capturing
# NOTE this doesn't work properly when capturing front buffer
export GLC_INDICATOR=0

# start capturing immediately
export GLC_START=0

# capture hotkey, <Ctrl> and <Shift> mods are supported
export GLC_HOTKEY="<Shift>F8"

# lock fps when capturing
export GLC_LOCK_FPS=0

# saved stream colorspace, bgr or 420jpeg
# set 420jpeg to convert to Y'CbCr (420JPEG) at capture
# NOTE this is a lossy operation
export GLC_COLORSPACE=420jpeg

# crop capture area to WxH+X+Y
# export GLC_CROP=WxH+X+Y

# record alsa devices
# format is device,rate,channels;device2...
# export GLC_AUDIO_RECORD=default,44100,1

# use GL_PACK_ALIGNMENT 8
export GLC_CAPTURE_DWORD_ALIGNED=1

# set SDL audiodriver to alsa
export SDL_AUDIODRIVER=alsa

# log verbosity
export GLC_LOG=1

# log file
export GLC_LOG_FILE="/dev/stderr"

LD_PRELOAD=libglc-capture.so "${@}"
