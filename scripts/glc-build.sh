#!/bin/bash
#
# glc-build.sh -- glc build and install script
# Copyright (C) 2007-2008 Pyry Haulos
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
	[ -f "$1.tar.gz" ] && rm -f "$1.tar.gz"
	wget -q "http://nullkey.ath.cx/$1/$1.tar.gz" || die "Can't fetch $1"
}

unpack () {
	[ -d "$1" ] && rm -rdf "$1"
	tar -xzf "$1.tar.gz" || die "Can't unpack $1.tar.gz"
}

gitfetch () {
	GIT="`which git 2> /dev/null`"

	if [ -d "$1" ]; then
		cd "$1"
		$GIT pull origin || die "Can't update $1"
		cd ..
	else
		$GIT clone "git://github.com/nullkey/$1.git" \
			|| die "Can't clone $1"
	fi
}

info "Welcome to glc install script!"

BUILD64=0
uname -a | grep x86_64 > /dev/null && BUILD64=1

echo "#include <stdio.h>
	int main(int argc, char argv[]){printf(\"test\");return 0;}" | \
	gcc -x c - -o /dev/null 2> /dev/null \
	|| die "Can't compile (Ubuntu users: sudo apt-get install build-essential)"
[ -e "/usr/include/X11/X.h" -a -e "/usr/include/X11/Xlib.h" ] \
	|| die "Missing X11 headers (Ubuntu users: sudo apt-get install libx11-dev)"
[ -e "/usr/include/X11/extensions/xf86vmode.h" ] \
	|| die "Missing XF86VidMode headers (Ubuntu users: sudo apt-get install libxxf86vm-dev)"
[ -e "/usr/include/GL/gl.h" -a -e "/usr/include/GL/glx.h" ] \
	|| die "Missing OpenGL headers (Ubuntu users: sudo apt-get install libgl1-mesa-dev)"
[ -e "/usr/include/alsa/asoundlib.h" ] \
	|| die "Missing ALSA headers (Ubuntu users: sudo apt-get install libasound2-dev)"
[ -e "/usr/include/png.h" ] \
	|| die "Missing libpng headers (Ubuntu users: sudo apt-get install libpng12-dev)"
[ -x "/usr/bin/cmake" ] \
	|| die "CMake not installed (Ubuntu users: sudo apt-get install cmake)"

if [ $BUILD64 == 1 ]; then
	echo "#include <stdio.h>
		int main(int argc, char argv[]){printf(\"test\");return 0;}" | \
		gcc -m32 -x c - -o /dev/null 2> /dev/null \
		|| die "Can't compile 32-bit code (Ubuntu users: sudo apt-get install gcc-multilib)"
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

[ "${DESTDIR}" == "" ] && DESTDIR="/usr"

SUDOMAKE="sudo make"
[ -w "${DESTDIR}" ] && SUDOMAKE="make"

ask "Enter compiler optimizations."
ask "  (${DEFAULT_CFLAGS})"
ask-prompt
read CFLAGS
[ "${CFLAGS}" == "" ] && CFLAGS="${DEFAULT_CFLAGS}"

USE_GIT="n"
ask "Use git (y/n)"
ask "  (git contains latest unstable development version)"
ask-prompt
read USE_GIT

if [ "${USE_GIT}" == "y" ]; then
	if [ -x "`which git 2> /dev/null`" ]; then
		gitfetch glc
		gitfetch glc-support
		gitfetch elfhacks
		gitfetch packetstream
		cd glc && ln -sf ../glc-support ./support && cd ..
	else
		die "git not found (Ubuntu users: sudo apt-get install git-core)"
	fi
else
	info "Fetching sources..."
	download elfhacks
	download packetstream
	download glc
	
	info "Unpacking sources..."
	unpack elfhacks
	unpack packetstream
	unpack glc
fi

MLIBDIR="lib"
[ $BUILD64 == 1 ] && MLIBDIR="lib64"

info "Building elfhacks..."
[ -d elfhacks/build ] || mkdir elfhacks/build
cd elfhacks/build
cmake .. \
	-DCMAKE_INSTALL_PREFIX:PATH="${DESTDIR}" \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_C_FLAGS_RELEASE_RELEASE:STRING="${CFLAGS}" \
	-DMLIBDIR="${MLIBDIR}" > /dev/null \
	|| die "Can't compile elfhacks (cmake failed)"
make > /dev/null || die "Can't compile elfhacks"
if [ $BUILD64 == 1 ]; then
	cd ..
	[ -d build32 ] || mkdir build32
	cd build32
	cmake .. \
		-DCMAKE_INSTALL_PREFIX:PATH="${DESTDIR}" \
		-DCMAKE_BUILD_TYPE:STRING="Release" \
		-DCMAKE_C_FLAGS_RELEASE:STRING="${CFLAGS} -m32" \
		-DMLIBDIR="lib32" > /dev/null \
		|| die "Can't compile 32-bit elfhacks (cmake failed)"
	make > /dev/null || die "Can't compile 32-bit elfhacks"
fi
cd ../..

info "Building packetstream..."
[ -d packetstream/build ] || mkdir packetstream/build
cd packetstream/build
cmake .. \
	-DCMAKE_INSTALL_PREFIX:PATH="${DESTDIR}" \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_C_FLAGS_RELEASE:STRING="${CFLAGS}" \
	-DMLIBDIR="${MLIBDIR}" > /dev/null \
	|| die "Can't compile packetstream (cmake failed)"
make > /dev/null || die "Can't compile packetstream"
if [ $BUILD64 == 1 ]; then
	cd ..
	[ -d build32 ] || mkdir build32
	cd build32
	cmake .. \
		-DCMAKE_INSTALL_PREFIX:PATH="${DESTDIR}" \
		-DCMAKE_BUILD_TYPE:STRING="Release" \
		-DCMAKE_C_FLAGS_RELEASE:STRING="${CFLAGS} -m32" \
		-DMLIBDIR="lib32" > /dev/null \
		|| die "Can't compile 32-bit packetstream (cmake failed)"
	make > /dev/null || die "Can't compile 32-bit packetstream"
fi
cd ../..

info "Building glc..."

export CMAKE_INCLUDE_PATH="${PWD}/elfhacks/src:${PWD}/packetstream/src"
export CMAKE_LIBRARY_PATH="${PWD}/elfhacks/build/src:${PWD}/packetstream/build/src"

[ -d glc/build ] || mkdir glc/build
cd glc/build

cmake .. \
	-DCMAKE_INSTALL_PREFIX:PATH="${DESTDIR}" \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_C_FLAGS_RELEASE:STRING="${CFLAGS}" \
	-DMLIBDIR="${MLIBDIR}" > /dev/null \
	 || die "Can't compile glc (cmake failed)"
make > /dev/null || die "Can't compile glc"
if [ $BUILD64 == 1 ]; then
	cd ../..
	export CMAKE_LIBRARY_PATH="${PWD}/elfhacks/build32/src:${PWD}/packetstream/build32/src"
	[ -d glc/build32 ] || mkdir glc/build32
	cd glc/build32

	cmake .. \
		-DCMAKE_INSTALL_PREFIX:PATH="${DESTDIR}" \
		-DCMAKE_BUILD_TYPE:STRING="Release" \
		-DCMAKE_C_FLAGS_RELEASE:STRING="${CFLAGS} -m32" \
		-DBINARIES:BOOL=OFF \
		-DMLIBDIR="lib32" > /dev/null \
		|| die "Can't compile 32-bit glc (cmake failed)"
	make > /dev/null || die "Can't compile 32-bit glc"
fi
cd ../..

info "Installing elfhacks..."
cd elfhacks/build
if [ $BUILD64 == 1 ]; then
	$SUDOMAKE install > /dev/null \
		|| die "Can't install 64-bit elfhacks"
	cd ../build32
	$SUDOMAKE install > /dev/null \
		|| die "Can't install 32-bit elfhacks"
else
	$SUDOMAKE install > /dev/null \
		|| die "Can't install elfhacks"
fi
cd ../..

info "Installing packetstream..."
cd packetstream/build
if [ $BUILD64 == 1 ]; then
	$SUDOMAKE install > /dev/null \
		|| die "Can't install 64-bit packetstream"
	cd ../build32
	$SUDOMAKE install > /dev/null \
		|| die "Can't install 32-bit packetstream"
else
	$SUDOMAKE install > /dev/null \
		|| die "Can't install packetstream"
fi
cd ../..

info "Installing glc..."
cd glc/build
if [ $BUILD64 == 1 ]; then
	$SUDOMAKE install > /dev/null \
		|| die "Can't install 64-bit glc"
	cd ../build32
	$SUDOMAKE install > /dev/null \
		|| die "Can't install 32-bit glc"
else
	$SUDOMAKE install > /dev/null \
		|| die "Can't install glc"
fi
cd ../..

info "Done :)"

# TODO more complete escape
RDIR=`echo "${DESTDIR}" | sed 's/ /\\ /g'`

LD_LIBRARY_PATH_ADD="${RDIR}/lib"
[ $BUILD64 == 1 ] && LD_LIBRARY_PATH_ADD="${RDIR}/lib64:${RDIR}/lib32"

if [ "${DESTDIR}" != "" ]; then
	info "You may need to add following lines to your .bashrc:"
	echo "export PATH=\"\${PATH}:${RDIR}/bin\""
	echo "export LD_LIBRARY_PATH=\"\${LD_LIBRARY_PATH}:${LD_LIBRARY_PATH_ADD}\""
fi

RM="rm"
[ -w "${DESTDIR}/usr/bin/glc-play" ] || RM="sudo ${RM}"

info "If you want to remove glc, execute:"
if [ $BUILD64 == 1 ]; then
	echo "${RM} \\"
	echo "${RDIR}/lib64/libglc-core.so* \\"
	echo "${RDIR}/lib64/libglc-capture.so* \\"
	echo "${RDIR}/lib64/libglc-play.so* \\"
	echo "${RDIR}/lib64/libglc-export.so* \\"
	echo "${RDIR}/lib64/libglc-hook.so* \\"
	echo "${RDIR}/lib64/libelfhacks.so* \\"
	echo "${RDIR}/lib64/libpacketstream.so* \\"
	echo "${RDIR}/lib64/libelfhacks.so* \\"
	echo "${RDIR}/lib32/libglc-core.so* \\"
	echo "${RDIR}/lib32/libglc-capture.so* \\"
	echo "${RDIR}/lib32/libglc-play.so* \\"
	echo "${RDIR}/lib32/libglc-export.so* \\"
	echo "${RDIR}/lib32/libglc-hook.so* \\"
	echo "${RDIR}/lib32/libelfhacks.so* \\"
	echo "${RDIR}/lib32/libpacketstream.so* \\"
else
	echo "${RM} \\"
	echo "${RDIR}/lib/libglc-core.so* \\"
	echo "${RDIR}/lib/libglc-capture.so* \\"
	echo "${RDIR}/lib/libglc-play.so* \\"
	echo "${RDIR}/lib/libglc-export.so* \\"
	echo "${RDIR}/lib/libglc-hook.so* \\"
	echo "${RDIR}/lib/libelfhacks.so* \\"
	echo "${RDIR}/lib/libpacketstream.so* \\"
fi
echo "${RDIR}/include/elfhacks.h \\"
echo "${RDIR}/include/packetstream.h \\"
echo "${RDIR}/bin/glc-capture \\"
echo "${RDIR}/bin/glc-play"
