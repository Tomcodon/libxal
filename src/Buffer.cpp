/// @file
/// @version 3.3
/// 
/// @section LICENSE
/// 
/// This program is free software; you can redistribute it and/or modify it under
/// the terms of the BSD license: http://opensource.org/licenses/BSD-3-Clause

#include <string.h> // required on Unix because of memset usage

#include <hltypes/harray.h>
#include <hltypes/hlog.h>
#include <hltypes/hresource.h>
#include <hltypes/hstring.h>

#include "AudioManager.h"
#include "Buffer.h"
#include "BufferAsync.h"
#include "Category.h"
#include "Sound.h"
#include "Source.h"
#include "xal.h"

namespace xal
{
	Buffer::Buffer(Sound* sound)
	{
		this->filename = sound->getRealFilename();
		this->fileSize = hresource::hsize(this->filename);
		Category* category = sound->getCategory();
		this->mode = category->getBufferMode();
		this->loaded = false;
		this->asyncLoadQueued = false;
		this->asyncLoadDiscarded = false;
		this->dataSize = 0;
		this->source = xal::mgr->_createSource(this->filename, category->getSourceMode(), this->mode, this->getFormat());
		this->loadedMetaData = false;
		this->size = 0;
		this->channels = 2;
		this->samplingRate = 44100;
		this->bitsPerSample = 16;
		this->duration = 0.0f;
		this->idleTime = 0.0f;
		if (xal::mgr->isEnabled() && this->getFormat() != UNKNOWN)
		{
			switch (this->mode)
			{
			case FULL:
				this->prepare();
				break;
			case ASYNC:
				this->prepareAsync();
				break;
			case LAZY:
				break;
			case MANAGED:
				break;
			case ON_DEMAND:
				break;
			case STREAMED:
				break;
			default:
				break;
			}
		}
	}

	Buffer::~Buffer()
	{
		this->asyncLoadMutex.lock();
		this->asyncLoadQueued = false;
		this->asyncLoadDiscarded = false;
		this->loaded = false;
		this->asyncLoadMutex.unlock();
		delete this->source;
	}
	
	int Buffer::getSize()
	{
		this->asyncLoadMutex.lock();
		this->_tryLoadMetaData();
		this->asyncLoadMutex.unlock();
		return this->size;
	}

	int Buffer::getChannels()
	{
		this->asyncLoadMutex.lock();
		this->_tryLoadMetaData();
		this->asyncLoadMutex.unlock();
		return this->channels;
	}

	int Buffer::getSamplingRate()
	{
		this->asyncLoadMutex.lock();
		this->_tryLoadMetaData();
		this->asyncLoadMutex.unlock();
		return this->samplingRate;
	}

	int Buffer::getBitsPerSample()
	{
		this->asyncLoadMutex.lock();
		this->_tryLoadMetaData();
		this->asyncLoadMutex.unlock();
		return this->bitsPerSample;
	}

	float Buffer::getDuration()
	{
		this->asyncLoadMutex.lock();
		this->_tryLoadMetaData();
		this->asyncLoadMutex.unlock();
		return this->duration;
	}

	Format Buffer::getFormat()
	{
#ifdef _FORMAT_FLAC
		if (this->filename.ends_with(".flac"))
		{
			return FLAC;
		}
#endif
#ifdef _FORMAT_M4A
		if (this->filename.ends_with(".m4a"))
		{
			return M4A;
		}
#endif
#ifdef _FORMAT_OGG
		if (this->filename.ends_with(".ogg"))
		{
			return OGG;
		}
#endif
#ifdef _FORMAT_SPX
		if (this->filename.ends_with(".spx"))
		{
			return SPX;
		}
#endif
#ifdef _FORMAT_WAV
		if (this->filename.ends_with(".wav"))
		{
			return WAV;
		}
#endif
		return UNKNOWN;
	}

	bool Buffer::isLoaded()
	{
		this->asyncLoadMutex.lock();
		bool result = this->loaded;
		this->asyncLoadMutex.unlock();
		return result;
	}

	bool Buffer::isAsyncLoadQueued()
	{
		this->asyncLoadMutex.lock();
		bool result = this->asyncLoadQueued;
		this->asyncLoadMutex.unlock();
		return result;
	}

	bool Buffer::isStreamed()
	{
		return (this->mode == STREAMED);
	}

	bool Buffer::isMemoryManaged()
	{
		return (this->mode == MANAGED);
	}

	void Buffer::prepare()
	{
		this->asyncLoadMutex.lock();
		this->asyncLoadDiscarded = false; // a possible previous unload call must be canceled
		if (!xal::mgr->isEnabled() || this->loaded)
		{
			this->asyncLoadQueued = false;
			this->loaded = true;
			this->asyncLoadMutex.unlock();
			return;
		}
		if (this->asyncLoadQueued)
		{
			if (!this->loaded)
			{
				this->asyncLoadMutex.unlock();
				this->_waitForAsyncLoad();
				this->asyncLoadMutex.lock();
			}
			this->asyncLoadMutex.unlock();
			return;
		}
		if (!this->isStreamed())
		{
			this->loaded = true;
			this->source->open();
			this->dataSize = this->source->getSize();
			this->stream.clear(this->dataSize);
			this->source->load(this->stream);
			this->source->close();
			this->dataSize = xal::mgr->_convertStream(this->source, this->stream, this->dataSize);
			this->asyncLoadMutex.unlock();
			return;
		}
		this->asyncLoadMutex.unlock();
		// streamed sounds cannot be loaded asynchronously and hence require no mutex locking
		if (!this->source->isOpen())
		{
			this->source->open();
			this->_tryLoadMetaData();
		}
	}

	bool Buffer::prepareAsync()
	{
		this->asyncLoadMutex.lock();
		if (!xal::mgr->isEnabled() || this->loaded)
		{
			this->loaded = true;
			this->asyncLoadMutex.unlock();
			return false;
		}
		if (this->isStreamed())
		{
			hlog::warn(xal::logTag, "Streamed sound cannot be loaded asynchronously: " + this->getFilename());
			this->asyncLoadMutex.unlock();
			return false;
		}
		this->asyncLoadDiscarded = false;
		if (!this->asyncLoadQueued) // this check is down here to allow the upper error messages to be displayed
		{
			this->asyncLoadQueued = BufferAsync::queueLoad(this);
		}
		bool result = this->asyncLoadQueued; // this->asyncLoadQueued CAN change between the two lines below and cause problems hence this temporary result variable
		this->asyncLoadMutex.unlock();
		return result;
	}

	int Buffer::load(bool looping, int size)
	{
		this->keepLoaded();
		if (!xal::mgr->isEnabled())
		{
			return 0;
		}
		this->asyncLoadMutex.lock();
		if (this->isStreamed() && this->source->isOpen())
		{
			this->dataSize = STREAM_BUFFER;
			this->stream.clear(this->dataSize);
			this->dataSize = this->source->loadChunk(this->stream, size);
			size -= this->dataSize;
			if (size > 0)
			{
				this->stream.seek(this->dataSize);
				if (!looping)
				{
					// fill rest of buffer with silence so systems that depends on buffered chunks don't get messed up
					this->stream.fill(0, size);
				}
				else
				{
					int read = 0;
					while (size > 0)
					{
						this->source->rewind();
						read = this->source->loadChunk(this->stream, size);
						size -= read;
						this->dataSize += read;
						this->stream.seek(read);
					}
				}
				this->stream.seek(0, hstream::START);
			}
			this->dataSize = xal::mgr->_convertStream(this->source, this->stream, this->dataSize);
		}
		this->asyncLoadMutex.unlock();
		return this->dataSize;
	}

	void Buffer::bind(Player* player, bool playerPaused)
	{
		this->boundPlayers |= player;
	}

	void Buffer::unbind(Player* player, bool playerPaused)
	{
		if (!playerPaused)
		{
			this->boundPlayers /= player;
		}
		this->asyncLoadMutex.lock();
		if (this->boundPlayers.size() == 0 && this->mode == xal::ON_DEMAND || this->mode == xal::STREAMED)
		{
			this->stream.clear(1L);
			this->asyncLoadQueued = false;
			this->asyncLoadDiscarded = true;
			this->loaded = false;
		}
		if (this->boundPlayers.size() == 0 && this->mode == xal::STREAMED)
		{
			this->source->close();
			this->asyncLoadQueued = false;
			this->asyncLoadDiscarded = true;
			this->loaded = false;
		}
		this->asyncLoadMutex.unlock();
	}

	void Buffer::keepLoaded()
	{
		this->idleTime = 0.0f;
	}

	void Buffer::rewind()
	{
		this->source->rewind();
	}

	int Buffer::calcOutputSize(int size)
	{
		return hround((float)size * xal::mgr->getSamplingRate() * xal::mgr->getChannels() * xal::mgr->getBitsPerSample() /
			((float)this->getSamplingRate() * this->getChannels() * this->getBitsPerSample()));
	}

	int Buffer::calcInputSize(int size)
	{
		return hround((float)size * this->getSamplingRate() * this->getChannels() * this->getBitsPerSample() /
			((float)xal::mgr->getSamplingRate() * xal::mgr->getChannels() * xal::mgr->getBitsPerSample()));
	}

	int Buffer::readPcmData(hstream& output)
	{
		// no mutex locking, because a separate source is used
		int result = 0;
		if (this->getFormat() != UNKNOWN)
		{
			Source* source = xal::mgr->_createSource(this->filename, xal::DISK, xal::FULL, this->getFormat());
			source->open();
			result = source->getSize();
			if (result > 0)
			{
				source->load(output);
				result = xal::mgr->_convertStream(source, output, result);
			}
			source->close();
			delete source;
		}
		return result;
	}

	void Buffer::_update(float timeDelta)
	{
		this->idleTime += timeDelta;
		if (this->idleTime >= xal::mgr->getIdlePlayerUnloadTime())
		{
			this->_tryClearMemory();
		}
	}

	void Buffer::_tryLoadMetaData()
	{
		if (!this->loadedMetaData)
		{
			bool open = this->source->isOpen();
			if (!open)
			{
				this->source->open();
			}
			this->size = this->source->getSize();
			this->channels = this->source->getChannels();
			this->samplingRate = this->source->getSamplingRate();
			this->bitsPerSample = this->source->getBitsPerSample();
			this->duration = this->source->getDuration();
			this->loadedMetaData = true;
			if (!open)
			{
				this->source->close();
			}
		}
	}

	bool Buffer::_tryClearMemory()
	{
		bool result = false;
		this->asyncLoadMutex.lock();
		if (this->isMemoryManaged() && this->boundPlayers.size() == 0 && (this->loaded || this->mode == STREAMED))
		{
			hlog::debug(xal::logTag, "Clearing memory for: " + this->filename);
			this->stream.clear(1L);
			this->source->close();
			this->asyncLoadQueued = false;
			this->asyncLoadDiscarded = true;
			this->loaded = false;
			result = true;
		}
		this->asyncLoadMutex.unlock();
		return result;
	}

	bool Buffer::_prepareAsyncStream()
	{
		this->asyncLoadMutex.lock();
		if (!this->asyncLoadQueued || this->asyncLoadDiscarded)
		{
			this->asyncLoadQueued = false;
			this->asyncLoadDiscarded = false;
			this->asyncLoadMutex.unlock();
			return false;
		}
		this->source->open();
		bool result = true;
		if (!this->source->isOpen())
		{
			this->asyncLoadQueued = false;
			this->asyncLoadDiscarded = false;
			result = false;
		}
		this->asyncLoadMutex.unlock();
		return result;
	}

	void Buffer::_decodeFromAsyncStream()
	{
		this->asyncLoadMutex.lock();
		if (!this->asyncLoadQueued || this->asyncLoadDiscarded || this->loaded)
		{
			this->source->close();
			this->asyncLoadQueued = false;
			this->asyncLoadDiscarded = false;
			this->asyncLoadMutex.unlock();
			return;
		}
		this->_tryLoadMetaData();
		this->dataSize = this->source->getSize();
		this->stream.clear(this->dataSize);
		this->source->load(this->stream);
		this->dataSize = xal::mgr->_convertStream(this->source, this->stream, this->dataSize);
		this->source->close();
		this->asyncLoadQueued = false;
		this->asyncLoadDiscarded = false;
		this->loaded = true;
		this->asyncLoadMutex.unlock();
	}

	void Buffer::_waitForAsyncLoad(float timeout)
	{
		BufferAsync::prioritizeLoad(this);
		float time = timeout;
		while (time > 0.0f || timeout <= 0.0f)
		{
			this->asyncLoadMutex.lock();
			if (this->loaded || this->asyncLoadDiscarded)
			{
				if (this->asyncLoadDiscarded)
				{
					this->loaded = false;
				}
				this->asyncLoadQueued = false;
				this->asyncLoadDiscarded = false;
				this->asyncLoadMutex.unlock();
				break;
			}
			this->asyncLoadMutex.unlock();
			hthread::sleep(0.1f);
			time -= 0.0001f;
			BufferAsync::update();
		}
	}

}
