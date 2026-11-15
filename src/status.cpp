//
// status.cpp
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

#include <cstdio>
#include <cstring>

#include "status.h"

CSystemStatus::CSystemStatus()
	: m_Version{'\0'},
	  m_bWFESleep(false),
	  m_nPLLDRate(0),
	  m_bAudioStarted(false),
	  m_nClockRate(0),
	  m_nTemperature(0),
	  m_nThrottledStatus(0),
	  m_bPowerSaving(false),
	  m_bAudioActive(false),
	  m_nAudioQueuePercent(0),
	  m_nAudioRenderCalls(0),
	  m_nAudioSleepCalls(0),
	  m_nAudioDropped(0),
	  m_nMIDIMessages(0),
	  m_bPowerPresent(false),
	  m_nBusMillivolts(0),
	  m_nCurrentMilliamps(0),
	  m_nPowerMilliwatts(0)
{
}

void CSystemStatus::SetBuild(const char* pVersion, bool bWFESleep, unsigned nPLLDRate, bool bAudioStarted)
{
	m_Lock.Acquire();

	strncpy(m_Version, pVersion ? pVersion : "unknown", sizeof(m_Version) - 1);
	m_Version[sizeof(m_Version) - 1] = '\0';
	m_bWFESleep = bWFESleep;
	m_nPLLDRate = nPLLDRate;
	m_bAudioStarted = bAudioStarted;

	m_Lock.Release();
}

void CSystemStatus::SetRuntime(unsigned nClockRate, unsigned nTemperature, u32 nThrottledStatus,
			       bool bPowerSaving, bool bAudioActive, unsigned nAudioQueuePercent,
			       unsigned nAudioRenderCalls, unsigned nAudioSleepCalls, unsigned nAudioDropped,
			       unsigned nMIDIMessages)
{
	m_Lock.Acquire();

	m_nClockRate = nClockRate;
	m_nTemperature = nTemperature;
	m_nThrottledStatus = nThrottledStatus;
	m_bPowerSaving = bPowerSaving;
	m_bAudioActive = bAudioActive;
	m_nAudioQueuePercent = nAudioQueuePercent;
	m_nAudioRenderCalls = nAudioRenderCalls;
	m_nAudioSleepCalls = nAudioSleepCalls;
	m_nAudioDropped = nAudioDropped;
	m_nMIDIMessages = nMIDIMessages;

	m_Lock.Release();
}

void CSystemStatus::SetPower(bool bPresent, unsigned nBusMillivolts, int nCurrentMilliamps,
			     int nPowerMilliwatts)
{
	m_Lock.Acquire();

	m_bPowerPresent = bPresent;
	m_nBusMillivolts = nBusMillivolts;
	m_nCurrentMilliamps = nCurrentMilliamps;
	m_nPowerMilliwatts = nPowerMilliwatts;

	m_Lock.Release();
}

size_t CSystemStatus::Format(char* pBuffer, size_t nSize)
{
	if (!pBuffer || nSize == 0)
		return 0;

	m_Lock.Acquire();

	const int nLength = snprintf(pBuffer, nSize,
		"mt32-pi status\n"
		"version=%s\n"
		"wfe=%s\n"
		"plld_hz=%u\n"
		"audio_started=%s\n"
		"clock_hz=%u\n"
		"temperature_c=%u\n"
		"throttled=0x%08x\n"
		"power_saving=%s\n"
		"audio_active=%s\n"
		"audio_queue_percent=%u\n"
		"audio_renders=%u\n"
		"audio_sleeps=%u\n"
		"audio_dropped=%u\n"
		"midi_messages=%u\n"
		"ina219=%s\n"
		"bus_mv=%u\n"
		"current_ma=%d\n"
		"power_mw=%d\n",
		m_Version,
		m_bWFESleep ? "on" : "off",
		m_nPLLDRate,
		m_bAudioStarted ? "yes" : "no",
		m_nClockRate,
		m_nTemperature,
		m_nThrottledStatus,
		m_bPowerSaving ? "yes" : "no",
		m_bAudioActive ? "yes" : "no",
		m_nAudioQueuePercent,
		m_nAudioRenderCalls,
		m_nAudioSleepCalls,
		m_nAudioDropped,
		m_nMIDIMessages,
		m_bPowerPresent ? "present" : "absent",
		m_nBusMillivolts,
		m_nCurrentMilliamps,
		m_nPowerMilliwatts);

	m_Lock.Release();

	if (nLength <= 0)
		return 0;

	const size_t nWritten = static_cast<size_t>(nLength);
	return nWritten < nSize ? nWritten : nSize - 1;
}

static CSystemStatus s_Status;

CSystemStatus* SystemStatus()
{
	return &s_Status;
}
