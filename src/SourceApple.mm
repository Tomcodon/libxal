/************************************************************************************\
This source file is part of the KS(X) audio library                                  *
For latest info, see http://code.google.com/p/libxal/                                *
**************************************************************************************
Copyright (c) 2010 Kresimir Spes (kreso@cateia.com), Boris Mikic,                    *
			       Ivan Vucica (ivan@vucica.net)                                     *
*                                                                                    *
* This program is free software; you can redistribute it and/or modify it under      *
* the terms of the BSD license: http://www.opensource.org/licenses/bsd-license.php   *
\************************************************************************************/
#include <hltypes/hstring.h>
#include "Category.h"
#include "SourceApple.h"
#include "SoundBuffer.h"
#include "StreamSound.h"
#include "AudioManager.h"

#ifndef __APPLE__
	#include <AL/al.h>
#else
	#include <OpenAL/al.h>
#endif

#import <AVFoundation/AVFoundation.h>

#define avAudioPlayer ((AVAudioPlayer*)this->avAudioPlayer_Void)

namespace xal
{
/******* CONSTRUCT / DESTRUCT ******************************************/

	SourceApple::SourceApple(SoundBuffer* sound, unsigned int sourceId) : Sound(),
		gain(1.0f), looping(false), paused(false), fadeTime(0.0f),
		fadeSpeed(0.0f), bound(true), sampleOffset(0.0f)
	{
		this->sound = sound;
		this->sourceId = sourceId;
		
		
		NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:sound->getVirtualFileName().c_str()]];
		this->avAudioPlayer_Void = [[AVAudioPlayer alloc] initWithContentsOfURL:url 
																		  error:nil];
		
		
	}

	SourceApple::~SourceApple()
	{
		
		[avAudioPlayer release];
	
	}

/******* METHODS *******************************************************/
	
	void SourceApple::update(float k)
	{
		if (this->sourceId == 0)
		{
			return;
		}
		
		if (this->isPlaying())
		{
			if (this->isFading())
			{
				this->fadeTime += this->fadeSpeed * k;
				if (this->fadeTime >= 1.0f && this->fadeSpeed > 0.0f)
				{
					avAudioPlayer.volume = this->gain * this->sound->getCategory()->getGain() * xal::mgr->getGlobalGain();
					
					this->fadeTime = 1.0f;
					this->fadeSpeed = 0.0f;
				}
				else if (this->fadeTime <= 0.0f && this->fadeSpeed < 0.0f)
				{
					this->fadeTime = 0.0f;
					this->fadeSpeed = 0.0f;
					if (!this->paused)
					{
						this->stop();
						return;
					}
					this->pause();
				}
				else
				{
					avAudioPlayer.volume = this->fadeTime * this->gain * this->sound->getCategory()->getGain() * xal::mgr->getGlobalGain();
					
				}
			}
		}
		if (!this->isPlaying() && !this->isPaused())
		{
			this->unbind();
		}
	}

	Sound* SourceApple::play(float fadeTime, bool looping)
	{
		if (this->sourceId == 0)
		{
			this->sourceId = xal::mgr->allocateSourceId();
			if (this->sourceId == 0)
			{
				return NULL;
			}
#ifdef _DEBUG
			xal::mgr->logMessage(hsprintf("allocated source ID %d", this->sourceId));
#endif
		}
#ifdef _DEBUG
		xal::mgr->logMessage("play sound " + this->sound->getVirtualFileName());
#endif
		if (!this->paused)
		{
			this->looping = looping;
		}
		bool alreadyFading = this->isFading();
		if (!alreadyFading)
		{
			// if set to -1, we will loop indefinitely. if set to 0, no repeats.
			avAudioPlayer.numberOfLoops = (this->looping ? 0 : -1); 
			
			if (this->isPaused())
			{
				avAudioPlayer.currentTime = this->sampleOffset;
			}
		}
		if (fadeTime > 0.0f)
		{
			this->fadeSpeed = 1.0f / fadeTime;
#ifdef _DEBUG
			xal::mgr->logMessage("fading in sound " + this->sound->getVirtualFileName());
#endif
		}
		else
		{
			this->fadeTime = 1.0f;
			this->fadeSpeed = 0.0f;
		}
		avAudioPlayer.volume = this->fadeTime * this->gain *
			this->sound->getCategory()->getGain() * xal::mgr->getGlobalGain();
		if (!alreadyFading)
		{
			//alSourcePlay(this->sourceId);
			if(![avAudioPlayer isPlaying])
				[avAudioPlayer play];
		}
		this->paused = false;
		return this;
	}

	void SourceApple::stop(float fadeTime)
	{
		this->stopSoft(fadeTime);
		if (this->sourceId != 0 && fadeTime <= 0.0f)
		{
			this->unbind(this->paused);
		}
	}

	void SourceApple::pause(float fadeTime)
	{
		this->stopSoft(fadeTime, true);
		if (this->sourceId != 0 && fadeTime <= 0.0f)
		{
			this->unbind(this->paused);
		}
	}

	void SourceApple::stopSoft(float fadeTime, bool pause)
	{
		if (this->sourceId == 0)
		{
			return;
		}
		this->paused = pause;
		if (fadeTime > 0.0f)
		{
#ifdef _DEBUG
			xal::mgr->logMessage("fading out sound " + this->sound->getVirtualFileName());
#endif
			this->fadeSpeed = -1.0f / fadeTime;
			return;
		}
#ifdef _DEBUG
		xal::mgr->logMessage(hstr(this->paused ? "pause" : "stop") + " sound " + this->sound->getVirtualFileName());
#endif
		this->fadeTime = 0.0f;
		this->fadeSpeed = 0.0f;
		//alGetSourcef(this->sourceId, AL_SEC_OFFSET, &this->sampleOffset);
		//alSourceStop(this->sourceId);
		this->sampleOffset = avAudioPlayer.currentTime;
		[avAudioPlayer stop];
		if (this->sound->getCategory()->isStreamed())
		{
			this->sound->setSourceId(this->sourceId);
			if (this->paused)
			{
				((StreamSound*)this->sound)->unqueueBuffers();
			}
			else
			{
				((StreamSound*)this->sound)->rewindStream();
			}
		}
	}

	void SourceApple::unbind(bool pause)
	{
		if (!this->isLocked())
		{
			this->sourceId = 0;
			if (!pause)
			{
				this->sound->unbindSource(this);
				xal::mgr->destroySource(this);
			}
		}
	}
	
/******* PROPERTIES ****************************************************/

	void SourceApple::setGain(float gain)
	{
		this->gain = gain;
		if (this->sourceId != 0)
		{
			avAudioPlayer.volume = this->gain *
				this->sound->getCategory()->getGain() * xal::mgr->getGlobalGain();
		}
	}

	unsigned int SourceApple::getBuffer()
	{
		return this->sound->getBuffer();
	}
	
	bool SourceApple::isPlaying()
	{
		return avAudioPlayer.playing;
		
		/////////////////////////
		if (this->sourceId == 0)
		{
			return false;
		}
		
		if (this->sound->getCategory()->isStreamed())
		{
			int queued;
			alGetSourcei(this->sourceId, AL_BUFFERS_QUEUED, &queued);
			int count;
			alGetSourcei(this->sourceId, AL_BUFFERS_PROCESSED, &count);
			return (queued > 0 || count > 0);
		}
		int state;
		alGetSourcei(this->sourceId, AL_SOURCE_STATE, &state);
		return (state == AL_PLAYING);
	}

	bool SourceApple::isPaused()
	{
		return (this->paused && !this->isFading());
	}
	
	bool SourceApple::isFading()
	{
		return (this->fadeSpeed != 0.0f);
	}

	bool SourceApple::isFadingIn()
	{
		return (this->fadeSpeed > 0.0f);
	}

	bool SourceApple::isFadingOut()
	{
		return (this->fadeSpeed < 0.0f);
	}

}
