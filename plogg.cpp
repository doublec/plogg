// Copyright (C) 2009, Chris Double. All Rights Reserved.
// See the license at the end of this file.
#include <cassert>
#include <cmath>
#include <map>
#include <iostream>
#include <fstream>
#include <ogg/ogg.h>
#include <theora/theora.h>
#include <theora/theoradec.h>
#include <vorbis/codec.h>
#include <SDL/SDL.h>

extern "C" {
#include <sydney_audio.h>
}

using namespace std;

enum StreamType {
  TYPE_VORBIS,
  TYPE_THEORA,
  TYPE_UNKNOWN
};

class TheoraDecode {
public:
  th_info mInfo;
  th_comment mComment;
  th_setup_info *mSetup;
  th_dec_ctx* mCtx;

public:
  TheoraDecode() :
    mSetup(0),
    mCtx(0)
  {
    th_info_init(&mInfo);
    th_comment_init(&mComment);
  }

  ~TheoraDecode() {
    th_setup_free(mSetup);
    th_decode_free(mCtx);
  }   
};

class VorbisDecode {
public:
  vorbis_info mInfo;
  vorbis_comment mComment;
  vorbis_dsp_state mDsp;
  vorbis_block mBlock;

public:
  VorbisDecode()
  {
    vorbis_info_init(&mInfo);
    vorbis_comment_init(&mComment);    
  }
};

class OggStream
{
public:
  int mSerial;
  ogg_stream_state mState;
  StreamType mType;
  bool mHeadersRead;
  TheoraDecode mTheora;
  VorbisDecode mVorbis;

public:
  OggStream(int serial = -1) : 
    mSerial(serial),
    mType(TYPE_UNKNOWN),
    mHeadersRead(false)
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
  SDL_Surface* mSurface;
  SDL_Overlay* mOverlay;
  sa_stream_t* mAudio;

private:
  bool read_page(istream& stream, ogg_sync_state* state, ogg_page* page);
  void handle_packet(OggStream* stream, ogg_packet* packet);
  void handle_theora_data(OggStream* stream, ogg_packet* packet);
  void handle_theora_header(OggStream* stream, ogg_packet* packet);
  void handle_vorbis_data(OggStream* stream, ogg_packet* packet);
  void handle_vorbis_header(OggStream* stream, ogg_packet* packet);

public:
  OggDecoder() :
    mSurface(0),
    mOverlay(0),
    mAudio(0)
  {
  }

  ~OggDecoder() {
    if (mAudio) {
      sa_stream_drain(mAudio);
      sa_stream_destroy(mAudio);
    }
    if (mSurface)
      SDL_FreeSurface(mSurface);
  }
  void play(istream& stream);
};

bool OggDecoder::read_page(istream& stream, ogg_sync_state* state, ogg_page* page) {
  int ret = 0;

  // If we've hit end of file we still need to continue processing
  // any remaining pages that we've got buffered.
  if (!stream.good())
    return ogg_sync_pageout(state, page) == 1;

  while((ret = ogg_sync_pageout(state, page)) != 1) {
    // Returns a buffer that can be written too
    // with the given size. This buffer is stored
    // in the ogg synchronisation structure.
    char* buffer = ogg_sync_buffer(state, 4096);
    assert(buffer);

    // Read from the file into the buffer
    stream.read(buffer, 4096);
    int bytes = stream.gcount();
    if (bytes == 0) {
      // End of file. 
      continue;
    }

    // Update the synchronisation layer with the number
    // of bytes written to the buffer
    ret = ogg_sync_wrote(state, bytes);
    assert(ret == 0);
  }
  return true;
}

void OggDecoder::play(istream& stream) {
  ogg_sync_state state;
  ogg_page page;
  int packets = 0;

  int ret = ogg_sync_init(&state);
  assert(ret == 0);
  
  while (read_page(stream, &state, &page)) {
    int serial = ogg_page_serialno(&page);
    OggStream* stream = 0;

    if(ogg_page_bos(&page)) {
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
    ret = ogg_stream_pagein(&stream->mState, &page);
    assert(ret == 0);
      
    // Process all available packets in the stream. When we
    // run out of packets, the while loop exits and we read more
    // pages if possible.
    ogg_packet packet;
    while ((ret = ogg_stream_packetout(&stream->mState, &packet)) != 0) {
      assert(ret == 1);

      // A packet is available, this is what we pass to the vorbis or
      // theora libraries to decode.
      handle_packet(stream, &packet);      
    }

    // Check for SDL events to exit
    SDL_Event event;
    if (SDL_PollEvent(&event) == 1) {
      if (event.type == SDL_KEYDOWN &&
	  event.key.keysym.sym == SDLK_ESCAPE)
	break;
      if (event.type == SDL_KEYDOWN &&
	  event.key.keysym.sym == SDLK_SPACE)
	SDL_WM_ToggleFullScreen(mSurface);
    } 
  }

  // Cleanup
  ret = ogg_sync_clear(&state);
  assert(ret == 0);

  SDL_Quit();
}

void OggDecoder::handle_packet(OggStream* stream, ogg_packet* packet) {
  if (stream->mHeadersRead && stream->mType == TYPE_THEORA)
    handle_theora_data(stream, packet);

  if (stream->mHeadersRead && stream->mType == TYPE_VORBIS)
    handle_vorbis_data(stream, packet);

  if (!stream->mHeadersRead &&
      (stream->mType == TYPE_THEORA || stream->mType == TYPE_UNKNOWN))
    handle_theora_header(stream, packet);

  if (!stream->mHeadersRead &&
      (stream->mType == TYPE_VORBIS || stream->mType == TYPE_UNKNOWN))
    handle_vorbis_header(stream, packet);
}

void OggDecoder::handle_theora_header(OggStream* stream, ogg_packet* packet) {
  int ret = th_decode_headerin(&stream->mTheora.mInfo,
			       &stream->mTheora.mComment,
			       &stream->mTheora.mSetup,
			       packet);
  if (ret == TH_ENOTFORMAT)
    return; // Not a theora header

  if (ret > 0) {
    // This is a theora header packet
    stream->mType = TYPE_THEORA;
    return;
  }

  // Any other return value is treated as a fatal error
  assert(ret == 0);

  // This is not a header packet. It is the first 
  // video data packet.
  stream->mTheora.mCtx = 
    th_decode_alloc(&stream->mTheora.mInfo, 
		    stream->mTheora.mSetup);
  assert(stream->mTheora.mCtx != NULL);
  stream->mHeadersRead = true;
  handle_theora_data(stream, packet);
}

void OggDecoder::handle_theora_data(OggStream* stream, ogg_packet* packet) {
  ogg_int64_t granulepos = -1;
  int ret = th_decode_packetin(stream->mTheora.mCtx,
			       packet,
			       &granulepos);
  assert(ret == 0 || ret == TH_DUPFRAME);

  // If the return code is TH_DUPFRAME then we don't need to
  // get the YUV data and display it since it's the same as
  // the previous frame.

  if (ret == 0) {
    // We have a frame. Get the YUV data
    th_ycbcr_buffer buffer;
    ret = th_decode_ycbcr_out(stream->mTheora.mCtx, buffer);
    assert(ret == 0);

    // Create an SDL surface to display if we haven't
    // already got one.
    if (!mSurface) {
      int r = SDL_Init(SDL_INIT_VIDEO);
      assert(r == 0);
      mSurface = SDL_SetVideoMode(buffer[0].width, 
				  buffer[0].height,
				  32,
				  SDL_SWSURFACE);
      assert(mSurface);
    }
   
    // Create a YUV overlay to do the YUV to RGB conversion
    if (!mOverlay) {
      mOverlay = SDL_CreateYUVOverlay(buffer[0].width,
				      buffer[0].height,
				      SDL_YV12_OVERLAY,
				      mSurface);
      assert(mOverlay);
    }

    SDL_Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = buffer[0].width;
    rect.h = buffer[0].height;
    
    SDL_LockYUVOverlay(mOverlay);
    for (int i=0; i < buffer[0].height; ++i)
      memcpy(mOverlay->pixels[0]+(mOverlay->pitches[0]*i), 
	     buffer[0].data+(buffer[0].stride*i), 
	     mOverlay->pitches[0]);
    
    for (int i=0; i < buffer[2].height; ++i)
      memcpy(mOverlay->pixels[2]+(mOverlay->pitches[2]*i), 
	     buffer[1].data+(buffer[1].stride*i), 
	     mOverlay->pitches[2]);
    
    for (int i=0; i < buffer[1].height; ++i)
      memcpy(mOverlay->pixels[1]+(mOverlay->pitches[1]*i), 
	     buffer[2].data+(buffer[2].stride*i), 
	     mOverlay->pitches[1]);
    
    SDL_UnlockYUVOverlay(mOverlay);	  
    SDL_DisplayYUVOverlay(mOverlay, &rect);
  }

  // Sleep for the time period of 1 frame
  float framerate = 
    float(stream->mTheora.mInfo.fps_numerator) / 
    float(stream->mTheora.mInfo.fps_denominator);

  // The delay is disabled if we've got audio. This is
  // because we aren't doing any form of a/v sync yet and
  // sleeping for a frame causes audio to underrun.
  // This will be fixed when a/v sync is addressed.
  if (!mAudio)
    SDL_Delay((1.0/framerate)*1000);
}

void OggDecoder::handle_vorbis_header(OggStream* stream, ogg_packet* packet) {
  int ret = vorbis_synthesis_headerin(&stream->mVorbis.mInfo,
				      &stream->mVorbis.mComment,
				      packet);
  // Unlike libtheora, libvorbis does not provide a return value to
  // indicate that we've finished loading the headers and got the
  // first data packet. To detect this I check if I already know the
  // stream type and if the vorbis_synthesis_headerin call failed.
  if (stream->mType == TYPE_VORBIS && ret == OV_ENOTVORBIS) {
    // First data packet
    ret = vorbis_synthesis_init(&stream->mVorbis.mDsp, &stream->mVorbis.mInfo);
    assert(ret == 0);
    ret = vorbis_block_init(&stream->mVorbis.mDsp, &stream->mVorbis.mBlock);
    assert(ret == 0);
    stream->mHeadersRead = true;
    handle_vorbis_data(stream, packet);
  }
  else if (ret == 0) {
    stream->mType = TYPE_VORBIS;
  }
}

void OggDecoder::handle_vorbis_data(OggStream* stream, ogg_packet* packet) {
  int ret = 0;
    
  if (vorbis_synthesis(&stream->mVorbis.mBlock, packet) == 0) {
    ret = vorbis_synthesis_blockin(&stream->mVorbis.mDsp, &stream->mVorbis.mBlock);
    assert(ret == 0);
  }

  float** pcm = 0;
  int samples = 0;
  while ((samples = vorbis_synthesis_pcmout(&stream->mVorbis.mDsp, &pcm)) > 0) {
    if (!mAudio) {
      ret = sa_stream_create_pcm(&mAudio,
				 NULL,
				 SA_MODE_WRONLY,
				 SA_PCM_FORMAT_S16_NE,
				 stream->mVorbis.mInfo.rate,
				 stream->mVorbis.mInfo.channels);
      assert(ret == SA_SUCCESS);

      ret = sa_stream_open(mAudio);
      assert(ret == SA_SUCCESS);
    }

    if (mAudio) {
      short buffer[samples * stream->mVorbis.mInfo.channels];
      short* p = buffer;
      for (int i=0;i < samples; ++i) {
	for(int j=0; j < stream->mVorbis.mInfo.channels; ++j) {
	  int v = static_cast<int>(floorf(0.5 + pcm[j][i]*32767.0));
	  if (v > 32767) v = 32767;
	  if (v <-32768) v = -32768;
	  *p++ = v;
	}
      }

      ret = sa_stream_write(mAudio, buffer, sizeof(buffer));
      assert(ret == SA_SUCCESS);
    }
	
    ret = vorbis_synthesis_read(&stream->mVorbis.mDsp, samples);
    assert(ret == 0);
  }
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
