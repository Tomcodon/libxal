/// @file
/// @author  Boris Mikic
/// @version 2.3
/// 
/// @section LICENSE
/// 
/// This program is free software; you can redistribute it and/or modify it under
/// the terms of the BSD license: http://www.opensource.org/licenses/bsd-license.php
/// 
/// @section DESCRIPTION
/// 
/// Provides a source for OGG format.

#ifdef HAVE_OGG
#ifndef XAL_OGG_SOURCE_H
#define XAL_OGG_SOURCE_H

#include <vorbis/vorbisfile.h>

#include <hltypes/hstream.h>
#include <hltypes/hstring.h>

#include "AudioManager.h"
#include "Source.h"
#include "xalExport.h"

namespace xal
{
	class xalExport OGG_Source : public Source
	{
	public:
		OGG_Source(chstr filename);
		~OGG_Source();

		bool open();
		void close();
		void rewind();
		bool load(unsigned char* output);
		int loadChunk(unsigned char* output, int size = STREAM_BUFFER_SIZE);

	protected:
		hstream stream;
		OggVorbis_File oggStream;

	};

}

#endif
#endif
