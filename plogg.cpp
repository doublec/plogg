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
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

extern "C" {
#include <sydney_audio.h>
}

using namespace std;

class SoundServer {
public:
  boost::asio::io_service mService;
  boost::thread* mThread;
  int mChannels;
  int mRate;
  sa_stream_t* mAudio;
  ogg_int64_t mPosition;
  int mQueued;
  
public:
  SoundServer(int channels, int rate) : 
    mThread(0),
    mChannels(channels),
    mRate(rate),
    mAudio(0),
    mPosition(0),
    mQueued(0) {
  }

  ~SoundServer() {
    mService.post(boost::bind(&SoundServer::shutdown, this));
    cout << "Join" << endl;
    mThread->join();
    delete mThread;
  }

  void shutdown() {
    if (mAudio) {
      sa_stream_drain(mAudio);
      sa_stream_destroy(mAudio);
      mAudio = 0;
    }
  }

  void write_audio(short* data, int size) {
    int ret = 0;
    if (!mAudio) {
      ret = sa_stream_create_pcm(&mAudio,
				     NULL,
				     SA_MODE_WRONLY,
				     SA_PCM_FORMAT_S16_NE,
				     mRate,
				     mChannels);
      assert(ret == SA_SUCCESS);
    
      ret = sa_stream_open(mAudio);
      assert(ret == SA_SUCCESS);
    }

    ret = sa_stream_write(mAudio, data, size * sizeof(short));
    assert(ret == SA_SUCCESS);

    ret = sa_stream_get_position(mAudio, SA_POSITION_WRITE_SOFTWARE, &mPosition);
    assert(ret == SA_SUCCESS);
    
    mQueued -= size;
    delete[] data;
  }

  void run() {
    mService.run();
    cout << "event completed" << endl;
  }

  void queue(short* data, int size) {
    mQueued += size;
    mService.post(boost::bind(&SoundServer::write_audio, this, data, size));
    if (!mThread) {
      mThread = new boost::thread(boost::bind(&SoundServer::run, this));
    }
  }

  float get_time() {
    if (!mAudio)
      return 0.0;

    float audio_time = 
      float(mPosition) /
      float(mRate) /
      float(mChannels) /
      sizeof(short);
    return audio_time;
  }

  float get_queued_time() {
    if (!mAudio)
      return 0.0;

    float audio_time = 
      float(mQueued) /
      float(mRate) /
      float(mChannels);
    return audio_time;
  }
};

enum StreamType {
  TYPE_VORBIS,
  TYPE_THEORA,
  TYPE_UNKNOWN
};

class OggStream;

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

  void initForData(OggStream* stream);

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

  void initForData(OggStream* stream);
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

void TheoraDecode::initForData(OggStream* stream) {
  stream->mTheora.mCtx = 
    th_decode_alloc(&stream->mTheora.mInfo, 
		    stream->mTheora.mSetup);
  assert(stream->mTheora.mCtx != NULL);
  int ppmax = 0;
  int ret = th_decode_ctl(stream->mTheora.mCtx,
			  TH_DECCTL_GET_PPLEVEL_MAX,
			  &ppmax,
			  sizeof(ppmax));
  assert(ret == 0);
  ppmax = 0;
  ret = th_decode_ctl(stream->mTheora.mCtx,
		      TH_DECCTL_SET_PPLEVEL,
		      &ppmax,
		      sizeof(ppmax));
  assert(ret == 0);
}

void VorbisDecode::initForData(OggStream* stream) {
  int ret = vorbis_synthesis_init(&stream->mVorbis.mDsp, &stream->mVorbis.mInfo);
  assert(ret == 0);
  ret = vorbis_block_init(&stream->mVorbis.mDsp, &stream->mVorbis.mBlock);
  assert(ret == 0);
}

typedef map<int, OggStream*> StreamMap; 

class OggDecoder
{
public:
  StreamMap mStreams;  
  SoundServer* mSound;
  SDL_Surface* mSurface;
  SDL_Overlay* mOverlay;
  ogg_int64_t  mFrameGranulepos;
  
private:
  bool handle_theora_header(OggStream* stream, ogg_packet* packet);
  bool handle_vorbis_header(OggStream* stream, ogg_packet* packet);
  void read_headers(istream& stream, ogg_sync_state* state);

  bool read_page(istream& stream, ogg_sync_state* state, ogg_page* page);
  bool read_packet(istream& is, ogg_sync_state* state, OggStream* stream, ogg_packet* packet);
  bool peek_packet(istream& is, ogg_sync_state* state, OggStream* stream, ogg_packet* packet);
  void handle_theora_data(OggStream* stream, ogg_packet* packet, th_ycbcr_buffer& buffer);
  bool display_video(istream& is, ogg_sync_state* state, OggStream* video);
  void display_yuv(th_ycbcr_buffer& buffer);

public:
  OggDecoder() :
    mSound(0),
    mSurface(0),
    mOverlay(0),
    mFrameGranulepos(0)
  {
  }

  ~OggDecoder() {
    if (mSound) {
      delete mSound;
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

bool OggDecoder::read_packet(istream& is, ogg_sync_state* state, OggStream* stream, ogg_packet* packet) {
  int ret = 0;

  while ((ret = ogg_stream_packetout(&stream->mState, packet)) != 1) {
    ogg_page page;
    if (!read_page(is, state, &page))
      return false;

    int serial = ogg_page_serialno(&page);
    assert(mStreams.find(serial) != mStreams.end());
    OggStream* pageStream = mStreams[serial];
    
    ret = ogg_stream_pagein(&pageStream->mState, &page);
    assert(ret == 0);      
  }
  return true;
}

bool OggDecoder::peek_packet(istream& is, ogg_sync_state* state, OggStream* stream, ogg_packet* packet) {
  int ret = 0;

  while ((ret = ogg_stream_packetpeek(&stream->mState, packet)) != 1) {
    ogg_page page;
    if (!read_page(is, state, &page))
      return false;

    int serial = ogg_page_serialno(&page);
    assert(mStreams.find(serial) != mStreams.end());
    OggStream* pageStream = mStreams[serial];
    
    ret = ogg_stream_pagein(&pageStream->mState, &page);
    assert(ret == 0);      
  }
  return true;
}

void OggDecoder::read_headers(istream& stream, ogg_sync_state* state) {
  ogg_page page;

  bool headersDone = false;
  while (!headersDone && read_page(stream, state, &page)) {
    int ret = 0;
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

    // Process all available header packets in the stream. When we hit
    // the first data stream we don't decode it, instead we
    // return. The caller can then choose to process whatever data
    // streams it wants to deal with.
    ogg_packet packet;
    while (!headersDone &&
	   (ret = ogg_stream_packetpeek(&stream->mState, &packet)) != 0) {
      assert(ret == 1);

      // A packet is available. If it is not a header packet we exit.
      // If it is a header packet, process it as normal.
      headersDone = headersDone || handle_theora_header(stream, &packet);
      headersDone = headersDone || handle_vorbis_header(stream, &packet);
      if (!headersDone) {
	// Consume the packet
	ret = ogg_stream_packetout(&stream->mState, &packet);
	assert(ret == 1);
      }
    }
  } 
}

void OggDecoder::play(istream& is) {
  ogg_sync_state state;

  int ret = ogg_sync_init(&state);
  assert(ret == 0);

  // Read headers for all streams
  read_headers(is, &state);

  // Find and initialize the first theora and vorbis
  // streams. According to the Theora spec these can be considered the
  // 'primary' streams for playback.
  OggStream* video = 0;
  OggStream* audio = 0;
  for(StreamMap::iterator it = mStreams.begin(); it != mStreams.end(); ++it) {
    OggStream* stream = (*it).second;
    if (!video && stream->mType == TYPE_THEORA) {
      video = stream;
      video->mTheora.initForData(video);
    }

    if (!audio && stream->mType == TYPE_VORBIS) {
      audio = stream;
      audio->mVorbis.initForData(audio);
    }
  }

  if (video) {
    cout << "Video stream is " 
	 << video->mSerial << " "
	 << video->mTheora.mInfo.frame_width << "x" << video->mTheora.mInfo.frame_height
	 << endl;
  }
  if (audio) {
    cout << "Audio stream is " 
	 << audio->mSerial << " "
	 << audio->mVorbis.mInfo.channels << " channels "
	 << audio->mVorbis.mInfo.rate << "KHz"
	 << endl;

    mSound = new SoundServer(audio->mVorbis.mInfo.channels, audio->mVorbis.mInfo.rate);
  }

  // Read audio packets, sending audio data to the sound hardware.
  // When it's time to display a frame, decode the frame and display it.
  ogg_packet packet;
  while (read_packet(is, &state, audio, &packet)) {
    if (vorbis_synthesis(&audio->mVorbis.mBlock, &packet) == 0) {
      ret = vorbis_synthesis_blockin(&audio->mVorbis.mDsp, &audio->mVorbis.mBlock);
      assert(ret == 0);
    }

    float** pcm = 0;
    int samples = 0;
    while ((samples = vorbis_synthesis_pcmout(&audio->mVorbis.mDsp, &pcm)) > 0) {
      if (mSound) {
	if (samples > 0) {
	  short* buffer = new short[samples * audio->mVorbis.mInfo.channels];
	  short* p = buffer;
	  for (int i=0;i < samples; ++i) {
	    for(int j=0; j < audio->mVorbis.mInfo.channels; ++j) {
	      int v = static_cast<int>(floorf(0.5 + pcm[j][i]*32767.0));
	      if (v > 32767) v = 32767;
	      if (v <-32768) v = -32768;
	      *p++ = v;
	    }
	  }
	  
	  mSound->queue(buffer, samples * audio->mVorbis.mInfo.channels);
	}
	
	ret = vorbis_synthesis_read(&audio->mVorbis.mDsp, samples);
	assert(ret == 0);
      }
      display_video(is, &state, video);
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

  // Process all remaining video packets
  if (video) {
    float period = 1.0/(float(video->mTheora.mInfo.fps_numerator) / 
			float(video->mTheora.mInfo.fps_denominator));
  
    while (display_video(is, &state, video)) {
      SDL_Delay(period*1000);
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
  }

  // Cleanup
  ret = ogg_sync_clear(&state);
  assert(ret == 0);

  SDL_Quit();
}

bool OggDecoder::display_video(istream& is, ogg_sync_state* state, OggStream* video) {
  // The granule position of a video frame represents the time
  // that that frame should be displayed up to. So we get the
  // current time, compare it to the last granule position read.
  // If the time is greater than that it's time to display a new
  // video frame.
  //
  // The time is obtained from the audio system - this represents
  // the time of the audio data that the user is currently
  // listening to. In this way the video frame should be synced up
  // to the audio the user is hearing.
  //
  if (video) {
    float audio_time = mSound->get_time();
    float video_time = th_granule_time(video->mTheora.mCtx, mFrameGranulepos);
    float period = 1.0/(float(video->mTheora.mInfo.fps_numerator) / 
			float(video->mTheora.mInfo.fps_denominator));
    if (audio_time > video_time) {
      cout << "Before loop: " << mSound->get_queued_time() << endl;
      th_ycbcr_buffer buffer;
      bool display = false;
    while (audio_time > video_time) {
      // Decode one frame and display it. If no frame is available we
      // don't do anything.
      cout << audio_time << " " << video_time << " " << mSound->get_queued_time() << endl;
      if (mSound->get_queued_time() < 0.3)
	break;
      ogg_packet packet;
      if (read_packet(is, state, video, &packet)) {

	boost::posix_time::ptime s(boost::posix_time::microsec_clock::universal_time());
	handle_theora_data(video, &packet, buffer);
	display = true;       
	boost::posix_time::ptime e(boost::posix_time::microsec_clock::universal_time());
	cout << e-s << " " << endl;
	video_time = th_granule_time(video->mTheora.mCtx, mFrameGranulepos);
	audio_time = mSound->get_time();
	// Always display the keyframes
	if (th_packet_iskeyframe(&packet))
	  break;
      }
      else {
	return false;
      }
    }
    if (display)
      display_yuv(buffer);
    cout << "After loop" << endl;
    }
  }
  return true;
}

bool OggDecoder::handle_theora_header(OggStream* stream, ogg_packet* packet) {
  int ret = th_decode_headerin(&stream->mTheora.mInfo,
			       &stream->mTheora.mComment,
			       &stream->mTheora.mSetup,
			       packet);
  if (ret == TH_ENOTFORMAT)
    return false; // Not a theora header

  if (ret > 0) {
    // This is a theora header packet
    stream->mType = TYPE_THEORA;
    return false;
  }

  // Any other return value is treated as a fatal error
  assert(ret == 0);

  // This is not a header packet. It is the first 
  // video data packet.
  return true;
}

void OggDecoder::display_yuv(th_ycbcr_buffer& buffer) {
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

void OggDecoder::handle_theora_data(OggStream* stream, ogg_packet* packet, th_ycbcr_buffer& buffer) {
  // The granulepos for a packet gives the time of the end of the
  // display interval of the frame in the packet.  We keep the
  // granulepos of the frame we've decoded and use this to know the
  // time when to display the next frame.
  int ret = th_decode_packetin(stream->mTheora.mCtx,
			       packet,
			       &mFrameGranulepos);
  assert(ret == 0 || ret == TH_DUPFRAME);

  // If the return code is TH_DUPFRAME then we don't need to
  // get the YUV data and display it since it's the same as
  // the previous frame.

  if (1/*ret == 0*/) {
    // We have a frame. Get the YUV data
    ret = th_decode_ycbcr_out(stream->mTheora.mCtx, buffer);
    assert(ret == 0);
  }
}

bool OggDecoder::handle_vorbis_header(OggStream* stream, ogg_packet* packet) {
  int ret = vorbis_synthesis_headerin(&stream->mVorbis.mInfo,
				      &stream->mVorbis.mComment,
				      packet);
  // Unlike libtheora, libvorbis does not provide a return value to
  // indicate that we've finished loading the headers and got the
  // first data packet. To detect this I check if I already know the
  // stream type and if the vorbis_synthesis_headerin call failed.
  if (stream->mType == TYPE_VORBIS && ret == OV_ENOTVORBIS) {
    // First data packet
    return true;
  }
  else if (ret == 0) {
    stream->mType = TYPE_VORBIS;
  }
  return false;
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
