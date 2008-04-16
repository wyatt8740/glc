#!/bin/bash
#
# encode.sh -- encoding glc stream to x264-encoded video
# Copyright (C) 2007-2008 Pyry Haulos
# For conditions of distribution and use, see copyright notice in glc.h

FILE=""
QUALITY=""

AUDIO="1"
VIDEO="1"

BITRATE="2000"
QP="20"
CRF="18"

METHOD="qp"

OUTFMT="mp4"
OUT="video.${OUTFMT}"

PASSLOG="pass.log"
AUDIOTMP="audio.mp3.tmp"

MULTIPASS="no"
ADDOPTS=""

showhelp () {
	echo "$0 [option]... [glc stream file]"
	echo "  -o, --out=FILE       write video to FILE"
	echo "                        default is ${OUT}"
	echo "  -v, --video=NUM      video stream number"
	echo "                        default is ${VIDEO}"
	echo "  -a, --audio=NUM      audio stream number"
	echo "                        default is ${AUDIO}"
	echo "  -m, --method=METHOD  bitrate calculation method"
	echo "                        supported methods are bitrate, qp, crf"
	echo "                        default method is ${METHOD}"
	echo "  -q, --quality=VAL    quality parameter"
	echo "  -f, --outfmt=FORMAT  output container format"
	echo "                        default is ${OUTFMT}"
	echo "  -x, --addopts=OPTS   additional options to mencoder"
	echo "  -h, --help           show this help"
}

OPT_TMP=`getopt -o o:v:a:m:q:f:x:h --long out:video:audio:method:quality:outfmt:addopts \
	-n "$0" -- "$@"`
if [ $? != 0 ]; then showhelp; exit 1; fi

eval set -- "$OPT_TMP"

while true; do
	case "$1" in
		-o|--out)
			OUT="$2"
			shift 2
			;;
		-v|--video)
			VIDEO="$2"
			shift 2
			;;
		-a|--audio)
			AUDIO="$2"
			shift 2
			;;
		-m|--method)
			METHOD="$2"
			shift 2
			;;
		-q|--quality)
			QUALITY="$2"
			shift 2
			;;
		-f|--outfmt)
			OUTFMT="$2"
			shift 2
			;;
		-a|--addopts)
			ADDOPTS="$2"
			shift 2
			;;
		-h|--help)
			showhelp
			exit 0
			shift 2
			;;
		--)
			shift
			break
			;;
		*)
			showhelp
			exit 1
			;;
	esac
done

for arg do FILE=$arg; done
if [ "$FILE" == "" ]; then
	showhelp
	exit 1
fi

KEYINT=300

X264_OPTS="ref=4:mixed_refs:bframes=3:b_pyramid:bime:weightb:direct_pred=auto:filter=-1,0:partitions=all:turbo=1:threads=auto:keyint=${KEYINT}"
LAME_OPTS="q=4" # TODO configure q, cbr or abr

case ${METHOD} in
	crf)
		[ "$QUALITY" != "" ] && CRF=$QUALITY
		X264_OPTS="crf=${CRF}:${X264_OPTS}"
		MULTIPASS="no"
		;;
	bitrate)
		[ "$QUALITY" != "" ] && BITRATE=$QUALITY
		X264_OPTS="bitrate=${BITRATE}:${X264_OPTS}"
		MULTIPASS="yes"
		;;
	qp)
		[ "$QUALITY" != "" ] && QP=$QUALITY
		X264_OPTS="qp=${QP}:${X264_OPTS}"
		MULTIPASS="yes"
		;;
	*)
		showhelp
		exit 1
		;;
esac

[ "$OUTFMT" != "avi" ] && OUTFMT="lavf -lavfopts format=${OUTFMT}"

glc-play "${FILE}" -o - -a "${AUDIO}" | lame -hV2 - "${AUDIOTMP}"

if [ "${MULTIPASS}" == "no" ]; then
	glc-play "${FILE}" -o - -y "${VIDEO}" | \
		mencoder - \
			-audiofile "${AUDIOTMP}"\
			-demuxer y4m \
			-ovc x264 \
			-x264encopts "${X264_OPTS}" \
			-of ${OUTFMT} \
			${ADDOPTS} \
			-o "${OUT}"
else
	glc-play "${FILE}" -o - -y "${VIDEO}" | \
		mencoder - \
			-nosound \
			-demuxer y4m \
			-ovc x264 \
			-x264encopts "${X264_OPTS}:pass=1" \
			-passlogfile "${PASSLOG}" \
			-of ${OUTFMT} \
			${ADDOPTS} \
			-o "${OUT}"
	glc-play "${FILE}" -o - -y "${VIDEO}" | \
		mencoder - \
			-audiofile "${AUDIOTMP}" \
			-demuxer y4m \
			-ovc x264 \
			-x264encopts "${X264_OPTS}:pass=2" \
			-passlogfile "${PASSLOG}" \
			-oac copy \
			-of ${OUTFMT} \
			${ADDOPTS} \
			-o "${OUT}"
fi

rm -f "${PASSLOG}" "${AUDIOTMP}"
