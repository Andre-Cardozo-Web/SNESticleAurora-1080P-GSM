

#include <stdio.h>
#include <string.h>
#include "types.h"
#include "snio.h"

#define SNIO_VERSION_5A22 (0x02)

/* AURORA_SNES_MOUSE_V1_5_PROTOCOL
 * 32-bit serial format: 8x0, R, L, speed[1:0], 0001,
 * Y(sign+7-bit magnitude), X(sign+7-bit magnitude). */
static Int32 _SnesMouseClampRaw(Int32 v)
{
	if (v > 127) return 127;
	if (v < -127) return -127;
	return v;
}

static Uint32 _SnesMouseScaleMagnitude(Int32 raw, Uint8 speed)
{
	Uint32 mag = (Uint32)(raw < 0 ? -raw : raw);

	if (speed == 1)
		mag = (mag * 3U) / 2U;
	else if (speed == 2)
		mag *= 2U;

	if (mag > 127U)
		mag = 127U;
	return mag;
}

void SnesIO::UpdateMousePacketSpeed()
{
	m_uMousePacket &= ~(3U << 20);
	m_uMousePacket |= ((Uint32)(m_uMouseSpeed & 3U)) << 20;
}

void SnesIO::CaptureMousePacket()
{
	Int32 rawX = _SnesMouseClampRaw(m_nMousePendingX);
	Int32 rawY = _SnesMouseClampRaw(m_nMousePendingY);
	Uint32 magX;
	Uint32 magY;

	m_nMousePendingX -= rawX;
	m_nMousePendingY -= rawY;

	magX = _SnesMouseScaleMagnitude(rawX, m_uMouseSpeed);
	magY = _SnesMouseScaleMagnitude(rawY, m_uMouseSpeed);

	m_uMousePacket = 0x00010000U;
	if (m_uMouseHostButtons & 0x02U) m_uMousePacket |= 1U << 23;
	if (m_uMouseHostButtons & 0x01U) m_uMousePacket |= 1U << 22;
	UpdateMousePacketSpeed();

	if (rawY < 0) m_uMousePacket |= 1U << 15;
	m_uMousePacket |= (magY & 0x7fU) << 8;
	if (rawX < 0) m_uMousePacket |= 1U << 7;
	m_uMousePacket |= (magX & 0x7fU);

	m_uMouseReadIndex = 0;
}

Uint8 SnesIO::ReadSerialMouse0()
{
	Uint8 bit;

	if (m_Regs.joydata & 1)
	{
		m_uMouseSpeed = (Uint8)((m_uMouseSpeed + 1) % 3);
		UpdateMousePacketSpeed();
		return 0;
	}

	if (m_uMouseReadIndex >= 32)
		return 1;

	bit = (m_uMousePacket & (0x80000000U >> m_uMouseReadIndex)) ? 1 : 0;
	m_uMouseReadIndex++;
	return bit;
}

Uint16 SnesIO::GetMouseAutoWord() const
{
	Uint16 v = 0x0001;
	if (m_uMouseHostButtons & 0x01U) v |= 0x0040;
	if (m_uMouseHostButtons & 0x02U) v |= 0x0080;
	v |= (Uint16)((m_uMouseSpeed & 3U) << 4);
	return v;
}

void SnesIO::SetMouseInput(Bool bConnected, Int32 nDeltaX, Int32 nDeltaY, Uint32 uButtons)
{
	Bool bWasConnected = m_bMouse0Connected;

	m_bMouse0Connected = bConnected ? TRUE : FALSE;
	if (!m_bMouse0Connected)
	{
		m_nMousePendingX = 0;
		m_nMousePendingY = 0;
		m_uMouseHostButtons = 0;
		m_uMousePacket = 0;
		m_uMouseReadIndex = 0;
		if (bWasConnected)
			m_uMouseSpeed = 0;
		return;
	}

	if (!bWasConnected)
	{
		m_uMouseSpeed = 0;
		m_uMousePacket = 0;
		m_uMouseReadIndex = 0;
		m_nMousePendingX = 0;
		m_nMousePendingY = 0;
	}

	if (nDeltaX > 32767) nDeltaX = 32767;
	if (nDeltaX < -32767) nDeltaX = -32767;
	if (nDeltaY > 32767) nDeltaY = 32767;
	if (nDeltaY < -32767) nDeltaY = -32767;

	if (nDeltaX > 0 && m_nMousePendingX > 32767 - nDeltaX)
		m_nMousePendingX = 32767;
	else if (nDeltaX < 0 && m_nMousePendingX < -32767 - nDeltaX)
		m_nMousePendingX = -32767;
	else
		m_nMousePendingX += nDeltaX;

	if (nDeltaY > 0 && m_nMousePendingY > 32767 - nDeltaY)
		m_nMousePendingY = 32767;
	else if (nDeltaY < 0 && m_nMousePendingY < -32767 - nDeltaY)
		m_nMousePendingY = -32767;
	else
		m_nMousePendingY += nDeltaY;

	m_uMouseHostButtons = uButtons & 0x03U;
}

Uint8 SnesIO::ReadSerialPad(Uint32 uPad)
{
	// read top-most joypad bit
	return (m_Regs.joyserial[uPad] >> 15) & 1;
}

void SnesIO::ShiftSerialPad(Uint32 uPad)
{
	// shift pad data
	m_Regs.joyserial[uPad] <<= 1;

	// if joystick connected
	if (m_Input.uPad[uPad]!=EMUSYS_DEVICE_DISCONNECTED)
	{
		// set connected status
		m_Regs.joyserial[uPad] |= 1;
	}
}

Uint8 SnesIO::ReadSerial0()
{
	Uint32 uData;

	if (m_bMouse0Connected)
		return ReadSerialMouse0();

	uData  = ReadSerialPad(0) << 0;

	// confirmed:
	// if strobe is left on, then bitposition never shifts
	// all bits returned are button B
	if (!(m_Regs.joydata&1))
	{
		ShiftSerialPad(0);
	}

	return uData;
}

Uint8 SnesIO::ReadSerial1()
{
	Uint32 uData;

	// if joypads 2,3,4 are all disconnected then assume no multitap is installed
	if (
		m_Input.uPad[2]==EMUSYS_DEVICE_DISCONNECTED && 
		m_Input.uPad[3]==EMUSYS_DEVICE_DISCONNECTED && 
		m_Input.uPad[4]==EMUSYS_DEVICE_DISCONNECTED
		)
	{
		// no multitap!

		// read serial bit
		uData  = ReadSerialPad(1) << 0;

		// confirmed:
		// if strobe is left on, then bitposition never shifts
		// all bits returned are button B
		if (!(m_Regs.joydata&1))
		{
			ShiftSerialPad(1);
		}

	} else
	{
		// multitap

		// confirmed:
		// if stobe is left on, then bit is returned if multitap is connected
		if (m_Regs.joydata&1)
		{
			// signal presence of multitap
			uData = 0x02;
		}
		else
		{
			// multitap port enabled?
			if (m_Regs.wrio & 0x80)
			{
				// use controllers 2 and 3
				uData  = ReadSerialPad(1) << 0;
				uData |= ReadSerialPad(2) << 1; 

				ShiftSerialPad(1);
				ShiftSerialPad(2);
			} else
			{
				// use controllers 4 and 5
				uData  = ReadSerialPad(3) << 0;
				uData |= ReadSerialPad(4) << 1; 

				ShiftSerialPad(3);
				ShiftSerialPad(4);
			}
		}
	}

	// confirmed:
	// this port always returns with 1C bits on
	// havent tested with multitap yet though
	return uData | 0x1C;
}

void SnesIO::WriteSerial(Uint8 uData)
{
	Bool bOldStrobe = (m_Regs.joydata & 1) ? TRUE : FALSE;
	Bool bNewStrobe = (uData & 1) ? TRUE : FALSE;

	if (bNewStrobe && !bOldStrobe)
	{
		int iPad;

		for (iPad=0; iPad < SNESIO_DEVICE_NUM; iPad++)
		{
			if (iPad == 0 && m_bMouse0Connected)
			{
				m_Regs.joyserial[iPad] = 0;
			}
			else if (m_Input.uPad[iPad] != EMUSYS_DEVICE_DISCONNECTED)
			{
				m_Regs.joyserial[iPad] = m_Input.uPad[iPad] & 0xFFF0;
			} else
			{
				m_Regs.joyserial[iPad] = 0;
			}
		}
		if (m_bMouse0Connected)
			m_uMouseReadIndex = 0;
	}

	/* Capture on falling edge so sensitivity-cycle reads while strobe is high
	   affect this packet, matching bsnes' transition-based sampling. */
	if (!bNewStrobe && bOldStrobe && m_bMouse0Connected)
		CaptureMousePacket();

	m_Regs.joydata = uData;
}

// this function gets called about 3 scanlines after vblank, it performs reads from the serial
// port and loads them into each register
void SnesIO::UpdateJoyPads()
{

	// strobe joypads
	WriteSerial(0);
	WriteSerial(1);
	WriteSerial(0);

	if (m_bMouse0Connected)
		m_Regs.joy1.w = GetMouseAutoWord();
	else
		m_Regs.joy1.w = m_Regs.joyserial[0];
	m_Regs.joy2.w = m_Regs.joyserial[1];

	// multitap enabled?
	if (m_Regs.wrio & 0x80)
	{
		// ??
		m_Regs.joy3.w = m_Regs.joyserial[1];
		m_Regs.joy4.w = m_Regs.joyserial[2];
	} else
	{
		// ??
		m_Regs.joy3.w = m_Regs.joyserial[3];
		m_Regs.joy4.w = m_Regs.joyserial[4];
	}

	// perform dummy reads
	for (int i=0; i<16; i++)
	{
		ReadSerial0();
		ReadSerial1();
	}
}


SnesIO::SnesIO()
{
	Reset();
}

void SnesIO::Reset()
{
	memset(this, 0, sizeof(*this));
	m_Regs.rdnmi  =  SNIO_VERSION_5A22;
	/* AURORA_SNES_WRIO_POWERON_V4_20260822
	 * WRIO ($4201) powers on at $FF. Bit 7 enables the PPU /EXTLATCH
	 * sampled by reads of $2137. */
	m_Regs.wrio = 0xFF;
}

void SnesIO::LatchInput(Emu::SysInputT  *pInput)
{
	if (pInput)
	{
		m_Input = *pInput;
	} else
	{
		// not connected
		m_Input.uPad[0] = EMUSYS_DEVICE_DISCONNECTED;
		m_Input.uPad[1] = EMUSYS_DEVICE_DISCONNECTED;
		m_Input.uPad[2] = EMUSYS_DEVICE_DISCONNECTED;
		m_Input.uPad[3] = EMUSYS_DEVICE_DISCONNECTED;
		m_Input.uPad[4] = EMUSYS_DEVICE_DISCONNECTED;
	}
}
