UNAME=$(shell uname -s)
ifeq "$(UNAME)" "Linux"
INCLUDE=
LIBS=-lasound
endif

ifeq "$(UNAME)" "Darwin"
#INCLUDE=-I/opt/local/include
LIBS=-lSDLmain -framework Carbon -framework CoreAudio -framework AudioToolbox -framework AudioUnit -framework Cocoa
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

plogg.o: plogg.cpp 
	g++ -g -c $(INCLUDE) -o plogg.o plogg.cpp

plogg: plogg.o 
	g++ -g -o plogg plogg.o -lsydneyaudio -lvorbis -ltheora -logg -lSDL $(LIBS)

clean: 
	rm *.o plogg
