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

plogg.o: plogg.cpp
	g++ -g -c $(INCLUDE) -Ilocal/include -o plogg.o plogg.cpp

plogg: plogg.o local/lib/libogg.a
	g++ -g -o plogg plogg.o  local/lib/libogg.a $(LIBS)

clean: 
	rm *.o plogg
