/// @file
/// @version 3.4
/// 
/// @section LICENSE
/// 
/// This program is free software; you can redistribute it and/or modify it under
/// the terms of the BSD license: http://opensource.org/licenses/BSD-3-Clause

#ifdef _OPENSLES
#include <stdio.h>
#include <string.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <hltypes/hmutex.h>
#include <hltypes/hlog.h>
#include <hltypes/hplatform.h>
#include <hltypes/hstring.h>

#include "AudioManager.h"
#include "Buffer.h"
#include "Category.h"
#include "OpenSLES_AudioManager.h"
#include "OpenSLES_Player.h"
#include "Sound.h"
#include "xal.h"

#define NORMAL_BUFFER_COUNT 2

#define OPENSLES_MANAGER ((OpenSLES_AudioManager*)xal::manager)

// this is just here to improve readability of code
#define CHECK_ERROR(message) \
	if (result != SL_RESULT_SUCCESS) \
	{ \
		hlog::error(xal::logTag, message); \
		this->player = NULL; \
		this->playerVolume = NULL; \
		this->playerBufferQueue = NULL; \
		if (this->playerObject != NULL) \
		{ \
			__CPP_WRAP(this->playerObject, Destroy); \
			this->playerObject = NULL; \
		} \
		return false; \
	}

namespace xal
{
	static const SLInterfaceID ids[] = { SL_IID_VOLUME, SL_IID_ANDROIDSIMPLEBUFFERQUEUE };
	static const SLboolean reqs[] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };
	static const int reqsSize = 2;

	void OpenSLES_Player::_playCallback(SLPlayItf player, void* context, SLuint32 event)
	{
		if (event & SL_PLAYEVENT_HEADATEND)
		{
			OpenSLES_Player* player = (OpenSLES_Player*)context;
			if (!player->sound->isStreamed())
			{
				player->active = false;
			}
		}
	}

	OpenSLES_Player::OpenSLES_Player(Sound* sound) : Player(sound), playing(false), active(false), stillPlaying(false),
		playerObject(NULL), player(NULL), playerVolume(NULL), playerBufferQueue(NULL)
	{
		for_iter (i, 0, STREAM_BUFFER_COUNT)
		{
			this->streamBuffers[i] = NULL;
		}
		if (this->sound->isStreamed())
		{
			for_iter (i, 0, STREAM_BUFFER_COUNT)
			{
				this->streamBuffers[i] = new unsigned char[STREAM_BUFFER_SIZE];
			}
		}
	}
	
	OpenSLES_Player::~OpenSLES_Player()
	{
		if (this->playerObject != NULL)
		{
			__CPP_WRAP(this->playerObject, Destroy);
		}
		for_iter (i, 0, STREAM_BUFFER_COUNT)
		{
			if (this->streamBuffers[i] != NULL)
			{
				delete[] this->streamBuffers[i];
				this->streamBuffers[i] = NULL;
			}
		}
	}
	
	void OpenSLES_Player::_update(float timeDelta)
	{
		this->stillPlaying = this->active;
		Player::_update(timeDelta);
		if (!this->stillPlaying && this->playing)
		{
			this->_stop();
		}
	}

	bool OpenSLES_Player::_systemIsPlaying()
	{
		return this->playing;
	}

	unsigned int OpenSLES_Player::_systemGetBufferPosition()
	{
		int bytes = 0;
		SLmillisecond milliseconds = 0;
		SLresult result = __CPP_WRAP_ARGS(this->player, GetPosition, (SLmillisecond*)&milliseconds);
		if (result == SL_RESULT_SUCCESS)
		{
			// first comes "* 0.001", because it can cause an int overflow otherwise
			bytes = (int)(milliseconds * 0.001f * this->buffer->getSamplingRate() * (this->buffer->getBitsPerSample() / 8) * this->buffer->getChannels());
			if (!this->sound->isStreamed() && this->looping)
			{
				bytes %= this->buffer->getSize();
			}
		}
		return bytes;
	}

	bool OpenSLES_Player::_systemNeedsStreamedBufferPositionCorrection()
	{
		return false;
	}
	
	bool OpenSLES_Player::_systemPreparePlay()
	{
		if (this->playerObject != NULL)
		{
			return true;
		}
		// input / source
		SLDataLocator_AndroidSimpleBufferQueue inLocator;
		inLocator.locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE;
		inLocator.numBuffers = (!this->sound->isStreamed() ? NORMAL_BUFFER_COUNT : STREAM_BUFFER_COUNT);
		SLDataFormat_PCM format;
		format.formatType = SL_DATAFORMAT_PCM;
		format.numChannels = this->buffer->getChannels();
		format.samplesPerSec = this->buffer->getSamplingRate() * 1000; // in mHz, this parameter is misnamed for some reason
		int bitsPerSample = this->buffer->getBitsPerSample();
		if (bitsPerSample == 8)
		{
			format.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_8;
		}
		else if (bitsPerSample == 16)
		{
			format.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
		}
		else if (bitsPerSample == 20)
		{
			format.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_20;
		}
		else if (bitsPerSample == 24)
		{
			format.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_24;
		}
		else if (bitsPerSample == 28)
		{
			format.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_28;
		}
		else if (bitsPerSample == 32)
		{
			format.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_32;
		}
		else
		{
			return false;
		}
		format.containerSize = (bitsPerSample + 7) / 8 * 8; // assuming all bits-per-sample formats are byte-aligned which may not be the right way to do things
		if (format.numChannels == 1)
		{
			format.channelMask = SL_SPEAKER_FRONT_CENTER;
		}
		else
		{
			format.channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
		}
		format.endianness = SL_BYTEORDER_LITTLEENDIAN;
		SLDataSource source;
		source.pLocator = &inLocator;
		source.pFormat = &format;
		// output / destination
		SLDataLocator_OutputMix outLocator;
		outLocator.locatorType = SL_DATALOCATOR_OUTPUTMIX;
		outLocator.outputMix = OPENSLES_MANAGER->outputMixObject;
		SLDataSink destination;
		destination.pLocator = &outLocator;
		destination.pFormat = NULL;
		// create player
		SLresult result;
		result = __CPP_WRAP_ARGS(OPENSLES_MANAGER->engine, CreateAudioPlayer, &this->playerObject, &source, &destination, reqsSize, ids, reqs);
		CHECK_ERROR("Could not create player object!");
		result = __CPP_WRAP_ARGS(this->playerObject, Realize, SL_BOOLEAN_FALSE);
		CHECK_ERROR("Could not realize player object!");
		result = __CPP_WRAP_ARGS(this->playerObject, GetInterface, SL_IID_PLAY, &this->player);
		CHECK_ERROR("Could not get player play interface!");
		result = __CPP_WRAP_ARGS(this->playerObject, GetInterface, SL_IID_VOLUME, &this->playerVolume);
		CHECK_ERROR("Could not get player volume interface!");
		result = __CPP_WRAP_ARGS(this->playerObject, GetInterface, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &this->playerBufferQueue);
		CHECK_ERROR("Could not get player buffer queue interface!");
		result = __CPP_WRAP_ARGS(this->player, RegisterCallback, &OpenSLES_Player::_playCallback, this);
		CHECK_ERROR("Could not register callback!");
		result = __CPP_WRAP_ARGS(this->player, SetCallbackEventsMask, SL_PLAYEVENT_HEADATEND);
		CHECK_ERROR("Could not set callback mask!");
		return true;
	}
	
	void OpenSLES_Player::_systemPrepareBuffer()
	{
		if (!this->sound->isStreamed())
		{
			if (!this->looping)
			{
				if (!this->paused)
				{
					this->_submitBuffer(this->buffer->getStream());
				}
				return;
			}
			int count = NORMAL_BUFFER_COUNT;
			if (this->paused)
			{
				count -= this->buffersSubmitted;
			}
			else
			{
				this->buffersSubmitted = 0;
			}
			for_iter (i, 0, count)
			{
				this->_submitBuffer(this->buffer->getStream());
			}
			return;
		}
		int count = STREAM_BUFFER_COUNT;
		if (this->paused)
		{
			count -= this->buffersSubmitted;
		}
		else
		{
			this->buffersSubmitted = 0;
		}
		if (count > 0)
		{
			count = this->_fillStreamBuffers(count);
			if (count > 0)
			{
				this->_submitStreamBuffers(count);
			}
		}
	}
	
	void OpenSLES_Player::_systemUpdateGain()
	{
		if (this->playerVolume != NULL)
		{
			float gain = this->_calcGain();
			SLmillibel value = -9600; // minimum possible attenuation/volume in mB
			if (gain > 0.01f)
			{
				value = (SLmillibel)(log10(gain) * 2000);
			}
			__CPP_WRAP_ARGS(this->playerVolume, SetVolumeLevel, value);
		}
	}
	
	void OpenSLES_Player::_systemUpdatePitch()
	{
		static bool _supported = true;
		if (_supported)
		{
			hlog::warn(xal::logTag, "Pitch change is not supported in this implementation! This message is only logged once.");
			_supported = false;
		}
		// even though there is no crash, it doesn't seem possible to play a sound when obtaining a playback interface so this code is disabled for now
		/*
		if (this->playerPlaybackRate != NULL)
		{
			static bool _supported = true;
			if (_supported)
			{
				SLpermille rateMin = 0;
				SLpermille rateMax = 0x7FFF; // max short
				SLpermille rateStep = 0;
				SLuint32 capabilities = 0;
				SLresult result = __CPP_WRAP_ARGS(this->playerPlaybackRate, GetRateRange, 0, &rateMin, &rateMax, &rateStep, &capabilities);
				if (result != SL_RESULT_SUCCESS)
				{
					hlog::warn(xal::logTag, "Pitch change is not supported on this device! This message is only logged once.");
					_supported = false;
					return;
				}
				SLpermille value = (SLpermille)hclamp(this->pitch * 1000, (float)rateMin, (float)rateMax);
				value = ((value - rateMin) / rateStep) * rateStep + rateMin; // correcting value to use "step" properly
				result = __CPP_WRAP_ARGS(this->playerPlaybackRate, SetRate, value);
				if (result != SL_RESULT_SUCCESS)
				{
					hlog::warn(xal::logTag, "Pitch change is not supported on this device! This message is only logged once.");
					_supported = false;
				}
			}
		}
		//*/
	}
	
	void OpenSLES_Player::_systemPlay()
	{
		SLresult result = __CPP_WRAP_ARGS(this->player, SetPlayState, SL_PLAYSTATE_PLAYING);
		if (result == SL_RESULT_SUCCESS)
		{
			this->playing = true;
			this->stillPlaying = true;
			this->active = true; // required, because otherwise the buffer will think it's done
		}
		else
		{
			hlog::warn(xal::logTag, "Could not start: " + this->sound->getFilename());
		}
	}
	
	int OpenSLES_Player::_systemStop()
	{
		if (this->playing)
		{
			SLresult result = __CPP_WRAP_ARGS(this->player, SetPlayState, this->paused ? SL_PLAYSTATE_PAUSED : SL_PLAYSTATE_STOPPED);
			if (result == SL_RESULT_SUCCESS)
			{
				if (this->paused)
				{
					this->buffersSubmitted -= this->_getProcessedBuffersCount();
				}
				else
				{
					this->bufferIndex = 0;
					this->buffer->rewind();
					__CPP_WRAP(this->playerBufferQueue, Clear);
					this->buffersSubmitted = 0;
				}
				this->playing = false;
				this->stillPlaying = false;
			}
			else
			{
				hlog::warn(xal::logTag, "Could not stop: " + this->sound->getFilename());
			}
		}
		return 0;
	}
	
	void OpenSLES_Player::_systemUpdateNormal()
	{
		if (this->looping)
		{
			int processed = this->_getProcessedBuffersCount();
			this->buffersSubmitted -= processed;
			for_iter (i, 0, processed)
			{
				this->_submitBuffer(this->buffer->getStream());
			}
			this->stillPlaying = true; // in case underrun happened, sound is regarded as stopped so let's just bitch-slap it and get this over with
			if (this->buffersSubmitted == 0)
			{
				this->_stop();
			}
		}
	}

	int OpenSLES_Player::_systemUpdateStream()
	{
		int processed = this->_getProcessedBuffersCount();
		if (processed == 0)
		{
			this->stillPlaying = true; // don't remove, it prevents streamed sounds from being stopped
			return 0;
		}
		this->buffersSubmitted -= processed;
		int count = this->_fillStreamBuffers(processed);
		if (count > 0)
		{
			this->_submitStreamBuffers(count);
			this->stillPlaying = true; // in case underrun happened, sound is regarded as stopped so let's just bitch-slap it and get this over with
		}
		if (this->buffersSubmitted == 0)
		{
			this->_stop();
		}
		return 0;
	}

	void OpenSLES_Player::_submitBuffer(hstream& stream)
	{
		SLresult result = __CPP_WRAP_ARGS(this->playerBufferQueue, Enqueue, (unsigned char*)stream, (int)stream.size());
		if (result == SL_RESULT_SUCCESS)
		{
			++this->buffersSubmitted;
		}
		else
		{
			hlog::warn(xal::logTag, "Could not queue buffer!");
		}
	}

	int OpenSLES_Player::_fillStreamBuffers(int count)
	{
		int size = this->buffer->load(this->looping, count * STREAM_BUFFER_SIZE);
		int filled = (size + STREAM_BUFFER_SIZE - 1) / STREAM_BUFFER_SIZE;
		hstream& stream = this->buffer->getStream();
		int currentSize = 0;
		for_iter (i, 0, filled)
		{
			currentSize = hmin(size, STREAM_BUFFER_SIZE);
			memcpy(this->streamBuffers[this->bufferIndex], &stream[i * STREAM_BUFFER_SIZE], currentSize);
			if (currentSize < STREAM_BUFFER_SIZE)
			{
				memset(&this->streamBuffers[this->bufferIndex][currentSize], 0, STREAM_BUFFER_SIZE - currentSize);
			}
			this->bufferIndex = (this->bufferIndex + 1) % STREAM_BUFFER_COUNT;
			size -= STREAM_BUFFER_SIZE;
		}
		return filled;
	}

	void OpenSLES_Player::_submitStreamBuffers(int count)
	{
		int queued = 0;
		int index = (this->bufferIndex + STREAM_BUFFER_COUNT - count) % STREAM_BUFFER_COUNT;
		for_iter (i, 0, count)
		{
			SLresult result = __CPP_WRAP_ARGS(this->playerBufferQueue, Enqueue, this->streamBuffers[index], STREAM_BUFFER_SIZE);
			if (result != SL_RESULT_SUCCESS)
			{
				hlog::warn(xal::logTag, "Could not queue streamed buffer!");
				break;
			}
			++queued;
			index = (index + 1) % STREAM_BUFFER_COUNT;
		}
		this->buffersSubmitted += queued;
	}

	int OpenSLES_Player::_getProcessedBuffersCount()
	{
		SLresult result = __CPP_WRAP_ARGS(this->playerBufferQueue, GetState, &this->playerBufferQueueState);
		if (result != SL_RESULT_SUCCESS)
		{
			return 0;
		}
		return (this->buffersSubmitted - this->playerBufferQueueState.count);
	}

}
#endif
