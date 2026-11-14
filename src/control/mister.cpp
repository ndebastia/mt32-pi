//
// mister.cpp
//
// mt32-pi - A baremetal MIDI synthesizer for Raspberry Pi
// Copyright (C) 2020-2023 Dale Whinham <daleyo@gmail.com>
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

#include <circle/logger.h>

#include "control/mister.h"

LOGMODULE("mistercontrol");

constexpr u8 MisterI2CAddress = 0x45;

CMisterControl::CMisterControl(CI2CMaster* pI2CMaster, TEventQueue& EventQueue)
	: m_pI2CMaster(pI2CMaster),
	  m_pEventQueue(&EventQueue),

	  bMisterActive(false),
	  m_LastSystemStatus{TMisterSynth::Unknown, 0xFF, 0xFF},
		  m_LastMisterStatus{TMisterSynth::Unknown, 0xFF, 0xFF},
		  m_PendingStatus{TMisterSynth::Unknown, 0xFF, 0xFF},
		  m_nPendingCount(0),
		  m_nConsecutiveErrors(0),
		  m_nBackoffPolls(0)
{
}

static bool IsValidStatus(const TMisterStatus& Status)
{
	if (Status.Synth != TMisterSynth::Mute &&
	    Status.Synth != TMisterSynth::MT32 &&
	    Status.Synth != TMisterSynth::SoundFont &&
	    Status.Synth != TMisterSynth::Unknown)
		return false;

	// TMT32ROMSet currently has five values (old, new, cm32l, any, all).
	if (Status.MT32ROMSet != 0xFF && Status.MT32ROMSet > static_cast<u8>(TMT32ROMSet::All))
		return false;

	// 0xFF means "no SoundFont"; every other byte is a valid list index on the
	// wire (the list itself is capped at 512 entries).
	return true;
}

void CMisterControl::Update(const TMisterStatus& SystemStatus)
{
	assert(m_pI2CMaster != nullptr);

	// After a burst of errors, leave the bus alone for a while instead of
	// hammering it every 50 ms and logging on every poll.
	if (m_nBackoffPolls > 0)
	{
		--m_nBackoffPolls;
		return;
	}

	// Read current status from MiSTer
	TMisterStatus MisterStatus;
	if (m_pI2CMaster->Read(MisterI2CAddress, &MisterStatus, sizeof(MisterStatus)) < 0 ||
	    !IsValidStatus(MisterStatus))
	{
		if (++m_nConsecutiveErrors >= 3)
		{
			ResetState();
			m_nConsecutiveErrors = 0;
			m_nPendingCount = 0;
			m_nBackoffPolls = 10;   // 500 ms at the 20 Hz poll rate
		}
		return;
	}

	m_nConsecutiveErrors = 0;

	// Core was reset or "Reset Hanging Notes" was selected from OSD; turn off all sound
	if (MisterStatus.Synth == TMisterSynth::Mute)
	{
		LOGNOTE("Stopping synth activity");
		EnqueueAllSoundOffEvent();
		WriteConfigToMister(SystemStatus);
		m_LastMisterStatus = MisterStatus;
		m_nPendingCount = 0;
		return;
	}

	if (!bMisterActive)
	{
		// First valid reply from MiSTer; apply its config
		ApplyConfig(MisterStatus, SystemStatus);

		// Write config back to MiSTer
		if (!WriteConfigToMister(MisterStatus))
			return;

		// Show MiSTer logo
		EnqueueDisplayImageEvent();

		m_LastMisterStatus = MisterStatus;
		bMisterActive = true;
		return;
	}

	// If the state has been changed by user controls/SysEx, we just need to update the MiSTer status
	if (SystemStatus != m_LastSystemStatus)
	{
		// Write config back to MiSTer
		if (!WriteConfigToMister(SystemStatus))
		{
			ResetState();
			return;
		}

		m_LastSystemStatus = SystemStatus;
		m_nPendingCount = 0;
		return;
	}

	if (MisterStatus == m_LastMisterStatus)
	{
		m_nPendingCount = 0;
		return;
	}

	// Require two consecutive identical reads before acting on a change.
	if (MisterStatus != m_PendingStatus)
	{
		m_PendingStatus = MisterStatus;
		m_nPendingCount = 1;
		return;
	}

	if (++m_nPendingCount < 2)
		return;

	m_nPendingCount = 0;

	// The state has been changed by MiSTer; apply it
	ApplyConfig(MisterStatus, SystemStatus);

	// Write config back to MiSTer
	if (!WriteConfigToMister(MisterStatus))
	{
		ResetState();
		return;
	}

	m_LastMisterStatus = MisterStatus;
}

void CMisterControl::ApplyConfig(const TMisterStatus& NewStatus, const TMisterStatus& CurrentStatus)
{
	TEvent Event;

	if (NewStatus.Synth != CurrentStatus.Synth)
	{
		Event.Type = TEventType::SwitchSynth;
		Event.SwitchSynth.Synth = NewStatus.Synth == TMisterSynth::MT32 ? TSynth::MT32 : TSynth::SoundFont;
		m_pEventQueue->Enqueue(Event);
	}

	if (NewStatus.MT32ROMSet != CurrentStatus.MT32ROMSet)
	{
		Event.Type = TEventType::SwitchMT32ROMSet;
		Event.SwitchMT32ROMSet.ROMSet = static_cast<TMT32ROMSet>(NewStatus.MT32ROMSet);
		m_pEventQueue->Enqueue(Event);
	}

	if (NewStatus.SoundFontIndex != CurrentStatus.SoundFontIndex)
	{
		Event.Type = TEventType::SwitchSoundFont;
		Event.SwitchSoundFont.Index = NewStatus.SoundFontIndex;
		m_pEventQueue->Enqueue(Event);
	}
}

bool CMisterControl::WriteConfigToMister(const TMisterStatus& NewStatus)
{
	// Write config back to MiSTer
	if (m_pI2CMaster->Write(MisterI2CAddress, &NewStatus, sizeof(NewStatus)) < 0)
	{
		LOGERR("MiSTer write failed");
		return false;
	}

	//LOGDBG("MiSTer Tx: 0x%02x 0x%02x 0x%02x", static_cast<u8>(NewStatus.Synth), NewStatus.MT32ROMSet, NewStatus.SoundFontIndex);
	return true;
}

void CMisterControl::ResetState()
{
	if (bMisterActive)
	{
		// MiSTer has just stopped responding; dispatch an All Sound Off event
		LOGNOTE("MiSTer stopped responding; turning notes off");
		EnqueueAllSoundOffEvent();
		bMisterActive = false;
		m_LastSystemStatus = {TMisterSynth::Unknown, 0xFF, 0xFF};
		m_LastMisterStatus = {TMisterSynth::Unknown, 0xFF, 0xFF};
	}
}

void CMisterControl::EnqueueDisplayImageEvent()
{
	TEvent Event;
	Event.Type = TEventType::DisplayImage;
	Event.DisplayImage.Image = TImage::MisterLogo;
	m_pEventQueue->Enqueue(Event);
}

void CMisterControl::EnqueueAllSoundOffEvent()
{
	TEvent Event;
	Event.Type = TEventType::AllSoundOff;
	m_pEventQueue->Enqueue(Event);
}
