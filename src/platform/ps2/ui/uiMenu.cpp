#include <stdlib.h>
#include <string.h>
#include <kernel.h>
#include <libpad.h>
#include "types.h"
#if 0
#include "font.h"
#else
#include "font.h"
#endif
#include "poly.h"
#include "uiMenu.h"


void CMenuScreen::SetEntries(char **ppStrings)
{
	/* AURORA_RUNTIME_SAFE_MENU_V1_4_1
	 * Empty/dynamic menus must never leave a -1 or stale selection. */
	m_nItems = 0;
	if (!ppStrings)
	{
		m_iSelect = 0;
		return;
	}

	while (*ppStrings && m_nItems < 32)
	{
		m_pEntries[m_nItems++] = *ppStrings;
		ppStrings++;
	}

	if (m_nItems <= 0)
		m_iSelect = 0;
	else
	{
		if (m_iSelect < 0) m_iSelect = 0;
		if (m_iSelect >= m_nItems) m_iSelect = m_nItems - 1;
	}
}

CMenuScreen::CMenuScreen()
{
	m_iSelect = 0;
	m_nItems  = 0;
	m_iTop    = 40;
	m_bHorizontal = FALSE;
	m_pUserData = NULL;
	memset(m_strText, 0, sizeof(m_strText));
	m_strTitle[0] = 0;
///	SetEntries(_TestStr);
//	SetText(0, "crapppy");
}

void CMenuScreen::SetTitle(const char *pTitle)
{
	/* AURORA_RUNTIME_SAFE_MENU_STRINGS_V1_4_2 */
	if (!pTitle)
		pTitle = "";
	strncpy(m_strTitle, pTitle, sizeof(m_strTitle) - 1);
	m_strTitle[sizeof(m_strTitle) - 1] = '\0';
}

void CMenuScreen::SetText(int iText, const char *pStr)
{
	if (iText < 0 || iText >= 4)
		return;
	if (!pStr)
		pStr = "";
	strncpy(m_strText[iText], pStr, sizeof(m_strText[iText]) - 1);
	m_strText[iText][sizeof(m_strText[iText]) - 1] = '\0';
}

void CMenuScreen::SetSelection(Int32 iSelect)
{
	if (m_nItems <= 0)
	{
		m_iSelect = 0;
		return;
	}

	if (iSelect < 0) iSelect = 0;
	if (iSelect >= m_nItems) iSelect = m_nItems - 1;
	m_iSelect = iSelect;
}

static void _MenuPrintAlignCenter(int x, int y, const char *str, Bool bHighlight = FALSE)
{                
    x-= FontGetStrWidth(str) / 2;
    FontPuts(x, y, str);

    if (bHighlight)
    {
		PolyColor4f(0.0f, 1.0f, 0.0f, 0.5f); 
		PolyRect(x-1, y-1, FontGetStrWidth(str) + 2, FontGetHeight() + 2);
    }
}

static void _MenuHeader(int vy, const char *str)
{
    PolyColor4f(0.0f, 0.2f, 0.2f, 0.5f); 
	PolyRect(32, vy, 256-64, 9);
	FontColor4f(0.0, 0.8f, 0.8f, 1.0f);
    _MenuPrintAlignCenter(128, vy, str);
}

void CMenuScreen::Draw()
{
	Int32 iLine;
	Int32 vx=128, vy = 40;

	FontSelect(0);
	FontColor4f(0.0, 0.8f, 0.8f, 1.0f);
//    FontPrintf(vx, vy, "%s", "Testing!");

	vx -= 80;

	/* The title may move independently, while entries and help text retain
	   the established layout shared by the other menu screens. */
	_MenuHeader(m_iTop, m_strTitle);
	vy+=FontGetHeight() * 2;

	FontColor4f(1.0, 1.0f, 1.0f, 1.0f);

	if (m_bHorizontal)
	{
		Int32 total = 0;
		const Int32 gap = 24;
		for (iLine = 0; iLine < m_nItems; iLine++)
			if (m_pEntries[iLine]) total += FontGetStrWidth(m_pEntries[iLine]);
		if (m_nItems > 1) total += gap * (m_nItems - 1);

		Int32 hx = 128 - total / 2;
		for (iLine = 0; iLine < m_nItems; iLine++)
		{
			Char *pStr = m_pEntries[iLine];
			if (pStr)
			{
				Int32 width = FontGetStrWidth(pStr);
				FontPuts(hx, vy, pStr);
				if (iLine == m_iSelect)
				{
					PolyColor4f(0.0f, 1.0f, 0.0f, 0.5f);
					PolyRect(hx-1, vy-1, width + 2, FontGetHeight() + 2);
				}
				hx += width + gap;
			}
		}
		vy += FontGetHeight() + 2;
	}
	else
	{
		for (iLine=0; iLine < m_nItems; iLine++)
		{
			Char *pStr = m_pEntries[iLine];
			Int32 iWidth;
			iWidth = FontGetStrWidth(pStr);
			if (pStr)
			{
				FontPuts(vx, vy, pStr);
				if (iLine == m_iSelect)
				{
					PolyColor4f(0.0f, 1.0f, 0.0f, 0.5f);
					PolyRect(vx-1, vy-1, iWidth + 2, FontGetHeight() + 2);
				}
			}
			vy += FontGetHeight() + 2;
		}
	}

	vy+=FontGetHeight();

	_MenuHeader(vy, "");
	vy+=FontGetHeight() * 2;

	for (int i=0; i < 4; i++)
	{
		FontColor4f(0.8, 0.8f, 0.8f, 1.0f);
//		FontPuts(vx, vy, m_strText[i]);
		_MenuPrintAlignCenter(128, vy,  m_strText[i]);
		vy+=FontGetHeight() + 2;
	}
}

void CMenuScreen::Process()
{
}

void CMenuScreen::Input(Uint32 buttons, Uint32 trigger)
{
	(void)buttons;
	if (m_nItems <= 0)
	{
		m_iSelect = 0;
		return;
	}
	if (m_bHorizontal)
	{
		if (trigger & PAD_LEFT)  m_iSelect--;
		if (trigger & PAD_RIGHT) m_iSelect++;
	}
	else
	{
		if (trigger & PAD_UP)   m_iSelect--;
		if (trigger & PAD_DOWN) m_iSelect++;
	}
	if (m_iSelect < 0) m_iSelect = 0;
	if (m_iSelect > (m_nItems - 1)) m_iSelect = (m_nItems - 1);

	if (trigger & (m_bHorizontal ? PAD_CROSS : (PAD_CROSS | PAD_START)))
		SendMessage(1, m_iSelect, m_pUserData);
}

