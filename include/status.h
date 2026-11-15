//
// status.h
//
// mt32-pi - A baremetal MIDI synthesizer for Raspberry Pi
// Copyright (C) 2026 pim-32 contributors
//
// This file is part of mt32-pi.
//
// mt32-pi is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// mt32-pi is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// mt32-pi. If not, see <http://www.gnu.org/licenses/>.
//

#ifndef _status_h
#define _status_h

#include <circle/types.h>

#include "threadsafety.h"

// A small read-only snapshot of the running build and power state. It is
// updated from the main/UI tasks and served as a virtual file by the FTP
// server, so a headless Pi can be inspected from the network without a serial
// console. The lock is only held while copying fields: no I/O happens inside.
class CSystemStatus
{
public:
	CSystemStatus();

	void SetBuild(const char* pVersion, bool bWFESleep, unsigned nPLLDRate, bool bAudioStarted);
	void SetConfig(const char* pOutputDevice, const char* pLCDType, bool bMisterEnabled,
		       unsigned nSampleRate, unsigned nChunkSize);
	void SetI2CBus(const char* pDevices);
	void SetRuntime(unsigned nClockRate, unsigned nTemperature, u32 nThrottledStatus,
			bool bPowerSaving, bool bAudioActive, unsigned nAudioQueuePercent,
			unsigned nAudioRenderCalls, unsigned nAudioSleepCalls, unsigned nAudioDropped,
			unsigned nMIDIMessages, unsigned nAudioPeakPermille);
	void SetMister(bool bActive, u8 nSynth, u8 nROMSet, u8 nSoundFontIndex,
		       unsigned nReadErrors, unsigned nWrites);
	void SetPower(bool bPresent, unsigned nBusMillivolts, int nCurrentMilliamps,
		      int nPowerMilliwatts);

	size_t Format(char* pBuffer, size_t nSize);

private:
	CAnnotatedSpinLock m_Lock;

	char m_Version[64] GUARDED_BY(m_Lock);
	bool m_bWFESleep GUARDED_BY(m_Lock);
	unsigned m_nPLLDRate GUARDED_BY(m_Lock);
	bool m_bAudioStarted GUARDED_BY(m_Lock);

	// Configuration actually in force, and the devices found on the I2C bus at
	// boot. The LCD and the MiSTer link share that bus, so this says whether a
	// silent link is a wiring problem or a protocol problem.
	char m_OutputDevice[16] GUARDED_BY(m_Lock);
	char m_LCDType[24] GUARDED_BY(m_Lock);
	bool m_bMisterEnabled GUARDED_BY(m_Lock);
	unsigned m_nSampleRate GUARDED_BY(m_Lock);
	unsigned m_nChunkSize GUARDED_BY(m_Lock);
	char m_I2CDevices[96] GUARDED_BY(m_Lock);

	unsigned m_nClockRate GUARDED_BY(m_Lock);
	unsigned m_nTemperature GUARDED_BY(m_Lock);
	u32 m_nThrottledStatus GUARDED_BY(m_Lock);
	bool m_bPowerSaving GUARDED_BY(m_Lock);
	bool m_bAudioActive GUARDED_BY(m_Lock);
	unsigned m_nAudioQueuePercent GUARDED_BY(m_Lock);
	unsigned m_nAudioRenderCalls GUARDED_BY(m_Lock);
	unsigned m_nAudioSleepCalls GUARDED_BY(m_Lock);
	unsigned m_nAudioDropped GUARDED_BY(m_Lock);
	unsigned m_nMIDIMessages GUARDED_BY(m_Lock);
	unsigned m_nAudioPeakPermille GUARDED_BY(m_Lock);

	// Last three-byte status the MiSTer actually returned, plus link counters.
	// A rejected read leaves 0xff in every byte, which is what an unwired or
	// powered-down MiSTer side looks like.
	bool m_bMisterActive GUARDED_BY(m_Lock);
	u8 m_nMisterSynth GUARDED_BY(m_Lock);
	u8 m_nMisterROMSet GUARDED_BY(m_Lock);
	u8 m_nMisterSoundFontIndex GUARDED_BY(m_Lock);
	unsigned m_nMisterReadErrors GUARDED_BY(m_Lock);
	unsigned m_nMisterWrites GUARDED_BY(m_Lock);

	bool m_bPowerPresent GUARDED_BY(m_Lock);
	unsigned m_nBusMillivolts GUARDED_BY(m_Lock);
	int m_nCurrentMilliamps GUARDED_BY(m_Lock);
	int m_nPowerMilliwatts GUARDED_BY(m_Lock);
};

CSystemStatus* SystemStatus();

#endif
