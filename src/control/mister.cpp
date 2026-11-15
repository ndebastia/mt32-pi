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
	  m_LastReadStatus{TMisterSynth::Unknown, 0xFF, 0xFF},
	  m_nReadErrors(0),
	  m_nWrites(0),

	  // Debounce state. These were dropped from the member-initialiser list in a
	  // revision of this change; an indeterminate m_nConsecutiveErrors would
	  // declare the link dead on the first read error (or never), and a garbage
	  // m_PendingStatus would compare against a real status.
	  m_PendingStatus{TMisterSynth::Unknown, 0xFF, 0xFF},
	  m_nPendingCount(0),
	  m_nConsecutiveErrors(0),
	  m_nBackoffPolls(0)
{
}

static bool IsValidStatus(const TMisterStatus& Status)
{
	// Unknown is a local sentinel, never a value the core sends. Rejecting it
	// stops a floating bus (all bytes 0xFF) from looking like a valid status.
	if (Status.Synth != TMisterSynth::Mute &&
	    Status.Synth != TMisterSynth::MT32 &&
	    Status.Synth != TMisterSynth::SoundFont)
		return false;

	// TMT32ROMSet currently has five values (old, new, cm32l, any, all). 0xFF
	// is not "no ROM set" on the wire: the RTL always drives two bits.
	if (Status.MT32ROMSet > static_cast<u8>(TMT32ROMSet::All))
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
		++m_nReadErrors;

		// Announce ourselves once a second while the link is down. This is NOT
		// what makes the link visible: every FPGA core latches "mt32-pi
		// present" on the I2C ADDRESS MATCH, whatever the direction — see
		// sys/mt32pi.sv, `if (tmp[7:1] == 'h45 || tmp[7:1] == 'h3c)` sets
		// mt32_available before the R/W bit is even decoded. A read-only link
		// is therefore detected already. The periodic write is a belt-and-
		// braces probe for a core that would gate presence on a write, and it
		// costs nothing: a write to an absent slave fails and is never
		// counted in m_nWrites (that counter only increments on success).
		if (!bMisterActive && (m_nReadErrors % 20) == 1)
			WriteConfigToMister(SystemStatus);

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
	m_LastReadStatus = MisterStatus;

	// Core was reset or "Reset Hanging Notes" was selected from OSD; turn off all
	// sound once, not on every 50 ms poll while the core is held in reset.
	if (MisterStatus.Synth == TMisterSynth::Mute)
	{
		if (m_LastMisterStatus.Synth != TMisterSynth::Mute)
		{
			LOGNOTE("Stopping synth activity");
			EnqueueAllSoundOffEvent();

			// One reply is enough to release the core's reset request. If it
			// fails, leave m_LastMisterStatus alone so the next poll retries.
			if (!WriteConfigToMister(SystemStatus))
				return;
		}

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

	// 0xFF means "no SoundFont" (for example in MT-32 mode); do not ask the
	// synth to switch to a non-existent list entry.
	if (NewStatus.SoundFontIndex != CurrentStatus.SoundFontIndex && NewStatus.SoundFontIndex != 0xFF)
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
		// Announcing ourselves to an absent MiSTer is expected while the link is
		// down; only a failure after the link was up is worth an error line.
		if (bMisterActive)
			LOGERR("MiSTer write failed");
		else
			LOGDBG("MiSTer write failed (no link yet)");

		return false;
	}

	++m_nWrites;
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
		// Also clear the diagnostic mirrors: mt32pi-status.txt prints the
		// last bytes actually read, and keeping the last good triplet while
		// mister_active=no would present a stale value as if it were current.
		m_LastReadStatus = {TMisterSynth::Unknown, 0xFF, 0xFF};
		m_PendingStatus = {TMisterSynth::Unknown, 0xFF, 0xFF};
		m_nPendingCount = 0;
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
