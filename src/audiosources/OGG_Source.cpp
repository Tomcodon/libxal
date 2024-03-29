/// @file
/// @version 3.4
/// 
/// @section LICENSE
/// 
/// This program is free software; you can redistribute it and/or modify it under
/// the terms of the BSD license: http://opensource.org/licenses/BSD-3-Clause

#ifdef _FORMAT_OGG
#include <stdio.h>
#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

#include <hltypes/hlog.h>
#include <hltypes/hltypesUtil.h>
#include <hltypes/hresource.h>
#include <hltypes/hstream.h>

#include "AudioManager.h"
#include "OGG_Source.h"
#include "xal.h"

namespace xal
{
	// small optimization, it's not thread-safe, but since it's not used anywhere, it doesn't matter
	static int _section = 0;

	static size_t _dataRead(void* data, size_t size, size_t count, void* dataSource)
	{
		hsbase* stream = (hsbase*)dataSource;
		return stream->readRaw(data, (int)(size * count));
	}

	static int _dataSeek(void* dataSource, ogg_int64_t offset, int whence)
	{
		hsbase::SeekMode mode = hsbase::CURRENT;
		switch (whence)
		{
		case SEEK_CUR:
			mode = hsbase::CURRENT;
			break;
		case SEEK_SET:
			mode = hsbase::START;
			break;
		case SEEK_END:
			mode = hsbase::END;
			break;
		}
		((hsbase*)dataSource)->seek((long)offset, mode);
		return 0;
	}

	static int _dataClose(void* dataSource) // an empty function is required on Android as it may crash otherwise
	{
		return 0;
	}

	static long _dataTell(void* dataSource)
	{
		return (long)((hsbase*)dataSource)->position();
	}

	OGG_Source::OGG_Source(chstr filename, SourceMode sourceMode, BufferMode bufferMode) : Source(filename, sourceMode, bufferMode)
	{
	}

	OGG_Source::~OGG_Source()
	{
		this->close();
	}

	bool OGG_Source::open()
	{
		Source::open();
		if (!this->streamOpen)
		{
			return false;
		}
		// setting the special callbacks
		ov_callbacks callbacks;
		callbacks.read_func = &_dataRead;
		callbacks.seek_func = &_dataSeek;
		callbacks.close_func = &_dataClose; // may not be NULL because it may crash on Android otherwise
		callbacks.tell_func = &_dataTell;
		if (ov_open_callbacks((void*)this->stream, &this->oggStream, NULL, 0, callbacks) == 0)
		{
			vorbis_info* info = ov_info(&this->oggStream, -1);
			this->channels = (int)info->channels;
			this->samplingRate = (int)info->rate;
			this->bitsPerSample = 16; // always 16 bit data
			int logicalSamples = (int)ov_pcm_total(&this->oggStream, -1);
			this->size = logicalSamples * this->channels * this->bitsPerSample / 8;
			this->duration = (float)logicalSamples / this->samplingRate;
			ov_pcm_seek(&this->oggStream, 0); // make sure the PCM stream is at the beginning to avoid nasty surprises
		}
		else
		{
			hlog::error(xal::logTag, "OGG: error reading data!");
			this->close();
		}
		return this->streamOpen;
	}

	void OGG_Source::close()
	{
		if (this->streamOpen)
		{
			ov_clear(&this->oggStream);
		}
		Source::close();
	}

	void OGG_Source::rewind()
	{
		if (this->streamOpen)
		{
			ov_pcm_seek(&this->oggStream, 0);
		}
	}

	bool OGG_Source::load(hstream& output)
	{
		if (!Source::load(output))
		{
			return false;
		}
		unsigned long remaining = this->size;
		output.prepareManualWriteRaw((int)remaining);
		char* buffer = (char*)output;
		int read = 0;
		while (remaining > 0)
		{
			read = (int)ov_read(&this->oggStream, buffer, (int)remaining, 0, 2, 1, &_section);
			if (read == 0)
			{
				memset(buffer, 0, remaining);
				break;
			}
			remaining -= read;
			buffer += read;
		}
		return true;
	}

	int OGG_Source::loadChunk(hstream& output, int size)
	{
		if (Source::loadChunk(output, size) == 0)
		{
			return 0;
		}
		int remaining = size;
		output.prepareManualWriteRaw(remaining);
		char* buffer = (char*)output;
		int read = 0;
		while (remaining > 0)
		{
			read = (int)ov_read(&this->oggStream, buffer, remaining, 0, 2, 1, &_section);
			if (read == 0)
			{
				break;
			}
			remaining -= read;
			buffer += read;
		}
		int result = (size - remaining);
		output.truncate((int64_t)result); // if there wasn't enough data to fill the entire ouput buffer
		return result;
	}

}
#endif
