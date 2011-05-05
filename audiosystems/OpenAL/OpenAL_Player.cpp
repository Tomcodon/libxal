/// @file
/// @author  Boris Mikic
/// @version 2.0
/// 
/// @section LICENSE
/// 
/// This program is free software; you can redistribute it and/or modify it under
/// the terms of the BSD license: http://www.opensource.org/licenses/bsd-license.php

#if HAVE_OPENAL
#ifndef __APPLE__
#include <AL/al.h>
#else
#include <OpenAL/al.h>
#endif

#include "AudioManager.h"
#include "Buffer.h"
#include "Category.h"
#include "OpenAL_AudioManager.h"
#include "OpenAL_Player.h"
#include "Sound.h"

namespace xal
{
	OpenAL_Player::OpenAL_Player(Sound* sound, Buffer* buffer) :
		Player(sound, buffer), sourceId(0), bufferIndex(0)
	{
		Category* category = sound->getCategory();
		memset(&this->bufferIds[0], 0, STREAM_BUFFER_COUNT * sizeof(unsigned int));
		alGenBuffers((!this->sound->isStreamed() ? 1 : STREAM_BUFFER_COUNT), &this->bufferIds[0]);
	}

	OpenAL_Player::~OpenAL_Player()
	{
		alDeleteBuffers((!this->sound->isStreamed() ? 1 : STREAM_BUFFER_COUNT), &this->bufferIds[0]);
	}

	void OpenAL_Player::setGain(float gain)
	{
		Player::setGain(gain);
		if (this->sourceId != 0)
		{
			alSourcef(this->sourceId, AL_GAIN, this->_calcGain());
		}
	}

	bool OpenAL_Player::isPlaying()
	{
		if (this->sound->isStreamed())
		{
			int queued;
			alGetSourcei(this->sourceId, AL_BUFFERS_QUEUED, &queued);
			int count;
			alGetSourcei(this->sourceId, AL_BUFFERS_PROCESSED, &count);
			return (queued > 0 || count > 0);
		}
		if (this->isFadingOut())
		{
			return false;
		}
		int state;
		alGetSourcei(this->sourceId, AL_SOURCE_STATE, &state);
		return (state == AL_PLAYING);
	}

	float OpenAL_Player::_sysGetOffset()
	{
		float offset;
		alGetSourcef(this->sourceId, AL_SAMPLE_OFFSET, &offset);
		return offset;
	}

	void OpenAL_Player::_sysSetOffset(float value)
	{
		// TODO - should be int
		alSourcef(this->sourceId, AL_SAMPLE_OFFSET, value);
		//alSourcei(this->sourceId, AL_SAMPLE_OFFSET, value);
	}

	int OpenAL_Player::_sysGetQueuedBuffersCount()
	{
		int queued;
		alGetSourcei(this->sourceId, AL_BUFFERS_QUEUED, &queued);
		return queued;
	}

	int OpenAL_Player::_sysGetProcessedBuffersCount()
	{
		int processed;
		alGetSourcei(this->sourceId, AL_BUFFERS_PROCESSED, &processed);
		return processed;
	}

	bool OpenAL_Player::_sysPreparePlay()
	{
		if (this->sourceId == 0)
		{
			this->sourceId = ((OpenAL_AudioManager*)xal::mgr)->allocateSourceId();
		}
		return (this->sourceId != 0);
	}

	void OpenAL_Player::_sysPrepareBuffer()
	{
		unsigned int format = (this->buffer->getChannels() == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16);
		int samplingRate = this->buffer->getSamplingRate();
		if (!this->sound->isStreamed())
		{
			alSourcei(this->sourceId, AL_BUFFER, this->bufferIds[0]);
			alSourcei(this->sourceId, AL_LOOPING, this->looping);
			alBufferData(this->bufferIds[0], format, this->buffer->getStream(), this->buffer->getSize(), samplingRate);
		}
		else
		{
			alSourcei(this->sourceId, AL_BUFFER, 0);
			alSourcei(this->sourceId, AL_LOOPING, false);
			unsigned char* stream;
			int size;
			for (int i = 0; i < STREAM_BUFFER_COUNT; i++)
			{
				size = this->buffer->getData((int)this->offset, STREAM_BUFFER_SIZE, &stream);
				alBufferData(this->bufferIds[i], format, stream, size, samplingRate);
			}
			this->_sysQueueBuffers(0, STREAM_BUFFER_COUNT);
		}
	}

	void OpenAL_Player::_sysUpdateFadeGain()
	{
		alSourcef(this->sourceId, AL_GAIN, this->_calcFadeGain());
	}

	void OpenAL_Player::_sysPlay()
	{
		alSourcePlay(this->sourceId);
	}

	void OpenAL_Player::_sysStop()
	{
		if (this->sourceId != 0)
		{
			alSourceStop(this->sourceId);
			((OpenAL_AudioManager*)xal::mgr)->releaseSourceId(this->sourceId);
			this->sourceId = 0;
		}
	}

	void OpenAL_Player::_sysQueueBuffers(int index, int count)
	{
		if (index + count <= STREAM_BUFFER_COUNT)
		{
			alSourceQueueBuffers(this->sourceId, count, &this->bufferIds[index]);
		}
		else
		{
			alSourceQueueBuffers(this->sourceId, STREAM_BUFFER_COUNT - index, &this->bufferIds[index]);
			alSourceQueueBuffers(this->sourceId, count + index - STREAM_BUFFER_COUNT, this->bufferIds);
		}
	}
 
	void OpenAL_Player::_sysUnqueueBuffers(int index, int count)
	{
		if (index + count <= STREAM_BUFFER_COUNT)
		{
			alSourceUnqueueBuffers(this->sourceId, count, &this->bufferIds[index]);
		}
		else
		{
			alSourceUnqueueBuffers(this->sourceId, STREAM_BUFFER_COUNT - index, &this->bufferIds[index]);
			alSourceUnqueueBuffers(this->sourceId, count + index - STREAM_BUFFER_COUNT, this->bufferIds);
		}
	}

	void OpenAL_Player::_sysQueueBuffers()
	{
		int queued;
		alGetSourcei(this->sourceId, AL_BUFFERS_QUEUED, &queued);
		this->_sysQueueBuffers(this->bufferIndex, STREAM_BUFFER_COUNT - queued);
	}
 
	void OpenAL_Player::_sysUnqueueBuffers()
	{
		int queued;
		alGetSourcei(this->sourceId, AL_BUFFERS_QUEUED, &queued);
		this->_sysUnqueueBuffers((this->bufferIndex + STREAM_BUFFER_COUNT - queued) % STREAM_BUFFER_COUNT, queued);
	}

	void OpenAL_Player::__sysSetBufferData(int index, unsigned char* data, int size)
	{
		alBufferData(this->bufferIds[index], (this->buffer->getChannels() == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16),
			data, size, this->buffer->getSamplingRate());
	}
 
}
#endif