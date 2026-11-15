//
// powermonitor.h
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

#ifndef _powermonitor_h
#define _powermonitor_h

#include <circle/i2cmaster.h>
#include <circle/types.h>

// Optional INA219 shunt monitor on the Pi's 5 V rail. The Pi has no on-board
// power sensor, so this is the only way to report actual watts; the rest of the
// status file reports software proxies (clock, temperature, throttle bits,
// audio-core activity).
struct TPowerSample
{
	bool bPresent;
	unsigned nBusMillivolts;
	int nCurrentMilliamps;
	int nPowerMilliwatts;
};

class CPowerMonitor
{
public:
	CPowerMonitor();

	bool Initialize(CI2CMaster* pI2CMaster, bool bEnabled, u8 Address, unsigned nShuntMilliohms);
	bool IsPresent() const { return m_bPresent; }
	TPowerSample Sample();

private:
	bool WriteRegister(u8 nRegister, u16 nValue);
	bool ReadRegister(u8 nRegister, u16* pValue);

	CI2CMaster* m_pI2CMaster;
	bool m_bPresent;
	u8 m_Address;
	unsigned m_nShuntMilliohms;
	unsigned m_nCurrentLSBUa;
	unsigned m_nErrors;
};

#endif
