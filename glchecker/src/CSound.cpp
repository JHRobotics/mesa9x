/*
Copyright (c) 2016, David Liu
All rights reserved. Read the license for more information.
*/

#include "CEngine.h"

/* Functions */
bool CEngine::CLoadSample(const char *path, CSample &sample, unsigned long maxplay)
{
	// Load stream
	/*if (!(sample = BASS_SampleLoad(false, path, 0, 0, maxplay, BASS_SAMPLE_MONO)))
	{
		strcpy(inerr, "Unable to find/load sound file."); // must be .mp3, .mp2, .mp1, .ogg, .wav or .aif
		return false;
	}*/
	return true;
}

bool CEngine::CCreateSound(CSample sample, CSound &chan, bool loop)
{
	/*if (!(chan = BASS_SampleGetChannel(sample, false)))
	{
		strcpy(inerr, "Unable to create channel from loaded sample.");
		return false;
	}
	else if (loop && BASS_ChannelFlags(chan, BASS_SAMPLE_LOOP, BASS_SAMPLE_LOOP) == -1)
	{
		strcpy(inerr, "Unable to configure sound channel.");
		return false;
	}*/
	return true;
}

void CEngine::CClearSound(CSound chan)
{
	//BASS_ChannelStop(chan); 
}

void CEngine::CClearSample(CSample sample)
{
	//BASS_SampleFree(sample); 
}

void CEngine::CStartSound(CSound chan)
{
	//BASS_ChannelPlay(chan, false);
}

void CEngine::CPauseSound(CSound chan)
{
	//BASS_ChannelPause(chan);
}

void CEngine::CStopSound(CSound chan)
{
	/*BASS_ChannelPause(chan);
	BASS_ChannelSetPosition(chan, 0, BASS_POS_BYTE);*/
}

void CEngine::CSoundVol(const unsigned short vol)
{
	//BASS_SetConfig(BASS_CONFIG_GVOL_SAMPLE, vol);
}
