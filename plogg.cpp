// Copyright (C) 2009, Chris Double. All Rights Reserved.
// See the license at the end of this file.
#include <cassert>
#include <map>
#include <iostream>
#include <fstream>
#include <ogg/ogg.h>

using namespace std;

class OggStream
{
public:
  int mSerial;
  ogg_stream_state mState;
  int mPacketCount;

public:
  OggStream(int serial = -1) : 
    mSerial(serial),
    mPacketCount(0)
  { 
  }

  ~OggStream() {
    int ret = ogg_stream_clear(&mState);
    assert(ret == 0);
  }
};

typedef map<int, OggStream*> StreamMap; 

class OggDecoder
{
public:
  StreamMap mStreams;  

public:
  bool read_page(istream& stream, ogg_sync_state* oy, ogg_page* og);
  void play(istream& stream);
};

bool OggDecoder::read_page(istream& stream, ogg_sync_state* oy, ogg_page* og) {
  int ret = 0;
  if (!stream.good())
    return false;

  while(ogg_sync_pageout(oy, og) != 1) {
    // Returns a buffer that can be written too
    // with the given size. This buffer is stored
    // in the ogg synchronisation structure.
    char* buffer = ogg_sync_buffer(oy, 4096);
    assert(buffer);

    // Read from the file into the buffer
    stream.read(buffer, 4096);
    int bytes = stream.gcount();
    if (bytes == 0) {
      // End of file
      return false;
    }

    // Update the synchronisation layer with the number
    // of bytes written to the buffer
    ret = ogg_sync_wrote(oy, bytes);
    assert(ret == 0);
  }
  return true;
}

void OggDecoder::play(istream& stream) {
  ogg_sync_state oy;
  ogg_page og;
  ogg_stream_state to;
  int packets = 0;

  int ret = ogg_sync_init(&oy);
  assert(ret == 0);
  
  while (read_page(stream, &oy, &og)) {
    int serial = ogg_page_serialno(&og);
    OggStream* stream = 0;

    if(ogg_page_bos(&og)) {
      // At the beginning of the stream, read headers
      // Initialize the stream, giving it the serial
      // number of the stream for this page.
      stream = new OggStream(serial);
      ret = ogg_stream_init(&stream->mState, serial);
      assert(ret == 0);
      mStreams[serial] = stream;
    }

    assert(mStreams.find(serial) != mStreams.end());
    stream = mStreams[serial];

    // Add a complete page to the bitstream
    ret = ogg_stream_pagein(&stream->mState, &og);
    assert(ret == 0);
      
    // Return a complete packet of data from the stream
    ogg_packet packet;
    ret = ogg_stream_packetout(&stream->mState, &packet);	
    if (ret == 0) {
      // Need more data to be able to complete the packet
      continue;
    }
    else if (ret == -1) {
      // We are out of sync and there is a gap in the data.
      // Exit
      cout << "There is a gap in the data - we are out of sync" << endl;
      break;
    }

    // A packet is available, this is what we pass to the vorbis or
    // theora libraries to decode.
    stream->mPacketCount++;
  }

  // Cleanup
  ret = ogg_sync_clear(&oy);
  assert(ret == 0);
}

void usage() {
  cout << "Usage: plogg <filename>" << endl;
}

int main(int argc, const char* argv[]) {
  if (argc != 2) { 
    usage();
  }

  ifstream file(argv[1], ios::in | ios::binary);
  if (file) {
    OggDecoder decoder;
    decoder.play(file);
    file.close();
    for(StreamMap::iterator it = decoder.mStreams.begin();
	it != decoder.mStreams.end();
	++it) {
      OggStream* stream = (*it).second;
      cout << stream->mSerial << " has " << stream->mPacketCount << " packets" << endl;
      delete stream;
    }
  }
  
  return 0;
}
// Copyright (C) 2009 Chris Double. All Rights Reserved.
// The original author of this code can be contacted at: chris.double@double.co.nz
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 
// THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
// FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// DEVELOPERS AND CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
