//
// power.h
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

#ifndef _power_h
#define _power_h

#include <circle/bcmpropertytags.h>
#include <circle/cputhrottle.h>
#if RASPPI >= 5
#include <circle/gpiopin.h>
#endif
#include <circle/types.h>

class CPower
{
public:
	CPower();

	void Update();
	void Awaken();
	void SetPowerSaveTimeout(u16 nSeconds) { m_nPowerSaveTimeout = nSeconds; }

	unsigned GetClockRate() const { return CCPUThrottle::Get()->GetClockRate(); }
	unsigned GetTemperature() const { return CCPUThrottle::Get()->GetTemperature(); }
	u32 GetThrottledStatus() const { return m_LastThrottledStatus; }
	bool IsPowerSaving() const { return m_State == TState::PowerSaving; }

protected:
	// Derived classes can delay the clock change until audio and display have
	// quiesced. Called from the main loop while the state is EnteringPowerSaving.
	virtual bool IsReadyForPowerSavingMode() const { return true; }

	virtual void OnEnterPowerSavingMode();
	virtual void OnExitPowerSavingMode();

	virtual void OnThrottleDetected();
	virtual void OnUnderVoltageDetected();

private:
	enum class TState
	{
		Normal,
		EnteringPowerSaving,
		PowerSaving
	};

	void UpdateThrottledStatus();
#if RASPPI >= 5
	void UpdateFan();
#endif

	u16 m_nPowerSaveTimeout;
	unsigned int m_nLastActivityTime;
	TState m_State;

	CBcmPropertyTags m_Tags;
	u32 m_LastThrottledStatus;
	unsigned int m_nLastThrottledCheckTime;
#if RASPPI >= 5
	// Active Cooler / Case Fan on the Pi 5 fan connector
	CGPIOPin m_FanPin;
	bool m_bFanPinReady;
	bool m_bFanOn;
#endif
};

#endif
