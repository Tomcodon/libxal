/// @file
/// @author  Boris Mikic
/// @version 2.0
/// 
/// @section LICENSE
/// 
/// This program is free software; you can redistribute it and/or modify it under
/// the terms of the BSD license: http://www.opensource.org/licenses/bsd-license.php
/// 
/// @section DESCRIPTION
/// 
/// Provides a buffer for audio data.

#ifndef XAL_BUFFER_H
#define XAL_BUFFER_H

#include <hltypes/hstring.h>

#include "AudioManager.h"
#include "xalExport.h"

namespace xal
{
	class Source;

	class xalExport Buffer
	{
	public:
		Buffer(chstr filename, HandlingMode loadMode, HandlingMode decodeMode);
		virtual ~Buffer();

		chstr getFilename() { return this->filename; }
		int getFileSize() { return this->fileSize; }
		unsigned char* getStream() { return this->stream; }
		Source* getSource() { return this->source; }

		int getSize();
		int getChannels();
		int getSamplingRate();
		int getBitsPerSample();
		float getDuration();
		Format getFormat();

		bool prepare(int offset = 0);
		bool release();
		void rewind();

		int getData(int offset, int size, unsigned char** output);
		int getData(int size, unsigned char** output);
		
	protected:
		hstr filename;
		int fileSize;
		HandlingMode loadMode;
		HandlingMode decodeMode;
		bool loaded;
		bool decoded;
		unsigned char* data;
		int dataIndex;
		unsigned char* stream;
		int streamIndex;
		Source* source;

	};

}

#endif
