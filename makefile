UNAME=$(shell uname -s)
ifeq "$(UNAME)" "Linux"
INCLUDE=
LIBS=-lasound
endif

ifeq "$(UNAME)" "Darwin"
INCLUDE=-I/opt/local/include
LIBS=-framework Carbon -framework CoreAudio -framework AudioToolbox -framework AudioUnit -framework Cocoa
endif

all: plogg

local/lib/libogg.a: thirdparty/build.sh
	cd thirdparty && ./build.sh && cd ..

local/lib/libtheora.a: thirdparty/build.sh
	cd thirdparty && ./build.sh && cd ..

local/lib/libvorbis.a: thirdparty/build.sh
	cd thirdparty && ./build.sh && cd ..

local/lib/libsydneyaudio.a: thirdparty/build.sh
	cd thirdparty && ./build.sh && cd ..

plogg.o: plogg.cpp local/lib/libogg.a local/lib/libtheora.a local/lib/libvorbis.a local/lib/libsydneyaudio.a
	g++ -g -c $(INCLUDE) -Ilocal/include -o plogg.o plogg.cpp

plogg: plogg.o 
	g++ -g -o plogg plogg.o local/lib/libsydneyaudio.a local/lib/libvorbis.a local/lib/libtheora.a local/lib/libogg.a -lSDL $(LIBS)

clean: 
	rm *.o plogg
