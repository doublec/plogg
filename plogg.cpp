// Copyright (C) 2009, Chris Double. All Rights Reserved.
// See the license at the end of this file.
#include <cassert>
#include <cmath>
#include <map>
#include <iostream>
#include <fstream>
#include <list>
#include <ogg/ogg.h>
#include <theora/theora.h>
#include <theora/theoradec.h>
#include <vorbis/codec.h>
#include <SDL/SDL.h>
#include <math.h>

extern "C" {
#include <sydney_audio.h>
}

using namespace std;

enum StreamType {
  TYPE_VORBIS,
  TYPE_THEORA,
  TYPE_SKELETON,
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
  bool mActive;
  TheoraDecode mTheora;
  VorbisDecode mVorbis;
  
  double GranuleTime(ogg_int64_t granulepos) {
    if (mType == TYPE_VORBIS) {
      return vorbis_granule_time(&mVorbis.mDsp, granulepos);
    } else if (mType = TYPE_THEORA) {
      return th_granule_time(&mTheora.mInfo, granulepos);
    } else {
      return -1;
    }
  }

public:
  OggStream(int serial = -1) : 
    mSerial(serial),
    mType(TYPE_UNKNOWN),
    mActive(true)
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

  // Set to a value between 0 and ppmax inclusive to experiment with
  // this parameter.
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

class ProgressBar;
class AudioSample;

class OggDecoder
{
public:
  StreamMap mStreams;  
  SDL_Surface* mSurface;
  SDL_Overlay* mOverlay;
  sa_stream_t* mAudio;
  // Granulepos of last theora packet returned by read_theora_packet().
  ogg_int64_t  mGranulepos;
  ProgressBar* mProgressBar;
  
  // All in seconds.
  double mStartTime;
  double mPlaybackStartTime;  
  double mCurrentTime;
  double mEndTime;
  double mSeekTime;

private:
  bool handle_theora_header(OggStream* stream, ogg_packet* packet);
  bool handle_vorbis_header(OggStream* stream, ogg_packet* packet);
  bool handle_skeleton_header(OggStream* stream, ogg_packet* packet);
  void read_headers(istream& stream, ogg_sync_state* state);

  // Returns offset of page start in file, or -1 if no page can be read.
  ogg_int64_t read_page(istream& stream, ogg_sync_state* state, ogg_page* page);
  bool read_packet(istream& is, ogg_sync_state* state, OggStream* stream, ogg_packet* packet);
  void handle_theora_data(OggStream* stream, ogg_packet* packet);

  int decode_theora(OggStream* stream,
                    ogg_packet* packet,
                    th_ycbcr_buffer buffer);

  void draw_theora(th_ycbcr_buffer buffer);

  // Decodes the next audio packet.
  AudioSample* decode_audio(ogg_sync_state* state,
                            istream& is,
                            OggStream* audio);

  // Plays an audio sample. Delete the sample after.
  void play_audio(AudioSample* sample);

  // Returns number of seconds we've been playing audio.
  double get_audio_position(OggStream* audio);  

  // Determines the mLength and mEndTime fields.
  void get_end_time(istream& stream, ogg_sync_state* state);
  
  void close_audio();
  void open_audio(int rate, int channels);

  // Seeks to mSeekTime.
  int seek(ogg_sync_state* state,
           istream& is,
           OggStream* audio,
           OggStream* video);

  // Returns the next theora packet, removing it from the steram.
  bool read_theora_packet(istream& is,
                          ogg_sync_state* state, 
                          OggStream* video,
                          ogg_packet* packet,
                          bool& used_buffered_packet);

  // Returns the next theora packet, without removing it from the stream.
  bool peek_theora_packet(istream& is,
                          ogg_sync_state* state,
                          OggStream* video,
                          ogg_packet* packet);

  // Gets the presentation time of the next audio packet.
  double get_audio_start_time(istream& is,
                              ogg_sync_state* state,
                              OggStream* audio,
                              bool play_audio);


  void reset_decode(ogg_sync_state* state,
                    OggStream* audio,
                    OggStream* video);

  // Offset of the page which was last read.
  ogg_int64_t mPageOffset;
  
  // Offset of first non-header page in file.
  ogg_int64_t mDataOffset;

  // Length of the media in bytes.
  ogg_int64_t mLength;

  // We normally keep count of the packets' granulepos as we decode, and 
  // if we encounter a packet with a -1 granulepos, we can just increment the
  // previous known granulepos. However after we seek, we don't know the
  // previous granulepos, so we read and buffer packets until we encounter a
  // packet with a non -1 granulepos. We can then calculate the granulepos of
  // the buffered packets, which we store in this list.
  list<ogg_packet*> mVideoPackets;

  // List of audio data we've decoded. We buffer audio packets here to
  // faciliate calculating their duration and start times.
  list<AudioSample*> mAudioSamples;
  
public:
  OggDecoder();
  ~OggDecoder();
  void play(istream& stream);
  void seek(double seek_target);
};

// Encapsulates the progress and seekbar rendering.
class ProgressBar {
private:
  int mHeight;
  int mPadding;
  int mBorder;
  double mVisibleDelay;
  double mStartTime;
  double mCurrentTime;
  double mEndTime;
  SDL_Surface* mSurface;
  OggDecoder* mDecoder;
  double mHideTime;
public:
  ProgressBar(SDL_Surface* surface,
              OggDecoder* decoder,
              double start_time, // in seconds
              double end_time, // in seconds
              double visible_delay, // in seconds
              int height, // in pixels
              int padding, // in pixels
              int border) // in pixels
    : mSurface(surface),
      mDecoder(decoder),
      mStartTime(start_time),
      mCurrentTime(0),
      mEndTime(end_time),
      mHeight(height),
      mPadding(padding),
      mBorder(border),
      mVisibleDelay(visible_delay),
      mHideTime(5.0)
  {
  }
  
  void draw(double current_time);
  void handle(SDL_Event& event);

private:
  SDL_Rect progress_border_rect();
  SDL_Rect progress_background_rect();
  SDL_Rect progress_rect();
  void update_hide_time();
};

SDL_Rect ProgressBar::progress_border_rect() {
  SDL_Rect border;
  border.x = mPadding;
  border.y = mSurface->h - mPadding - mHeight;
  border.w = mSurface->w - mPadding * 2;
  border.h = mHeight;
  return border;
}

SDL_Rect ProgressBar::progress_background_rect() {
  SDL_Rect background;
  background.x = mPadding + mBorder;
  background.y = mSurface->h - mPadding - mHeight + mBorder;
  background.w = mSurface->w - 2 * mPadding - 2 * mBorder;
  background.h = mHeight - 2 * mBorder;
  return background;
}

SDL_Rect ProgressBar::progress_rect() {
  double duration = mEndTime - mStartTime;
  double position = mCurrentTime - mStartTime;
  
  SDL_Rect background = progress_background_rect();
  double maxWidth = background.w - 2 * mBorder;
  SDL_Rect progress;
  progress.x = background.x + mBorder;
  progress.y = background.y + mBorder;
  progress.h = background.h - 2 * mBorder;
  progress.w = max(1, (int)(maxWidth * position / duration));
  return progress;
}

void ProgressBar::draw(double current_time) {
  mCurrentTime = current_time;
  
  if (mCurrentTime > mHideTime)
    return;
  
  assert(mStartTime != -1);
  assert(mEndTime != -1);
  assert(mCurrentTime != -1);
  
  SDL_Rect border = progress_border_rect();
  unsigned white = SDL_MapRGB(mSurface->format, 255, 255, 255);
  int err = SDL_FillRect(mSurface, &border, white);
  assert(err == 0);

  SDL_Rect background = progress_background_rect();
  unsigned black = SDL_MapRGB(mSurface->format, 0, 0, 0);
  err = SDL_FillRect(mSurface, &background, black);
  assert(err == 0);

  SDL_Rect progress = progress_rect();
  unsigned gray = SDL_MapRGB(mSurface->format, 0xd6, 0xd6, 0xd6);
  err = SDL_FillRect(mSurface, &progress, gray);
  assert(err == 0);
  
  SDL_Flip(mSurface);
}

bool static isInside(int x, int y, const SDL_Rect& rect) {
  return x > rect.x &&
         x < rect.x + rect.w &&
         y > rect.y &&
         y < rect.y + rect.h;
}

void ProgressBar::handle(SDL_Event& event) {
  if (event.type == SDL_MOUSEMOTION) {
    mHideTime = mCurrentTime + mVisibleDelay;
  }
  
  if (event.type == SDL_MOUSEBUTTONDOWN &&
      event.button.button == SDL_BUTTON_LEFT) {
    int x = event.button.x;
    int y = event.button.y;
    SDL_Rect background = progress_background_rect();
    SDL_Rect progress = progress_rect();
    if (isInside(x, y, background)) {
      double progressWidth = background.w - 2 * mBorder;
      double proportion = (x - progress.x) / progressWidth;
      double duration = mEndTime - mStartTime;
      double seekTime = duration * proportion;
      mDecoder->seek(seekTime);
    }
  }
}

static ogg_packet* clone(ogg_packet* op) {
  ogg_packet* c = new ogg_packet();
  memcpy(c, op, sizeof(ogg_packet));
  c->packet = new unsigned char[c->bytes];
  memcpy(c->packet, op->packet, op->bytes);
  return c;
}

static void clear(list<ogg_packet*>& packets) {
  list<ogg_packet*>::iterator itr = packets.begin();
  while (itr != packets.end()) {
    ogg_packet* clone = *itr;
    delete clone->packet;
    delete clone;
    itr++;
  }
  packets.clear();
}

// Stores decoded sounds data with end-time and duration info.
class AudioSample {
public:
  AudioSample(short* buffer, int size, int samples) :
    mBuffer(buffer),
    mSize(size),
    mSamples(samples),
    mGranulepos(0) {}

  ~AudioSample() {
    if (mBuffer) {
      delete mBuffer;
      mBuffer = 0;
      mSize = 0;
    }
  }

  // Sound data.
  short* mBuffer;

  // Number of elements in mBuffer.
  int mSize;
  
  // End time.
  ogg_int64_t mGranulepos;
  
  // Number of samples. Start time is mGranulepos - mSamples.
  int mSamples;
};

static void clear(list<AudioSample*>& samples) {
  list<AudioSample*>::iterator itr = samples.begin();
  while (itr != samples.end()) {
    delete *itr;
    itr++;
  }
  samples.clear();
}

OggDecoder::OggDecoder() :
  mSurface(0),
  mOverlay(0),
  mAudio(0),
  mGranulepos(0),
  mStartTime(-1),
  mEndTime(-1),
  mPageOffset(0),
  mDataOffset(0),
  mLength(0),
  mCurrentTime(0),
  mProgressBar(0),
  mSeekTime(-1),
  mPlaybackStartTime(-1)
{
}

OggDecoder::~OggDecoder() {
  close_audio();
  if (mSurface) {
    SDL_FreeSurface(mSurface);
    mSurface = 0;
  }
  if (mProgressBar) {
    delete mProgressBar;
    mProgressBar = 0;
  }
}

ogg_int64_t OggDecoder::read_page(istream& stream, ogg_sync_state* state, ogg_page* page) {
  int ret = 0;
  ogg_int64_t offset = 0;

  // If we've hit end of file we still need to continue processing
  // any remaining pages that we've got buffered.
  if (!stream.good()) {
    if (ogg_sync_pageout(state, page) == 1) {
      offset = mPageOffset;
      mPageOffset += page->header_len + page->body_len;
      return offset;
    } else {
      return -1;
    }
  }

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
  
  offset = mPageOffset;
  mPageOffset += page->header_len + page->body_len;
  return offset;
}

bool OggDecoder::read_packet(istream& is, ogg_sync_state* state, OggStream* stream, ogg_packet* packet) {
  int ret = 0;

  while ((ret = ogg_stream_packetout(&stream->mState, packet)) != 1) {
    ogg_page page;
    if (read_page(is, state, &page) == -1)
      return false;

    int serial = ogg_page_serialno(&page);
    assert(mStreams.find(serial) != mStreams.end());
    OggStream* pageStream = mStreams[serial];

    // Drop data for streams we're not interested in.
    if (stream->mActive) {
      ret = ogg_stream_pagein(&pageStream->mState, &page);
      assert(ret == 0);
    }
  }
  return true;
}

void OggDecoder::read_headers(istream& stream, ogg_sync_state* state) {
  ogg_page page;

  bool headersDone = false;
  ogg_int64_t offset = 0;
  while (!headersDone &&
         (offset = read_page(stream, state, &page)) != -1) {
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
        headersDone = headersDone || handle_skeleton_header(stream, &packet);
        if (!headersDone) {
          // Consume the packet
          ret = ogg_stream_packetout(&stream->mState, &packet);
          assert(ret == 1);
        } else {
          // First non-header page. Remember its location, so we can seek
          // to time 0.
          mDataOffset = offset;
        }
    }
  } 
  assert(mDataOffset != 0);
}
  
void OggDecoder::get_end_time(istream& stream, ogg_sync_state* state) {
  // Seek to the end of file to find the length and duration.
  ogg_sync_reset(state);
  stream.seekg(0, ios::end);
  mLength = stream.tellg();

  const int step = 5000;
  while (1) {
    ogg_page page;    
    int ret = ogg_sync_pageseek(state, &page);
    if (ret == 0) {
      char* buffer = ogg_sync_buffer(state, step);
      assert(buffer);

      // Read from the file into the buffer
      stream.seekg(-step, ios::cur);        
      stream.read(buffer, step);
      int bytes = stream.gcount();
      assert(bytes != 0);

      // Update the synchronisation layer with the number
      // of bytes written to the buffer
      ret = ogg_sync_wrote(state, bytes);
      assert(ret == 0);    
      continue;
    }
    
    if (ret < 0 || ogg_page_granulepos(&page) == -1)
      continue;
  
    ogg_int64_t gp = ogg_page_granulepos(&page);
    int serialno = ogg_page_serialno(&page);
    OggStream *os = mStreams[serialno];
    mEndTime = os->GranuleTime(gp);
    break;
  }
  ogg_sync_reset(state);
  stream.seekg(mDataOffset, ios::beg);  
}

void OggDecoder::seek(double seek_target) {
  cout << "Seek to " << seek_target << "s" << endl;
  mSeekTime = seek_target;
}

void OggDecoder::close_audio() {
  if (mAudio) {
    sa_stream_drain(mAudio);
    sa_stream_destroy(mAudio);
    mAudio = 0;
  }
}

void OggDecoder::open_audio(int rate, int channels) {
  assert(!mAudio);
  int ret = sa_stream_create_pcm(&mAudio,
                                 NULL,
                                 SA_MODE_WRONLY,
                                 SA_PCM_FORMAT_S16_NE,
                                 rate,
                                 channels);
  assert(ret == SA_SUCCESS);

  ret = sa_stream_open(mAudio);
  assert(ret == SA_SUCCESS);
  
  mPlaybackStartTime = -1;
}

void OggDecoder::reset_decode(ogg_sync_state* state,
                              OggStream* audio,
                              OggStream* video)
{
  ogg_stream_reset(&audio->mState);
  ogg_stream_reset(&video->mState);
  clear(mVideoPackets);
  clear(mAudioSamples);
  mGranulepos = -1;
}

static ogg_int64_t s_to_ms(double s) {
  return (ogg_int64_t)(s * 1000 + 0.5);
}

int OggDecoder::seek(ogg_sync_state* state,
                     istream& is,
                     OggStream* audio,
                     OggStream* video)
{
  ogg_int64_t pageOffset = mPageOffset;
    
  if (mSeekTime == 0) {
    is.seekg(mDataOffset);
    mPageOffset = mDataOffset;
    ogg_sync_reset(state);
    return 0;
  }
  
  // Bisection search, find start offset of last page with end time less than
  // the seek target.
  ogg_int64_t offset_start = mDataOffset;
  ogg_int64_t offset_end = mLength;
  ogg_int64_t seek_target = s_to_ms(mSeekTime);
  ogg_int64_t time_start = s_to_ms(mStartTime);
  ogg_int64_t time_end = s_to_ms(mEndTime);
  ogg_int64_t theora_frame_duration =
    s_to_ms((double)video->mTheora.mInfo.fps_denominator /
            (double)video->mTheora.mInfo.fps_numerator);
  int hops = 0;
  ogg_int64_t previous_guess = -1;
  const int step = 5000; // Mean page length is about 4300 bytes.
  int backsteps = 1;
  while (true) {
  
    // Reset the streams so that we don't mess up further iterations.
    reset_decode(state, audio, video);
      
    ogg_int64_t duration = time_end - time_start;
    double target = (double)(seek_target - time_start) / (double)duration;
    ogg_int64_t interval = offset_end - offset_start;
    ogg_int64_t offset_guess = 1;

    offset_guess = offset_start + (ogg_int64_t)((double)interval * target);

    //cout << "start = " << offset_start << endl
    //     << "guess = " << offset_guess << endl
    //     << "end   = " << offset_end << endl
    //     << "interval = " << interval << endl
    //     << "target = " << target << endl;

    if (interval < step) {
      is.seekg(offset_start);
      //cout << "(interval < step)" << endl;
      break;      
    } else if (offset_guess + step > offset_end) {
      // Don't seek too close to the end of the interval, backoff
      // exponentially form the end.
      int backoff = (int)(step * pow(2.0, backsteps));
      backsteps++;
      offset_guess = max(offset_end - backoff, offset_start + step/2);
      //cout << "(offset_guess + step > offset_end)" << endl;
    } else {
      backsteps = 0;
    }
    
    assert(offset_guess >= offset_start);
    assert(offset_guess <= offset_end);
    assert(offset_guess != previous_guess);
    previous_guess = offset_guess;
    
    hops++;
    is.seekg(offset_guess);
  
    // We've seeked into the media somewhere. Locate the next page, and then
    // figure out the granule time of the audio and video streams there.
    // We can then make a bisection decision based on our location in the media.
    
    // Sync to the next page.
    ogg_sync_reset(state);
    ogg_int64_t offset = 0;
    int page_length = 0;
    int skipped_page_length = 0;
    ogg_page page;
    int ret = 0;
    while (ret <= 0) {
      ret = ogg_sync_pageseek(state, &page);
      if (ret == 0) {
        char* buffer = ogg_sync_buffer(state, step);
        assert(buffer);

        // Read from the file into the buffer
        is.read(buffer, step);
        int bytes = is.gcount();
        assert(bytes != 0);

        // Update the synchronisation layer with the number
        // of bytes written to the buffer
        ret = ogg_sync_wrote(state, bytes);
        assert(ret == 0);    
        continue;
      }
      
      if (ret < 0) {
        assert(offset >= 0);
        offset += -ret;
        assert(offset >= 0);
        continue;
      }
    }

    // We've located a page of length |ret| at |offset_guess + offset|.
    // Remember where the page is located.
    mPageOffset = offset_guess + offset;

    // Read pages until we can determine the granule time of the audio and 
    // video stream.
    ogg_int64_t audio_time = -1;
    ogg_int64_t video_time = -1;
    while (audio_time == -1 || video_time == -1) {
    
      // Add the page to its stream, determine its granule time.
      int serialno = ogg_page_serialno(&page);
      OggStream *stream = mStreams[serialno];
      if (stream->mActive) {
        ret = ogg_stream_pagein(&stream->mState, &page);
        assert(ret == 0);
      }      

      ogg_int64_t granulepos = ogg_page_granulepos(&page);

      if (granulepos != -1 &&
          serialno == audio->mSerial &&
          audio_time == -1) {
        audio_time = s_to_ms(audio->GranuleTime(granulepos));
      }
      
      if (granulepos != -1 &&
          serialno == video->mSerial &&
          video_time == -1) {
        video_time = s_to_ms(video->GranuleTime(granulepos));
      }

      mPageOffset += page.header_len + page.body_len;
      if (read_page(is, state, &page) == -1) {
        break;
      }
    }

    ogg_int64_t granule_time = min(audio_time, video_time);
    assert(granule_time > 0);
  
    //cout << "Time at " << offset_guess << " is " << granule_time
    //     << " (a=" << audio_time << ", v=" << video_time << ")" << endl;

    if (granule_time >= seek_target) {
      // We've landed after the seek target.
      ogg_int64_t old_offset_end = offset_end;
      offset_end = offset_guess;
      assert(offset_end < old_offset_end);
      time_end = granule_time;
    } else if (granule_time < seek_target) {
      // Landed before seek target.
      ogg_int64_t old_offset_start = offset_start;
      offset_start = offset_guess + offset;
      assert(offset_start > old_offset_start);
      time_start = granule_time;
    }
    
    assert(time_start < seek_target);
    assert(time_end >= seek_target);
    assert(offset_start != offset_end);
  }

  cout << "Seek complete in " << hops << " bisections." << endl;

  return 0;
}

void OggDecoder::play_audio(AudioSample* sample) {
  int ret = sa_stream_write(mAudio,
                            sample->mBuffer,
                            sizeof(*sample->mBuffer) * sample->mSize);
  assert(ret == SA_SUCCESS);
}

AudioSample* OggDecoder::decode_audio(ogg_sync_state* state,
                                      istream& is,
                                      OggStream* audio) {
  int ret = 0;

  if (mAudioSamples.size() > 0) {
    assert(mAudioSamples.front()->mGranulepos > 0);
    AudioSample* sample = mAudioSamples.front();
    mAudioSamples.pop_front();
    return sample;
  }

  // We've not any buffered audio samples. Read and decode a page of data.
  ogg_packet packet;

  while ((ret = ogg_stream_packetout(&audio->mState, &packet)) != 1) {
    ogg_page page;
    if (read_page(is, state, &page) == -1)
      return 0;

    int serial = ogg_page_serialno(&page);
    assert(mStreams.find(serial) != mStreams.end());
    OggStream* pageStream = mStreams[serial];

    // Drop data for streams we're not interested in.
    if (pageStream->mActive) {
      ret = ogg_stream_pagein(&pageStream->mState, &page);
      assert(ret == 0);
    }
  }

  do {
    // We've read an audio packet, decode it.
    if (vorbis_synthesis(&audio->mVorbis.mBlock, &packet) == 0) {
      ret = vorbis_synthesis_blockin(&audio->mVorbis.mDsp, &audio->mVorbis.mBlock);
      assert(ret == 0);
    }

    float** pcm = 0;
    int samples = 0;

    while ((samples = vorbis_synthesis_pcmout(&audio->mVorbis.mDsp, &pcm)) > 0) {
      
      if (samples > 0) {
        size_t size = samples * audio->mVorbis.mInfo.channels;
        short* buffer = new short[size];
        short* p = buffer;
        for (int i=0;i < samples; ++i) {
          for(int j=0; j < audio->mVorbis.mInfo.channels; ++j) {
            int v = static_cast<int>(floor(0.5 + pcm[j][i]*32767.0));
            if (v > 32767) v = 32767;
            if (v <-32768) v = -32768;
            *p++ = v;
          }
        }
        AudioSample* sample = new AudioSample(buffer, (int)size, samples);
        mAudioSamples.push_back(sample);
      }

      ret = vorbis_synthesis_read(&audio->mVorbis.mDsp, samples);
      assert(ret == 0);
    }
    
    if (packet.granulepos != -1) {
      AudioSample* sample = mAudioSamples.back();
      sample->mGranulepos = packet.granulepos;
    }

    // Attempt to read another packet.
    ret = ogg_stream_packetout(&audio->mState, &packet);

  } while (ret == 1);
  
  // Reverse iterate over the newly decoded samples to assign timestamps to them.
  assert(mAudioSamples.back()->mGranulepos != 0);
  list<AudioSample*>::reverse_iterator ritr = mAudioSamples.rbegin();
  AudioSample* back = mAudioSamples.back();
  ogg_int64_t prev = back->mGranulepos - back->mSamples;
  
  ritr++; // Skip the last entry in the list, it should have a granulepos.
  while (ritr != mAudioSamples.rend()) {
    AudioSample* sample = *ritr;
    assert(sample->mGranulepos == 0 || sample->mGranulepos == prev);
    sample->mGranulepos = prev;
    prev -= sample->mSamples;
    ritr++;
  }

  // All packets in the page should be decoded.  
  assert((ret = ogg_stream_packetout(&audio->mState, &packet)) != 1);

  // Return the front sample.
  AudioSample* sample = mAudioSamples.front();
  mAudioSamples.pop_front();
  return sample;
}

double OggDecoder::get_audio_position(OggStream* audio)
{
  ogg_int64_t position = 0;
  sa_position_t positionType = SA_POSITION_WRITE_SOFTWARE;
#if defined(WIN32)
  positionType = SA_POSITION_WRITE_HARDWARE;
#endif	
  int ret = sa_stream_get_position(mAudio, positionType, &position);
  assert(ret == SA_SUCCESS);
  double audio_time = 
    double(position) /
    double(audio->mVorbis.mInfo.rate) /
    double(audio->mVorbis.mInfo.channels) /
    sizeof(short);
  return audio_time;
}

bool OggDecoder::peek_theora_packet(istream& is,
                                    ogg_sync_state* state,
                                    OggStream* video,
                                    ogg_packet* packet)
{
  bool buffered = false;
  bool success = read_theora_packet(is, state, video, packet, buffered);
  if (!success)
    return false;
  if (buffered) {
    ogg_packet* p = new ogg_packet();
    memcpy(p, packet, sizeof(ogg_packet));
    mVideoPackets.insert(mVideoPackets.begin(), p);
  } else {
    mVideoPackets.insert(mVideoPackets.begin(), clone(packet));
  }
  assert(packet->granulepos > 0);
  return true;
}

bool OggDecoder::read_theora_packet(istream& is,
                                    ogg_sync_state* state,
                                    OggStream* video,
                                    ogg_packet* packet,
                                    bool& used_buffered_packet)
{
  used_buffered_packet = false;
  if (mVideoPackets.size() > 0) {
    // We have a buffered packet, return that.
    ogg_packet* clone = mVideoPackets.front();
    mVideoPackets.pop_front();
    memcpy(packet, clone, sizeof(ogg_packet));
    free(clone);
    used_buffered_packet = true;
    assert(packet->granulepos > 0);
    mGranulepos = packet->granulepos;
    return true;
  }
  
  if (!read_packet(is, state, video, packet)) {
    return false;
  }

  if (packet->granulepos > 0) {
    // Packet already has valid granulepos.
    mGranulepos = packet->granulepos;
    return true;
  }
  
  // We shouldn't get a header packet here, we should have -1 granulepos.
  assert(packet->granulepos == -1);
  if (mGranulepos != -1) {
    // Packet's granulepos is the previous packet's granulepos incremented.
    if (th_packet_iskeyframe(packet)) {
      packet->granulepos =
        th_granule_frame(&video->mTheora.mCtx, mGranulepos) + 1;
    } else {
      packet->granulepos = mGranulepos + 1;
    }
    mGranulepos = packet->granulepos;    
    return true;
  }

  // If a packet does not have a granulepos, we need to calculate it.
  // We don't know the granulepos of the previous packet (we probably just
  // seeked) so read packets until we get one with a granulepos, and use
  // that to determine the stored packets' granulepos.
  assert(mVideoPackets.size() == 0);
  // Read and store packets until we find one with non -1 granulepos.
  mVideoPackets.push_back(clone(packet));
  while (packet->granulepos == -1) {
    if (!read_packet(is, state, video, packet))
      break;
    mVideoPackets.push_back(clone(packet));
  }

  // We have a packet with a granulepos. Label the stored packets
  // with granulepos relative to the known granulepos.
  list<ogg_packet*>::reverse_iterator rev_itr = mVideoPackets.rbegin();
  ogg_packet* prev = *rev_itr;
  rev_itr++;
  int shift = video->mTheora.mInfo.keyframe_granule_shift;
  while (rev_itr != mVideoPackets.rend()) {
    ogg_packet* op = *rev_itr;
    assert(op->granulepos == -1);
    assert(prev->granulepos != -1);
    if (th_packet_iskeyframe(op)) {
      op->granulepos =
        (th_granule_frame(&video->mTheora.mCtx, prev->granulepos) - 1) << shift;
    } else {
      op->granulepos = prev->granulepos - 1;
    }
    assert(th_granule_frame(&video->mTheora.mCtx, prev->granulepos) ==
           th_granule_frame(&video->mTheora.mCtx, op->granulepos) + 1);
    prev = op;
    rev_itr++;
  }

  // Now return the first buffered packet.
  ogg_packet* clone = mVideoPackets.front();
  mVideoPackets.pop_front();
  memcpy(packet, clone, sizeof(ogg_packet));
  free(clone);
  used_buffered_packet = true;
  assert(packet->granulepos > 0);
  mGranulepos = packet->granulepos;
  return true;
}

double OggDecoder::get_audio_start_time(istream& is,
                                        ogg_sync_state* state,
                                        OggStream* audio,
                                        bool play_audio)
{
  AudioSample* first = decode_audio(state, is, audio);
  ogg_int64_t granulepos = first->mGranulepos - first->mSamples;
  double start_time = audio->GranuleTime(granulepos);
  mAudioSamples.push_front(first);
  return start_time;
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
    else if (!audio && stream->mType == TYPE_VORBIS) {
      audio = stream;
      audio->mVorbis.initForData(audio);
    }
    else
      stream->mActive = false;
  }

  // Initialize the duration and length data.
  get_end_time(is, &state);

  assert(audio);

  if (video) {
    cout << "Video stream is " 
      << video->mSerial << " "
      << video->mTheora.mInfo.frame_width << "x" << video->mTheora.mInfo.frame_height
      << endl;
  }

  cout << "Audio stream is " 
    << audio->mSerial << " "
    << audio->mVorbis.mInfo.channels << " channels "
    << audio->mVorbis.mInfo.rate << "KHz"
    << endl;

  open_audio(audio->mVorbis.mInfo.rate, audio->mVorbis.mInfo.channels);

  // Read audio packets, sending audio data to the sound hardware.
  // When it's time to display a frame, decode the frame and display it.
  bool need_audio_time = true;
  double theora_frame_duration = (double)video->mTheora.mInfo.fps_denominator /
                                 (double)video->mTheora.mInfo.fps_numerator;
  mGranulepos = -1;                                 
  while (true) {
  
    if (mSeekTime != -1) {
      close_audio();
      seek(&state, is, audio, video);
      
      // Get the first theora packet, and determine it's keyframe offset,
      // and seek again to its keyframe.
      ogg_packet packet;
      bool release_packet = false;
      if (!peek_theora_packet(is, &state, video, &packet)) {
        break;
      }
      if (!th_packet_iskeyframe(&packet)) {
        int shift = video->mTheora.mInfo.keyframe_granule_shift;
        ogg_int64_t keyframe_granulepos = (packet.granulepos >> shift) << shift;
        mSeekTime = th_granule_time(video->mTheora.mCtx, keyframe_granulepos) - theora_frame_duration;
        cout << "Seeking to keyframe at " << mSeekTime << endl;
        seek(&state, is, audio, video);
      }

      mPlaybackStartTime = -1;
      mCurrentTime = 0;
      need_audio_time = true;
      open_audio(audio->mVorbis.mInfo.rate, audio->mVorbis.mInfo.channels);
      
      // Decode audio forward to the seek target.
      while (1) {
        AudioSample* sample = decode_audio(&state, is, audio);
        if (!sample)
          break;
        double end = audio->GranuleTime(sample->mGranulepos);
        if (end > mSeekTime) {
          // This sample is the first which finishes after the seek target,
          // so it must start at or before the target. We start playback here.
          mAudioSamples.push_front(sample);
          break;
        }
        delete sample;
      }      

      // Decode video forward to the seek target.
      while (1) {

        // See if we've got any buffered packets that we need to display.
        ogg_packet packet;
        bool release_packet = false;
        if (!peek_theora_packet(is, &state, video, &packet)) {
          break;
        }

        // See if this packet is before the seek target. We must round here
        // to ensure we don't stop just before a keyframe due to a floating
        // point error.
        double video_time = th_granule_time(video->mTheora.mCtx, packet.granulepos);
        if (s_to_ms(video_time) > s_to_ms(mSeekTime)) {
          break;
        }
  
        // This frame is before the target frame, decode it, and discard it.
        if (!read_theora_packet(is, &state, video, &packet, release_packet)) {
          break;
        }

        th_ycbcr_buffer buffer;
        decode_theora(video, &packet, buffer);
        if (release_packet) {
          delete packet.packet;
          packet.packet = 0;
        }
      }
      mSeekTime = -1;
    }
    
    if (need_audio_time) {
      double audio_start_time = get_audio_start_time(is, &state, audio, true);
      mPlaybackStartTime = audio_start_time;
      cout << "Set playback start time to " << mPlaybackStartTime << endl;

      if (mStartTime == -1) {
        mStartTime = mPlaybackStartTime;
        cout << "Video goes from " << mStartTime << "s to "
             << mEndTime << "s" << endl;
      }
      need_audio_time = false;
    }

    AudioSample* sample = decode_audio(&state, is, audio);
    if (!sample)
      break;
    
    play_audio(sample);
    delete sample;

    // At this point we've written some audio data to the sound
    // system. Now we check to see if it's time to display a video
    // frame.
    //
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
    
      assert(mPlaybackStartTime != -1);
      
      double audio_time = get_audio_position(audio);
      double video_time = th_granule_time(video->mTheora.mCtx, mGranulepos);
      mCurrentTime = audio_time + mPlaybackStartTime;

      if (s_to_ms(mCurrentTime) > s_to_ms(video_time)) {
        // Decode one frame and display it. If no frame is available we
        // don't do anything.
        
        // See if we've got any buffered packets that we need to display.
        ogg_packet packet;
        bool release_packet = false;
        if (!read_theora_packet(is, &state, video, &packet, release_packet)) {
          break;
        }

        th_ycbcr_buffer buffer;
        bool have_frame = decode_theora(video, &packet, buffer) == 0;
        if (release_packet) {
          delete packet.packet;
          packet.packet = 0;
        }
        video_time = th_granule_time(video->mTheora.mCtx, mGranulepos);
        if (have_frame) {
          draw_theora(buffer);
        }
      }
    }

    // Check for SDL events to exit
    SDL_Event event;
    if (SDL_PollEvent(&event) == 1) {
    
      if (mProgressBar) {
        mProgressBar->handle(event);
      }
    
      if ((event.type == SDL_KEYDOWN &&
        event.key.keysym.sym == SDLK_ESCAPE) ||
        event.type == SDL_QUIT)
        break;

      if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
          case SDLK_SPACE:
            SDL_WM_ToggleFullScreen(mSurface);
            break;
          case SDLK_HOME:
            seek(0);
            break;
          default:
            break;
        }
      }
    }
  }

  // Cleanup
  ret = ogg_sync_clear(&state);
  assert(ret == 0);
}

bool OggDecoder::handle_skeleton_header(OggStream* stream, ogg_packet* packet) {
  // Is it a "fishead" skeleton identifier packet?
  if (packet->bytes > 8 && memcmp(packet->packet, "fishead", 8) == 0) {
    stream->mType = TYPE_SKELETON;
    return false;
  }
  
  if (stream->mType != TYPE_SKELETON) {
    // The first packet must be the skeleton identifier.
    return false;
  }
  
  // "fisbone" stream info packet?
  if (packet->bytes >= 8 && memcmp(packet->packet, "fisbone", 8) == 0) {
    return false;
  }
  
  // "index" keyframe index packet?
  if (packet->bytes > 6 && memcmp(packet->packet, "index", 6) == 0) {
    return false;
  }
  
  if (packet->e_o_s) {
    return false;
  }
  
  // Shouldn't actually get here.
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

int OggDecoder::decode_theora(OggStream* stream,
                              ogg_packet* packet,
                              th_ycbcr_buffer buffer)
{
  // The granulepos for a packet gives the time of the end of the
  // display interval of the frame in the packet.  We keep the
  // granulepos of the frame we've decoded and use this to know the
  // time when to display the next frame.
  int ret = th_decode_packetin(stream->mTheora.mCtx,
                               packet,
                               0);
  assert(ret == 0 || ret == TH_DUPFRAME);

  // If the return code is TH_DUPFRAME then we don't need to
  // get the YUV data and display it since it's the same as
  // the previous frame.
  if (ret == TH_DUPFRAME) {
    return TH_DUPFRAME;
  }

  // We have a frame. Get the YUV data
  ret = th_decode_ycbcr_out(stream->mTheora.mCtx, buffer);
  assert(ret == 0);

  // Remember last granulepos decoded.
  mGranulepos = packet->granulepos;

  return ret;
}

void OggDecoder::draw_theora(th_ycbcr_buffer buffer)
{
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
  
  // Draw the progress bar.
  if (!mProgressBar) {
    mProgressBar = new ProgressBar(mSurface,
                                   this,
                                   mStartTime,
                                   mEndTime,
                                   5, 15, 10, 1);
    assert(mProgressBar);
  }
  mProgressBar->draw(mCurrentTime);
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

int main(int argc, char* argv[]) {
  if (argc != 2) { 
    usage();
    return 0;
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
  SDL_Quit();
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
