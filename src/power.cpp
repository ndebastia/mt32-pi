//
// power.cpp
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

#include <circle/bcmpropertytags.h>
#include <circle/cputhrottle.h>
#include <circle/logger.h>
#include <circle/timer.h>

#include "power.h"

LOGMODULE("power");

#if RASPPI >= 5
// Pi 5 fan connector: GPIO 45, active low (as Circle's CCPUThrottle drives it).
// Switch on at FanOnTemperature, off again below FanOffTemperature.
constexpr unsigned FanGPIOPin          = 45;
constexpr unsigned FanOnTemperature    = 60;
constexpr unsigned FanOffTemperature   = 50;
#endif

// Bits in the throttled status response
constexpr u32 UnderVoltageOccurredBit = 1 << 16;
constexpr u32 ThrottlingOccurredBit   = 1 << 18;

CPower::CPower()
	: m_nPowerSaveTimeout(300),
	  m_nLastActivityTime(0),
	  m_State(TState::Normal),
	  m_LastThrottledStatus(0),
	  m_nLastThrottledCheckTime(0)
#if RASPPI >= 5
	  , m_bFanPinReady(false),
	  m_bFanOn(false)
#endif
{
}

void CPower::Update()
{
	unsigned int nTicks = CTimer::Get()->GetTicks();

	// Power save timeout: ask the derived class to stop audio and blank the
	// display first. The clock is lowered only once it reports that it is safe.
	if (m_State == TState::Normal && m_nPowerSaveTimeout && (nTicks - m_nLastActivityTime) >= m_nPowerSaveTimeout * HZ)
	{
		m_State = TState::EnteringPowerSaving;
		OnEnterPowerSavingMode();
	}

	if (m_State == TState::EnteringPowerSaving && IsReadyForPowerSavingMode())
	{
		CCPUThrottle::Get()->SetSpeed(TCPUSpeed::CPUSpeedLow);
		m_State = TState::PowerSaving;
	}

	// Check for undervoltage and throttling. This is a firmware mailbox call and
	// the main loop runs at spin rate, so poll it at a rate that is useful to a
	// human. On the Pi 5 the fan state is refreshed once a second; elsewhere a
	// four-second interval is plenty and keeps the mailbox off the MIDI path.
#if RASPPI >= 5
	constexpr unsigned int ThrottledStatusInterval = HZ;
#else
	constexpr unsigned int ThrottledStatusInterval = MSEC2HZ(4000);
#endif
	if ((nTicks - m_nLastThrottledCheckTime) >= ThrottledStatusInterval)
	{
		m_nLastThrottledCheckTime = nTicks;
		UpdateThrottledStatus();
#if RASPPI >= 5
		UpdateFan();
#endif
	}
}

#if RASPPI >= 5
void CPower::UpdateFan()
{
	// SoC temperature in degrees Celsius from the firmware; 0 on failure
	const unsigned nTemperature = CCPUThrottle::Get()->GetTemperature();
	if (nTemperature == 0)
		return;

	// Set up the pin here rather than in the constructor: something during startup
	// returns GPIO 45 to an undriven state, which runs the fan at full speed
	if (!m_bFanPinReady)
	{
		m_FanPin.AssignPin(FanGPIOPin);
		m_FanPin.SetMode(GPIOModeOutput, false);
		m_bFanPinReady = true;
	}

	if (!m_bFanOn && nTemperature >= FanOnTemperature)
	{
		m_bFanOn = true;
		LOGNOTE("Fan on (%uC)", nTemperature);
	}
	else if (m_bFanOn && nTemperature < FanOffTemperature)
	{
		m_bFanOn = false;
		LOGNOTE("Fan off (%uC)", nTemperature);
	}

	// Rewrite the state every time (one write a second), in case anything else
	// touched the pin; LOW runs the fan, HIGH stops it
	m_FanPin.Write(m_bFanOn ? LOW : HIGH);
}
#endif

void CPower::Awaken()
{
	m_nLastActivityTime = CTimer::Get()->GetTicks();

	if (m_State == TState::Normal)
		return;

	// If the clock was already lowered, restore it. If we were still in the
	// entering state it is still at maximum, so only the audio/display side
	// needs to be brought back.
	if (m_State == TState::PowerSaving)
		CCPUThrottle::Get()->SetSpeed(TCPUSpeed::CPUSpeedMaximum);

	m_State = TState::Normal;
	OnExitPowerSavingMode();
}

void CPower::OnEnterPowerSavingMode()
{
	LOGNOTE("Entering power saving mode");
}

void CPower::OnExitPowerSavingMode()
{
	LOGNOTE("Leaving power saving mode");
}

void CPower::OnThrottleDetected()
{
	LOGWARN("CPU throttling by firmware detected; check power supply/cooling");
}

void CPower::OnUnderVoltageDetected()
{
	LOGWARN("Undervoltage detected; check power supply");
}

void CPower::UpdateThrottledStatus()
{
	// Get throttled status from the firmware and clear status bits
	TPropertyTagSimple ThrottledStatus;
	ThrottledStatus.nValue = 0xFFFF;
	m_Tags.GetTag(PROPTAG_GET_THROTTLED, &ThrottledStatus, sizeof(ThrottledStatus), sizeof(ThrottledStatus.nValue));

	bool bNewVal = ThrottledStatus.nValue & ThrottlingOccurredBit;
	bool bOldVal = m_LastThrottledStatus & ThrottlingOccurredBit;

	if (bNewVal && bOldVal != bNewVal)
		OnThrottleDetected();

	bNewVal = ThrottledStatus.nValue & UnderVoltageOccurredBit;
	bOldVal = m_LastThrottledStatus & UnderVoltageOccurredBit;

	if (bNewVal && bOldVal != bNewVal)
		OnUnderVoltageDetected();

	m_LastThrottledStatus = ThrottledStatus.nValue;
}
