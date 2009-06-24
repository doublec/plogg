#! /bin/sh
set -e
PREFIX=`pwd`/..
export PKG_CONFIG_PATH="$PREFIX/local/lib/pkgconfig"
cd libogg
./autogen.sh --prefix=$PREFIX/local --disable-shared --enable-static || exit 1
make && make install || exit 1
cd ../libvorbis
./autogen.sh --prefix=$PREFIX/local --with-ogg=$PREFIX/local --disable-shared --enable-static || exit 1
make && make install || exit 1
cd ../libtheora
./autogen.sh --prefix=$PREFIX/local --with-ogg=$PREFIX/local --with-vorbis=$PREFIX/local --disable-shared --enable-static || exit 1
make && make install || exit 1
cd ../libsydneyaudio
./autogen.sh || exit 1
SOUND_BACKEND=--with-alsa
if [ $(uname -s) = "Darwin" ]; then
  SOUND_BACKEND=""
fi
./configure --prefix=$PREFIX/local --disable-shared --enable-static "$SOUND_BACKEND" || exit 1
make && make install || exit 1
