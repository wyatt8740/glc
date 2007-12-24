#!/bin/bash
#
# glc-build.sh -- glc build and install script
# Copyright (C) 2007 Pyry Haulos
#

info () {
	echo -e "\033[32minfo\033[0m  : $1"
}

ask () {
	echo -e "        $1"
}

ask-prompt () {
	echo -ne "      \033[34m>\033[0m "
}

error () {
	echo -e "\033[31merror\033[0m : $1"
}

die () {
	error "$1"
	exit 1
}

download () {
	wget -c -q "$1" || die "Can't fetch $1"
}

unpack () {
	tar -xzf "$1" || die "Can't unpack $1"
}

gitfetch () {
	GIT_CLONE=`which git-clone 2> /dev/null`
	GIT_PULL=`which git-pull 2> /dev/null`

	if [ -d "$1" ]; then
		cd "$1"
		$GIT_PULL origin || die "Can't update $1"
		cd ..
	else
		$GIT_CLONE "git://nullkey.ath.cx/~pyry/$1" \
			|| die "Can't clone $1"
	fi
}

info "Welcome to glc install script!"

BUILD64=0
uname -a | grep x86_64 > /dev/null && BUILD64=1

echo "#include <stdio.h>
	int main(int argc, char argv[]){printf(\"test\");return 0;}" | \
	gcc -x c - -o /dev/null 2> /dev/null \
	|| die "Can't compile (Ubuntu users: apt-get install build-essential)"
[ -e "/usr/include/X11/X.h" -a -e "/usr/include/X11/Xlib.h" ] \
	|| die "Missing X11 headers (Ubuntu users: apt-get install libx11-dev)"
[ -e "/usr/include/X11/extensions/xf86vmode.h" ] \
	|| die "Missing XF86VidMode headers (Ubuntu users: apt-get install libxxf86vm-dev)"
[ -e "/usr/include/GL/gl.h" -a -e "/usr/include/GL/glx.h" ] \
	|| die "Missing OpenGL headers (Ubuntu users: apt-get install libgl1-mesa-dev)"
[ -e "/usr/include/alsa/asoundlib.h" ] \
	|| die "Missing ALSA headers (Ubuntu users: apt-get install libasound2-dev)"

if [ $BUILD64 == 1 ]; then
	echo "#include <stdio.h>
		int main(int argc, char argv[]){printf(\"test\");return 0;}" | \
		gcc -m32 -x c - -o /dev/null 2> /dev/null \
		|| die "Can't compile 32-bit code (Ubuntu users: apt-get install gcc-multilib)"
fi

DEFAULT_CFLAGS="-O2 -msse -mmmx -fomit-frame-pointer"
[ $BUILD64 == 0 ] && DEFAULT_CFLAGS="${DEFAULT_CFLAGS} -mtune=pentium3"

ask "Enter path where glc will be installed."
ask "  (leave blank to install to root directory)"
ask-prompt
read DESTDIR
[ "${DESTDIR:${#DESTDIR}-1}" == "/" ] && DESTDIR="${DESTDIR:0:${#DESTDIR}-1}"
if [ "${DESTDIR}" != "" ]; then
	if [ -e "${DESTDIR}" ]; then
		[ -f "${DESTDIR}" ] && die "Invalid install directory"
	else
		mkdir -p "${DESTDIR}" 2> /dev/null \
			|| sudo mkdir -p "${DESTDIR}" 2> /dev/null \
			|| die "Can't create install directory"
	fi
fi

SUDOMAKE="sudo make"
[ -w "${DESTDIR}" ] && SUDOMAKE="make"

ask "Enter compiler optimizations."
ask "  (${DEFAULT_CFLAGS})"
ask-prompt
read CFLAGS
[ "${CFLAGS}" == "" ] && CFLAGS="${DEFAULT_CFLAGS}"

ask "Enter linker optimizations. (-Wl,-O1)"
ask-prompt
read LDFLAGS
[ "${LDFLAGS}" == "" ] && LDFLAGS="-Wl,-O1"

USE_GIT="n"
ask "Use git (y/n)"
ask "  (git contains latest unstable development version)"
ask-prompt
read USE_GIT

if [ "${USE_GIT}" == "y" ]; then
	GIT_CLONE=`which git-clone 2> /dev/null`
	if [ -x "${GIT_CLONE}" ]; then
		gitfetch glc
		gitfetch glc-support
		gitfetch elfhacks
		gitfetch packetstream
		cd glc && ln -sf ../glc-support ./support && cd ..
	else
		die "git-clone not found (Ubuntu users: apt-get install git-core)"
	fi
else
	info "Fetching sources..."
	download "http://nullkey.ath.cx/elfhacks/elfhacks.tar.gz"
	download "http://nullkey.ath.cx/packetstream/packetstream.tar.gz"
	download "http://nullkey.ath.cx/glc/glc.tar.gz"
	
	info "Unpacking sources..."
	unpack "elfhacks.tar.gz"
	unpack "packetstream.tar.gz"
	unpack "glc.tar.gz"
fi

info "Building elfhacks..."
cd elfhacks
make CFLAGS="${CFLAGS}" LDFLAGS="${LDFLAGS}" > /dev/null \
	|| die "Can't compile elfhacks"
if [ $BUILD64 == 1 ]; then
	make CFLAGS="${CFLAGS} -m32" LDFLAGS="${LDFLAGS} -m32" BUILD="build32" > /dev/null \
		|| die "Can't compile 32-bit elfhacks"
fi
cd ..

info "Building packetstream..."
cd packetstream
make CFLAGS="${CFLAGS}" LDFLAGS="${LDFLAGS}" > /dev/null \
	|| die "Can't compile packetstream"
if [ $BUILD64 == 1 ]; then
	make CFLAGS="${CFLAGS} -m32" LDFLAGS="${LDFLAGS} -m32" BUILD="build32" > /dev/null \
		|| die "Can't compile 32-bit packetstream"
fi
cd ..

info "Building glc..."
cd glc
LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:../elfhacks/build:../packetstream/build" \
	make \
	CFLAGS="${CFLAGS} -I../elfhacks/src -I../packetstream/src" \
	LDFLAGS="${LDFLAGS} -L../elfhacks/build -L../packetstream/build" \
	> /dev/null || die "Can't compile glc"
if [ $BUILD64 == 1 ]; then
	LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:../elfhacks/build32:../packetstream/build32" \
		make \
		CFLAGS="${CFLAGS} -m32 -I../elfhacks/src -I../packetstream/src " \
		LDFLAGS="${LDFLAGS} -m32 -L../elfhacks/build32 -L../packetstream/build32" \
		BUILD="build32" \
		> /dev/null || die "Can't compile 32-bit glc"
fi
cd ..

info "Installing elfhacks..."
cd elfhacks
if [ $BUILD64 == 1 ]; then
	$SUDOMAKE install MLIBDIR="lib64" DESTDIR="${DESTDIR}" > /dev/null \
		|| die "Can't install 64-bit elfhacks"
	$SUDOMAKE install MLIBDIR="lib32" DESTDIR="${DESTDIR}" BUILD="build32" > /dev/null \
		|| die "Can't install 32-bit elfhacks"
else
	$SUDOMAKE install DESTDIR="${DESTDIR}" > /dev/null \
		|| die "Can't install elfhacks"
fi
cd ..

info "Installing packetstream..."
cd packetstream

if [ $BUILD64 == 1 ]; then
	$SUDOMAKE install MLIBDIR="lib64" DESTDIR="${DESTDIR}" > /dev/null \
		|| die "Can't install 64-bit packetstream"
	$SUDOMAKE install MLIBDIR="lib32" DESTDIR="${DESTDIR}" BUILD="build32" > /dev/null \
		|| die "Can't install 32-bit packetstream"
else
	$SUDOMAKE install DESTDIR="${DESTDIR}" > /dev/null \
		|| die "Can't install packetstream"
fi
cd ..

info "Installing glc..."
cd glc
if [ $BUILD64 == 1 ]; then
	$SUDOMAKE install MLIBDIR="lib64" DESTDIR="${DESTDIR}" > /dev/null \
		|| die "Can't install 64-bit glc"
	$SUDOMAKE install-libs MLIBDIR="lib32" DESTDIR="${DESTDIR}" BUILD="build32" > /dev/null \
		|| die "Can't install 32-bit glc"
else
	$SUDOMAKE install MLIBDIR="lib" DESTDIR="${DESTDIR}" > /dev/null \
		|| die "Can't install glc"
fi
cd ..

info "Done :)"

LD_LIBRARY_PATH_ADD="${DESTDIR}/usr/lib"
[ $BUILD64 == 1 ] && LD_LIBRARY_PATH_ADD="${DESTDIR}/usr/lib64:${DESTDIR}/usr/lib32"

if [ "${DESTDIR}" != "" ]; then
	info "You may need to add following lines to your .bashrc:"
	echo "PATH=\"\${PATH}:${DESTDIR}/usr/bin\""
	echo "LD_LIBRARY_PATH=\"\$LD_LIBRARY_PATH:${LD_LIBRARY_PATH_ADD}\""
fi

RM="rm"
[ -w "${DESTDIR}/usr/bin/glc-play" ] || RM="sudo ${RM}"

# TODO more complete escape
RDIR=`echo "${DESTDIR}" | sed 's/ /\\ /g'`

info "If you want to remove glc, execute:"
if [ $BUILD64 == 1 ]; then
	echo "${RM} \\"
	echo "${RDIR}/usr/lib64/libglc-core.so* \\"
	echo "${RDIR}/usr/lib64/libglc-capture.so* \\"
	echo "${RDIR}/usr/lib64/libglc-play.so* \\"
	echo "${RDIR}/usr/lib64/libglc-export.so* \\"
	echo "${RDIR}/usr/lib64/libglc-hook.so* \\"
	echo "${RDIR}/usr/lib64/libelfhacks.so* \\"
	echo "${RDIR}/usr/lib64/libpacketstream.so* \\"
	echo "${RDIR}/usr/lib64/libelfhacks.so* \\"
	echo "${RDIR}/usr/lib32/libglc-core.so* \\"
	echo "${RDIR}/usr/lib32/libglc-capture.so* \\"
	echo "${RDIR}/usr/lib32/libglc-play.so* \\"
	echo "${RDIR}/usr/lib32/libglc-export.so* \\"
	echo "${RDIR}/usr/lib32/libglc-hook.so* \\"
	echo "${RDIR}/usr/lib32/libelfhacks.so* \\"
	echo "${RDIR}/usr/lib32/libpacketstream.so* \\"
else
	echo "${RM} \\"
	echo "${RDIR}/usr/lib/libglc-core.so* \\"
	echo "${RDIR}/usr/lib/libglc-capture.so* \\"
	echo "${RDIR}/usr/lib/libglc-play.so* \\"
	echo "${RDIR}/usr/lib/libglc-export.so* \\"
	echo "${RDIR}/usr/lib/libglc-hook.so* \\"
	echo "${RDIR}/usr/lib/libelfhacks.so* \\"
	echo "${RDIR}/usr/lib/libpacketstream.so* \\"
fi
echo "${RDIR}/usr/include/elfhacks.h \\"
echo "${RDIR}/usr/include/packetstream.h \\"
echo "${RDIR}/usr/bin/glc-capture \\"
echo "${RDIR}/usr/bin/glc-play"
