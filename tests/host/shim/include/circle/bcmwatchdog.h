//
// bcmwatchdog.h
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

// Fake Circle: hardware watchdog (host test harness)
#ifndef _circle_bcmwatchdog_h
#define _circle_bcmwatchdog_h

#include <circle/types.h>

class CBcmWatchdog
{
public:
	static const unsigned MaxTimeoutSeconds = 15;
	static const unsigned PartitionDefault = 0;
	static const unsigned PartitionHalt = 63;

	void Start(unsigned nTimeoutSeconds = MaxTimeoutSeconds) { (void) nTimeoutSeconds; }
	void Stop() {}
	void Restart(unsigned nPartition = PartitionDefault) { (void) nPartition; }
	boolean IsRunning() const { return FALSE; }
	unsigned GetTimeLeft() const { return 0; }
};

#endif
