/*===================================================================*/
/*                                                                   */
/*  InfoNES_pAPU.cpp : InfoNES Sound Emulation Function              */
/*                                                                   */
/*  2000/05/29  InfoNES Project ( based on DarcNES and NesterJ )     */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/*  Include files                                                    */
/*-------------------------------------------------------------------*/
#include "K6502.h"
#include "K6502_rw.h"
#include "InfoNES_System.h"
#include "InfoNES_pAPU.h"

/*-------------------------------------------------------------------*/
/*   APU Event resources                                             */
/*-------------------------------------------------------------------*/

struct ApuEvent_t ApuEventQueue[ APU_EVENT_MAX ];
int  cur_event;
WORD entertime;

/*-------------------------------------------------------------------*/
/*   APU Register Write Functions                                    */
/*-------------------------------------------------------------------*/

#define APU_WRITEFUNC(name, evtype) \
void ApuWrite##name(WORD addr, BYTE value) \
{ \
  if ( cur_event >= APU_EVENT_MAX ) return; \
  ApuEventQueue[cur_event].time = (WORD)( K6502_GetPassedClocks() - entertime ); \
  ApuEventQueue[cur_event].type = APUET_W_##evtype; \
  ApuEventQueue[cur_event].data = value; \
  cur_event++; \
}

APU_WRITEFUNC(C1a, C1A);
APU_WRITEFUNC(C1b, C1B);
APU_WRITEFUNC(C1c, C1C);
APU_WRITEFUNC(C1d, C1D);

APU_WRITEFUNC(C2a, C2A);
APU_WRITEFUNC(C2b, C2B);
APU_WRITEFUNC(C2c, C2C);
APU_WRITEFUNC(C2d, C2D);

APU_WRITEFUNC(C3a, C3A);
APU_WRITEFUNC(C3b, C3B);
APU_WRITEFUNC(C3c, C3C);
APU_WRITEFUNC(C3d, C3D);

APU_WRITEFUNC(C4a, C4A);
APU_WRITEFUNC(C4b, C4B);
APU_WRITEFUNC(C4c, C4C);
APU_WRITEFUNC(C4d, C4D);

APU_WRITEFUNC(C5a, C5A);
APU_WRITEFUNC(C5b, C5B);
APU_WRITEFUNC(C5c, C5C);
APU_WRITEFUNC(C5d, C5D);

APU_WRITEFUNC(Control, CTRL);

ApuWritefunc pAPUSoundRegs[20] = 
{
  ApuWriteC1a,
  ApuWriteC1b,
  ApuWriteC1c,
  ApuWriteC1d,
  ApuWriteC2a,
  ApuWriteC2b,
  ApuWriteC2c,
  ApuWriteC2d,
  ApuWriteC3a,
  ApuWriteC3b,
  ApuWriteC3c,
  ApuWriteC3d,
  ApuWriteC4a,
  ApuWriteC4b,
  ApuWriteC4c,
  ApuWriteC4d,
  ApuWriteC5a,
  ApuWriteC5b,
  ApuWriteC5c,
  ApuWriteC5d,
};

/*-------------------------------------------------------------------*/
/*   APU resources                                                   */
/*-------------------------------------------------------------------*/

BYTE wave_buffers[5][735];      /* 44100 / 60 = 735 samples per sync */

BYTE ApuCtrl;
BYTE ApuCtrlNew;

/*-------------------------------------------------------------------*/
/*   APU Quality resources                                           */
/*-------------------------------------------------------------------*/

int ApuQuality;

DWORD ApuPulseMagic;
DWORD ApuTriangleMagic;
DWORD ApuNoiseMagic;
unsigned int ApuSamplesPerSync;
unsigned int ApuCyclesPerSample;
unsigned int ApuSampleRate;
DWORD ApuCycleRate;

struct ApuQualityData_t 
{
  DWORD pulse_magic;
  DWORD triangle_magic;
  DWORD noise_magic;
  unsigned int samples_per_sync;
  unsigned int cycles_per_sample;
  unsigned int sample_rate;
  DWORD cycle_rate;
} ApuQual[] = {
  { 0xa2567000, 0xa2567000, 0xa2567000, 183, 164, 11025, 1062658 },
  { 0x512b3800, 0x512b3800, 0x512b3800, 367,  82, 22050, 531329 },
  /* 1789773 NTSC CPU clocks / 44100 Hz, in 16.16 fixed point.  The
     previous value missed one decimal digit and made DPCM ~10x slow. */
  { 0x289d9c00, 0x289d9c00, 0x289d9c00, 735,  41, 44100, 2659741 },
};

/*-------------------------------------------------------------------*/
/*  Rectangle Wave #1 resources                                      */
/*-------------------------------------------------------------------*/
BYTE ApuC1a, ApuC1b, ApuC1c, ApuC1d;

BYTE* ApuC1Wave;
DWORD ApuC1Skip;
DWORD ApuC1Index;
int   ApuC1EnvPhase;
BYTE  ApuC1EnvVol;
BYTE  ApuC1Atl;
int   ApuC1SweepPhase;
DWORD ApuC1Freq;   

/*-------------------------------------------------------------------*/
/*  Rectangle Wave #2 resources                                      */
/*-------------------------------------------------------------------*/
BYTE ApuC2a, ApuC2b, ApuC2c, ApuC2d;

BYTE* ApuC2Wave;
DWORD ApuC2Skip;
DWORD ApuC2Index;
int   ApuC2EnvPhase;
BYTE  ApuC2EnvVol;
BYTE  ApuC2Atl;   
int   ApuC2SweepPhase;
DWORD ApuC2Freq;   

/*-------------------------------------------------------------------*/
/*  Triangle Wave resources                                          */
/*-------------------------------------------------------------------*/
BYTE ApuC3a, ApuC3b, ApuC3c, ApuC3d;

DWORD ApuC3Skip;
DWORD ApuC3Index;
BYTE  ApuC3Atl;
DWORD ApuC3Llc;                             /* Linear Length Counter */
BYTE  ApuC3ReloadFlag;

/*-------------------------------------------------------------------*/
/*  Noise resources                                                  */
/*-------------------------------------------------------------------*/
BYTE ApuC4a, ApuC4b, ApuC4c, ApuC4d;

DWORD ApuC4Sr;                                     /* Shift register */
DWORD ApuC4Fdc;                          /* Frequency divide counter */
DWORD ApuC4Skip;
DWORD ApuC4Index;
BYTE  ApuC4Atl;
BYTE  ApuC4EnvVol;
int   ApuC4EnvPhase;

/*-------------------------------------------------------------------*/
/*  DPCM resources                                                   */
/*-------------------------------------------------------------------*/
BYTE  ApuC5Reg[4];
BYTE  ApuC5Enable;
BYTE  ApuC5Looping;
BYTE  ApuC5CurByte;
BYTE  ApuC5DpcmValue;

int   ApuC5Freq;
int   ApuC5Phaseacc;

WORD  ApuC5Address, ApuC5CacheAddr;
int   ApuC5DmaLength, ApuC5CacheDmaLength;

/*-------------------------------------------------------------------*/
/*  Wave Data                                                        */
/*-------------------------------------------------------------------*/
BYTE pulse_25[0x20] = {
  0x11, 0x11, 0x11, 0x11,
  0x11, 0x11, 0x11, 0x11,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
};

BYTE pulse_50[0x20] = {
  0x11, 0x11, 0x11, 0x11,
  0x11, 0x11, 0x11, 0x11,
  0x11, 0x11, 0x11, 0x11,
  0x11, 0x11, 0x11, 0x11,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
};

BYTE pulse_75[0x20] = {
  0x11, 0x11, 0x11, 0x11,
  0x11, 0x11, 0x11, 0x11,
  0x11, 0x11, 0x11, 0x11,
  0x11, 0x11, 0x11, 0x11,
  0x11, 0x11, 0x11, 0x11,
  0x11, 0x11, 0x11, 0x11,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
};

BYTE pulse_87[0x20] = {
  0x11, 0x11, 0x11, 0x11,
  0x11, 0x11, 0x11, 0x11,
  0x11, 0x11, 0x11, 0x11,
  0x11, 0x11, 0x11, 0x11,
  0x11, 0x11, 0x11, 0x11,
  0x11, 0x11, 0x11, 0x11,
  0x11, 0x11, 0x11, 0x11,
  0x00, 0x00, 0x00, 0x00,
};

BYTE triangle_50[0x20] = {
  0x00, 0x10, 0x20, 0x30,
  0x40, 0x50, 0x60, 0x70,
  0x80, 0x90, 0xa0, 0xb0,
  0xc0, 0xd0, 0xe0, 0xf0,
  0xff, 0xef, 0xdf, 0xcf,
  0xbf, 0xaf, 0x9f, 0x8f,
  0x7f, 0x6f, 0x5f, 0x4f,
  0x3f, 0x2f, 0x1f, 0x0f,
};

BYTE *pulse_waves[4] = {
  pulse_87, pulse_75, pulse_50, pulse_25,
};

/*-------------------------------------------------------------------*/
/*  Active Time Left Data                                            */
/*-------------------------------------------------------------------*/
BYTE ApuAtl[0x20] = 
{
  5, 127, 10, 1, 19,  2, 40,  3, 80,  4, 30,  5, 7,  6, 13,  7,
  6,   8, 12, 9, 24, 10, 48, 11, 96, 12, 36, 13, 8, 14, 16, 15,
};

/*-------------------------------------------------------------------*/
/* Frequency Limit of Rectangle Channels                             */
/*-------------------------------------------------------------------*/
WORD ApuFreqLimit[8] = 
{
   0x3FF, 0x555, 0x666, 0x71C, 0x787, 0x7C1, 0x7E0, 0x7F0
};

/*-------------------------------------------------------------------*/
/* Noise Frequency Lookup Table                                      */
/*-------------------------------------------------------------------*/
DWORD ApuNoiseFreq[ 16 ] =
{
     4,    8,   16,   32,   64,   96,  128,  160,
   202,  254,  380,  508,  762, 1016, 2034, 4068
};

/*-------------------------------------------------------------------*/
/* DMC Transfer Clocks Table                                          */
/*-------------------------------------------------------------------*/
DWORD ApuDpcmCycles[ 16 ] = 
{
  428, 380, 340, 320, 286, 254, 226, 214,
  190, 160, 142, 128, 106,  85,  72,  54
};

/*===================================================================*/
/*                                                                   */
/*      ApuRenderingWave1() : Rendering Rectangular Wave #1          */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/* Write registers of rectangular wave #1                            */
/*-------------------------------------------------------------------*/

int ApuWriteWave1( int cycles, int event )
{
    /* APU Reg Write Event */
    while ( ( event < cur_event ) && ( ApuEventQueue[event].time < cycles ) ) 
    {
      if ( ( ApuEventQueue[event].type & APUET_MASK ) == APUET_C1 ) 
      {
	switch ( ApuEventQueue[event].type & 0x03 ) 
        {
	case 0:
	  ApuC1a    = ApuEventQueue[event].data;
	  ApuC1Wave = pulse_waves[ ApuC1DutyCycle >> 6 ];
	  break;

	case 1:
	  ApuC1b    = ApuEventQueue[event].data; 
	  break;
	  
	case 2:
	  ApuC1c = ApuEventQueue[event].data;
	  ApuC1Freq = ( ( ( (WORD)ApuC1d & 0x07 ) << 8 ) + ApuC1c );
	  
	  if ( ApuC1Freq ) 
          {
	    ApuC1Skip = ( ApuPulseMagic << 1 ) / ( ApuC1Freq + 1 );
	  } else {
	    ApuC1Skip = 0;
	  }
	  break;

	case 3:
	  ApuC1d = ApuEventQueue[event].data;
	  ApuC1Freq = ( ( ( (WORD)ApuC1d & 0x07 ) << 8 ) + ApuC1c );
	  ApuC1Atl = ApuAtl[ ( ApuC1d & 0xf8 ) >> 3 ];
	  ApuC1EnvVol = 15;
	  
	  if ( ApuC1Freq ) 
          {
	    ApuC1Skip = ( ApuPulseMagic << 1 ) / ( ApuC1Freq + 1 );
	  } else {
	    ApuC1Skip = 0;
	  }
	  break;
	}
      } 
      else if ( ApuEventQueue[event].type == APUET_W_CTRL ) 
      {
	ApuCtrlNew = ApuEventQueue[event].data;

	if( !(ApuEventQueue[event].data&(1<<0)) ) {
	  ApuC1Atl = 0;
	}
      }
      event++;
    }
    return event;
}

/*-------------------------------------------------------------------*/
/* Rendering rectangular wave #1                                     */
/*-------------------------------------------------------------------*/

void ApuRenderingWave1( void )
{
  int cycles = 0;
  int event = 0;

  /* note: 41 CPU cycles occur between increments of i */
  ApuCtrlNew = ApuCtrl;
  for ( unsigned int i = 0; i < ApuSamplesPerSync; i++ ) 
  {
    /* Write registers */
    cycles += ApuCyclesPerSample;
    event = ApuWriteWave1( cycles, event );

    /*
     * TODO: using a table of max frequencies is not technically
     * clean, but it is fast and (or should be) accurate
     */
    if ( ApuC1Freq < 8 || ( !ApuC1SweepIncDec && ApuC1Freq > ApuC1FreqLimit ) )
    {
      wave_buffers[0][i] = 0;
      continue;
    }

    /* Wave Rendering */
    if ( ( ApuCtrlNew & 0x01 ) && ApuC1Atl )
    {
      BYTE vol = ApuC1Env ? ApuC1Vol : ApuC1EnvVol;
      ApuC1Index += ApuC1Skip;
      ApuC1Index &= 0x1fffffff;
      wave_buffers[0][i] = ApuC1Wave[ApuC1Index >> 24] ? vol : 0;
    } else {
      wave_buffers[0][i] = 0;
    }
  }
}

/*===================================================================*/
/*                                                                   */
/*      ApuRenderingWave2() : Rendering Rectangular Wave #2          */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/* Write registers of rectangular wave #2                           */
/*-------------------------------------------------------------------*/

int ApuWriteWave2( int cycles, int event )
{
    /* APU Reg Write Event */
    while ( ( event < cur_event ) && ( ApuEventQueue[event].time < cycles ) ) 
    {
      if ( ( ApuEventQueue[event].type & APUET_MASK ) == APUET_C2 ) 
      {
	switch ( ApuEventQueue[event].type & 0x03 ) 
        {
	case 0:
	  ApuC2a    = ApuEventQueue[event].data;
	  ApuC2Wave = pulse_waves[ ApuC2DutyCycle >> 6 ];
	  break;

	case 1:
	  ApuC2b    = ApuEventQueue[event].data; 
	  break;
	  
	case 2:
	  ApuC2c = ApuEventQueue[event].data;
	  ApuC2Freq = ( ( ( (WORD)ApuC2d & 0x07 ) << 8 ) + ApuC2c );
	  
	  if ( ApuC2Freq ) 
          {
	    ApuC2Skip = ( ApuPulseMagic << 1 ) / ( ApuC2Freq + 1 );
	  } else {
	    ApuC2Skip = 0;
	  }
	  break;

	case 3:
	  ApuC2d = ApuEventQueue[event].data;
	  ApuC2Freq = ( ( ( (WORD)ApuC2d & 0x07 ) << 8 ) + ApuC2c );
	  ApuC2Atl = ApuAtl[ ( ApuC2d & 0xf8 ) >> 3 ];
	  ApuC2EnvVol = 15;
	  
	  if ( ApuC2Freq ) 
          {
	    ApuC2Skip = ( ApuPulseMagic << 1 ) / ( ApuC2Freq + 1 );
	  } else {
	    ApuC2Skip = 0;
	  }
	  break;
	}
      } 
      else if ( ApuEventQueue[event].type == APUET_W_CTRL ) 
      {
	ApuCtrlNew = ApuEventQueue[event].data;

	if( !(ApuEventQueue[event].data&(1<<1)) ) {
	  ApuC2Atl = 0;
	}
      }
      event++;
    }
    return event;
}

/*-------------------------------------------------------------------*/
/* Rendering rectangular wave #2                                     */
/*-------------------------------------------------------------------*/

void ApuRenderingWave2( void )
{
  int cycles = 0;
  int event = 0;

  /* note: 41 CPU cycles occur between increments of i */
  ApuCtrlNew = ApuCtrl;
  for ( unsigned int i = 0; i < ApuSamplesPerSync; i++ ) 
  {
    /* Write registers */
    cycles += ApuCyclesPerSample;
    event = ApuWriteWave2( cycles, event );

    /*
     * TODO: using a table of max frequencies is not technically
     * clean, but it is fast and (or should be) accurate
     */
    if ( ApuC2Freq < 8 || ( !ApuC2SweepIncDec && ApuC2Freq > ApuC2FreqLimit ) )
    {
      wave_buffers[1][i] = 0;
      continue;
    }

    /* Wave Rendering */
    if ( ( ApuCtrlNew & 0x02 ) && ApuC2Atl )
    {
      BYTE vol = ApuC2Env ? ApuC2Vol : ApuC2EnvVol;
      ApuC2Index += ApuC2Skip;
      ApuC2Index &= 0x1fffffff;
      wave_buffers[1][i] = ApuC2Wave[ApuC2Index >> 24] ? vol : 0;
    } else {
      wave_buffers[1][i] = 0;
    }
  }
}

/*===================================================================*/
/*                                                                   */
/*      ApuRenderingWave3() : Rendering Triangle Wave                */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/* Write registers of triangle wave #3                              */
/*-------------------------------------------------------------------*/

int ApuWriteWave3( int cycles, int event )
{
  /* APU Reg Write Event */
  while (( event < cur_event ) && ( ApuEventQueue[event].time < cycles ) ) 
  {
    if ( ( ApuEventQueue[event].type & APUET_MASK ) == APUET_C3 ) 
    {
      switch ( ApuEventQueue[event].type & 3 ) 
      {
      case 0:
	ApuC3a = ApuEventQueue[event].data;
	break;

      case 1:
	ApuC3b = ApuEventQueue[event].data;
	break;

      case 2:
	ApuC3c = ApuEventQueue[event].data;
	if ( ApuC3Freq ) 
        {
	  ApuC3Skip = ApuTriangleMagic / ( ApuC3Freq + 1 );
	} else {
	  ApuC3Skip = 0;  
	}
	break;

      case 3:
	ApuC3d = ApuEventQueue[event].data;
	ApuC3Atl = ApuC3LengthCounter;
	ApuC3ReloadFlag = 1;
	if ( ApuC3Freq ) 
	{
	  ApuC3Skip = ApuTriangleMagic / ( ApuC3Freq + 1 );
	} else {
	  ApuC3Skip = 0;
	}
      }
    } else if ( ApuEventQueue[event].type == APUET_W_CTRL ) {
      ApuCtrlNew = ApuEventQueue[event].data;

      if( !(ApuEventQueue[event].data&(1<<2)) ) {
	ApuC3Atl = 0;
	ApuC3Llc = 0;
      }
    }
    event++;
  }
  return event;
}

/*-------------------------------------------------------------------*/
/* Rendering triangle wave #3                                        */
/*-------------------------------------------------------------------*/

void ApuRenderingWave3( void )
{
  int cycles = 0;
  int event = 0;
      
  /* note: 41 CPU cycles occur between increments of i */
  ApuCtrlNew = ApuCtrl;
  for ( unsigned int i = 0; i < ApuSamplesPerSync; i++) 
  {
    /* Write registers */
    cycles += ApuCyclesPerSample;
    event = ApuWriteWave3( cycles, event );

    /* Cutting Min Frequency */
    if ( ApuC3Freq < 8 )
    {
      wave_buffers[2][i] = 0;
      continue;
    }

    /* Wave Rendering */
    if ( ( ApuCtrlNew & 0x04 ) && ApuC3Atl > 0 && ApuC3Llc > 0 )
    {
      ApuC3Index += ApuC3Skip;
      ApuC3Index &= 0x1fffffff;
      wave_buffers[2][i] = triangle_50[ ApuC3Index >> 24 ] >> 4;
    } else {
      wave_buffers[2][i] = 0;
    }
  }
}

/*===================================================================*/
/*                                                                   */
/*      ApuRenderingWave4() : Rendering Noise                        */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/* Write registers of noise channel #4                              */
/*-------------------------------------------------------------------*/

int ApuWriteWave4( int cycles, int event )
{
  /* APU Reg Write Event */
  while ( (event < cur_event) && (ApuEventQueue[event].time < cycles) ) 
  {
    if ( ( ApuEventQueue[event].type & APUET_MASK ) == APUET_C4 ) 
    {
      switch (ApuEventQueue[event].type & 3) {
      case 0:
	ApuC4a = ApuEventQueue[event].data;
	break;

      case 1:
	ApuC4b = ApuEventQueue[event].data;
	break;

      case 2:
	ApuC4c = ApuEventQueue[event].data;

	/* Frequency */ 
	if ( ApuC4Freq ) {
	  ApuC4Skip = ApuNoiseMagic / ApuC4Freq;
	} else {
	  ApuC4Skip = 0;
	}
	break;

      case 3:
	ApuC4d = ApuEventQueue[event].data;
	ApuC4EnvVol = 15;

	/* Frequency */ 
	if ( ApuC4Freq ) {
	  ApuC4Skip = ApuNoiseMagic / ApuC4Freq;
	} else {
	  ApuC4Skip = 0;
	}
	ApuC4Atl = ApuC4LengthCounter;
      }
    } else if (ApuEventQueue[event].type == APUET_W_CTRL) {
      ApuCtrlNew = ApuEventQueue[event].data;

      if( !(ApuEventQueue[event].data&(1<<3)) ) {
	ApuC4Atl = 0;
      }
    } 
    event++;
  }
  return event;
}

/*-------------------------------------------------------------------*/
/* Rendering noise channel #4                                        */
/*-------------------------------------------------------------------*/

void ApuRenderingWave4(void)
{
  int cycles = 0;
  int event = 0;

  ApuCtrlNew = ApuCtrl;
  for ( unsigned int i = 0; i < ApuSamplesPerSync; i++ ) 
  {
    /* Write registers */
    cycles += ApuCyclesPerSample;
    event = ApuWriteWave4( cycles, event );

    /* Wave Rendering */
    if ( ( ApuCtrlNew & 0x08 ) && ApuC4Atl )
    {
      int shift = ApuC4Small ? 6 : 1;
      ApuC4Index += ApuC4Skip;
      while ( ApuC4Index >= 0x01000000 )
      {
	DWORD feedback = ( ApuC4Sr ^ ( ApuC4Sr >> shift ) ) & 1;
	ApuC4Sr = ( ApuC4Sr >> 1 ) | ( feedback << 14 );
	ApuC4Index -= 0x01000000;
      }

      if ( !( ApuC4Sr & 1 ) )
      {
        wave_buffers[3][i] = ApuC4Env ? ApuC4Vol : ApuC4EnvVol;
      } else {
	wave_buffers[3][i] = 0;
      }
    } else {
      wave_buffers[3][i] = 0;
    }
  }
}

/*===================================================================*/
/*                                                                   */
/*      ApuRenderingWave5() : Rendering DPCM channel #5              */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/* Write registers of DPCM channel #5                               */
/*-------------------------------------------------------------------*/

int ApuWriteWave5( int cycles, int event )
{
  /* APU Reg Write Event */
  while ( (event < cur_event) && (ApuEventQueue[event].time < cycles) ) 
  {
    if ( ( ApuEventQueue[event].type & APUET_MASK ) == APUET_C5 ) 
    {
      ApuC5Reg[ ApuEventQueue[event].type & 3 ] = ApuEventQueue[event].data;

      switch (ApuEventQueue[event].type & 3) {
      case 0:
	ApuC5Freq    = ApuDpcmCycles[ ( ApuEventQueue[event].data & 0x0F ) ] << 16;
	ApuC5Looping = ApuEventQueue[event].data & 0x40;
	break;
      case 1:
	/* $4011 is a seven-bit direct DAC. */
	ApuC5DpcmValue = ApuEventQueue[event].data & 0x7F;
	break;
      case 2:
	ApuC5CacheAddr = 0xC000 + (WORD)( ApuEventQueue[event].data << 6 );
	break;
      case 3:
	ApuC5CacheDmaLength = ( ( ApuEventQueue[event].data << 4 ) + 1 ) << 3;
	break;
      }
    } else if (ApuEventQueue[event].type == APUET_W_CTRL) {
      ApuCtrlNew = ApuEventQueue[event].data;

      if( !(ApuEventQueue[event].data&(1<<4)) ) {
	ApuC5Enable    = 0;
	ApuC5DmaLength = 0;
      } else {
	ApuC5Enable = 0xFF;
	if( !ApuC5DmaLength ) {
	  ApuC5Address   = ApuC5CacheAddr;
	  ApuC5DmaLength = ApuC5CacheDmaLength;
	}
      }
    }
    event++;
  }
  return event;
}

/*-------------------------------------------------------------------*/
/* Rendering DPCM channel #5                                         */
/*-------------------------------------------------------------------*/

void ApuRenderingWave5(void)
{
  int cycles = 0;
  int event = 0;

  ApuCtrlNew = ApuCtrl;
  for ( unsigned int i = 0; i < ApuSamplesPerSync; i++ ) 
  {
    /* Write registers */
    cycles += ApuCyclesPerSample;
    event = ApuWriteWave5( cycles, event );

    if( ( ApuCtrlNew & 0x10 ) && ApuC5DmaLength && ApuC5Freq > 0 ) {
      ApuC5Phaseacc -= ApuCycleRate;

      while( ApuC5Phaseacc < 0 ) {
	ApuC5Phaseacc += ApuC5Freq;
	if( !( ApuC5DmaLength & 7 ) ) {
	  ApuC5CurByte = K6502_Read( ApuC5Address );
	  if( 0xFFFF == ApuC5Address )
	    ApuC5Address = 0x8000;
	  else
	    ApuC5Address++;
	}
	--ApuC5DmaLength;

	/* DPCM changes the seven-bit DAC by two.  Apply the last bit before
	   deciding whether the sample ends; the old order silently dropped
	   every eighth bit. */
	if( ApuC5CurByte & ( 1 << ((ApuC5DmaLength&7)^7)) ) {
	  if( ApuC5DpcmValue <= 125 )
	    ApuC5DpcmValue += 2;
	} else {
	  if( ApuC5DpcmValue >= 2 )
	    ApuC5DpcmValue -= 2;
	}

	if( !ApuC5DmaLength ) {
	  if( ApuC5Looping ) {
	    ApuC5Address = ApuC5CacheAddr;
	    ApuC5DmaLength = ApuC5CacheDmaLength;
	  } else {
	    ApuC5Enable = 0;
	    break;
	  }
	}
      }
    }

    /* $4011 direct DAC output remains audible even when DMA is disabled. */
    wave_buffers[4][i] = ApuC5DpcmValue;
  }
}


/*===================================================================*/
/*                                                                   */
/*     InfoNES_pApuVsync() : Callback Function per Vsync             */
/*                                                                   */
/*===================================================================*/

/* The legacy renderer clocked envelopes, sweeps and the triangle linear
   counter once for every PCM sample (735 times per frame).  Apart from being
   needlessly expensive, that makes instruments vanish almost instantly.
   InfoNES models the 240/120 Hz frame sequencer in four/two ticks per Vsync. */
static void ApuClockFrameSequencer( void )
{
  DWORD delta;

  if ( ApuC1Atl && !ApuC1Hold )
    ApuC1Atl--;

  ApuC1EnvPhase -= 4;
  while ( ApuC1EnvPhase < 0 )
  {
    ApuC1EnvPhase += ApuC1EnvDelay;
    if ( ApuC1Hold )
      ApuC1EnvVol = ( ApuC1EnvVol - 1 ) & 0x0f;
    else if ( ApuC1EnvVol > 0 )
      ApuC1EnvVol--;
  }

  if ( ApuC1SweepOn && ApuC1SweepShifts )
  {
    ApuC1SweepPhase -= 2;
    while ( ApuC1SweepPhase < 0 )
    {
      ApuC1SweepPhase += ApuC1SweepDelay;
      delta = ApuC1Freq >> ApuC1SweepShifts;
      if ( ApuC1SweepIncDec )
      {
        /* Pulse 1 uses one's-complement negation (one extra subtraction). */
        ApuC1Freq = ( ApuC1Freq > delta ) ? ApuC1Freq - delta - 1 : 0;
      }
      else
      {
        if ( ApuC1Freq + delta <= 0x7ff )
          ApuC1Freq += delta;
      }
    }
    ApuC1Skip = ApuC1Freq
        ? ( ApuPulseMagic << 1 ) / ( ApuC1Freq + 1 ) : 0;
  }

  if ( ApuC2Atl && !ApuC2Hold )
    ApuC2Atl--;

  ApuC2EnvPhase -= 4;
  while ( ApuC2EnvPhase < 0 )
  {
    ApuC2EnvPhase += ApuC2EnvDelay;
    if ( ApuC2Hold )
      ApuC2EnvVol = ( ApuC2EnvVol - 1 ) & 0x0f;
    else if ( ApuC2EnvVol > 0 )
      ApuC2EnvVol--;
  }

  if ( ApuC2SweepOn && ApuC2SweepShifts )
  {
    ApuC2SweepPhase -= 2;
    while ( ApuC2SweepPhase < 0 )
    {
      ApuC2SweepPhase += ApuC2SweepDelay;
      delta = ApuC2Freq >> ApuC2SweepShifts;
      if ( ApuC2SweepIncDec )
        ApuC2Freq = ( ApuC2Freq >= delta ) ? ApuC2Freq - delta : 0;
      else if ( ApuC2Freq + delta <= 0x7ff )
        ApuC2Freq += delta;
    }
    ApuC2Skip = ApuC2Freq
        ? ( ApuPulseMagic << 1 ) / ( ApuC2Freq + 1 ) : 0;
  }

  if ( ApuC3ReloadFlag )
  {
    ApuC3Llc = ApuC3LinearLength;
  }
  else if ( ApuC3Llc > 0 )
  {
    ApuC3Llc = ( ApuC3Llc > 4 * 64 ) ? ApuC3Llc - 4 * 64 : 0;
  }
  if ( !ApuC3Holdnote )
    ApuC3ReloadFlag = 0;
  if ( ApuC3Atl > 0 && !ApuC3Holdnote )
    ApuC3Atl--;

  if ( ApuC4Atl && !ApuC4Hold )
    ApuC4Atl--;

  ApuC4EnvPhase -= 4;
  while ( ApuC4EnvPhase < 0 )
  {
    ApuC4EnvPhase += ApuC4EnvDelay;
    if ( ApuC4Hold )
      ApuC4EnvVol = ( ApuC4EnvVol - 1 ) & 0x0f;
    else if ( ApuC4EnvVol > 0 )
      ApuC4EnvVol--;
  }
}

void InfoNES_pAPUVsync(void)
{
  ApuRenderingWave1();
  ApuRenderingWave2();
  ApuRenderingWave3();
  ApuRenderingWave4();
  ApuRenderingWave5();
    
  ApuCtrl = ApuCtrlNew;
    
  InfoNES_SoundOutput(ApuSamplesPerSync, 
			      wave_buffers[0], wave_buffers[1], wave_buffers[2],
			      wave_buffers[3], wave_buffers[4]);

  ApuClockFrameSequencer();

  entertime = K6502_GetPassedClocks();
  cur_event = 0;
}

/*===================================================================*/
/*  Versioned pAPU snapshot                                           */
/*===================================================================*/

#define INFONES_APU_STATE_MAGIC   0x41505553UL /* "APUS" */
#define INFONES_APU_STATE_VERSION 2

struct InfoNESApuStateImageT
{
  DWORD uMagic;
  DWORD uVersion;
  BYTE Ctrl, CtrlNew;

  BYTE C1[4];
  DWORD C1Skip, C1Index;
  int C1EnvPhase;
  BYTE C1EnvVol, C1Atl;
  int C1SweepPhase;
  DWORD C1Freq;

  BYTE C2[4];
  DWORD C2Skip, C2Index;
  int C2EnvPhase;
  BYTE C2EnvVol, C2Atl;
  int C2SweepPhase;
  DWORD C2Freq;

  BYTE C3[4];
  DWORD C3Skip, C3Index;
  BYTE C3Atl;
  DWORD C3Llc;
  BYTE C3WriteLatency, C3CounterStarted;

  BYTE C4[4];
  DWORD C4Sr, C4Fdc, C4Skip, C4Index;
  BYTE C4Atl, C4EnvVol;
  int C4EnvPhase;

  BYTE C5Reg[4];
  BYTE C5Enable, C5Looping, C5CurByte, C5DpcmValue;
  int C5Freq, C5Phaseacc;
  WORD C5Address, C5CacheAddr;
  int C5DmaLength, C5CacheDmaLength;

  int nEvents;
  WORD EnterTime;
  ApuEvent_t Events[INFONES_APU_STATE_EVENT_MAX];
};

typedef char InfoNESApuStateFits[
  sizeof(InfoNESApuStateImageT) <= INFONES_APU_STATE_MAX ? 1 : -1
];

int InfoNES_pAPUSaveState(void *pState, int nStateBytes)
{
  InfoNESApuStateImageT State;

  if (!pState || nStateBytes < (int)sizeof(State) ||
      cur_event < 0 || cur_event > INFONES_APU_STATE_EVENT_MAX)
    return 0;

  InfoNES_MemorySet(&State, 0, sizeof(State));
  State.uMagic = INFONES_APU_STATE_MAGIC;
  State.uVersion = INFONES_APU_STATE_VERSION;
  State.Ctrl = ApuCtrl;
  State.CtrlNew = ApuCtrlNew;

#define APU_SAVE_REGS(N) \
  State.C##N[0] = ApuC##N##a; \
  State.C##N[1] = ApuC##N##b; \
  State.C##N[2] = ApuC##N##c; \
  State.C##N[3] = ApuC##N##d
  APU_SAVE_REGS(1);
  State.C1Skip = ApuC1Skip; State.C1Index = ApuC1Index;
  State.C1EnvPhase = ApuC1EnvPhase; State.C1EnvVol = ApuC1EnvVol;
  State.C1Atl = ApuC1Atl; State.C1SweepPhase = ApuC1SweepPhase;
  State.C1Freq = ApuC1Freq;

  APU_SAVE_REGS(2);
  State.C2Skip = ApuC2Skip; State.C2Index = ApuC2Index;
  State.C2EnvPhase = ApuC2EnvPhase; State.C2EnvVol = ApuC2EnvVol;
  State.C2Atl = ApuC2Atl; State.C2SweepPhase = ApuC2SweepPhase;
  State.C2Freq = ApuC2Freq;

  APU_SAVE_REGS(3);
  State.C3Skip = ApuC3Skip; State.C3Index = ApuC3Index;
  State.C3Atl = ApuC3Atl; State.C3Llc = ApuC3Llc;
  State.C3WriteLatency = 0;
  State.C3CounterStarted = ApuC3ReloadFlag;

  APU_SAVE_REGS(4);
  State.C4Sr = ApuC4Sr; State.C4Fdc = ApuC4Fdc;
  State.C4Skip = ApuC4Skip; State.C4Index = ApuC4Index;
  State.C4Atl = ApuC4Atl; State.C4EnvVol = ApuC4EnvVol;
  State.C4EnvPhase = ApuC4EnvPhase;
#undef APU_SAVE_REGS

  InfoNES_MemoryCopy(State.C5Reg, ApuC5Reg, sizeof(State.C5Reg));
  State.C5Enable = ApuC5Enable; State.C5Looping = ApuC5Looping;
  State.C5CurByte = ApuC5CurByte; State.C5DpcmValue = ApuC5DpcmValue;
  State.C5Freq = ApuC5Freq; State.C5Phaseacc = ApuC5Phaseacc;
  State.C5Address = ApuC5Address; State.C5CacheAddr = ApuC5CacheAddr;
  State.C5DmaLength = ApuC5DmaLength;
  State.C5CacheDmaLength = ApuC5CacheDmaLength;
  State.nEvents = cur_event;
  State.EnterTime = entertime;
  if (cur_event > 0)
  {
    InfoNES_MemoryCopy(
      State.Events,
      ApuEventQueue,
      cur_event * sizeof(ApuEvent_t)
    );
  }

  InfoNES_MemorySet(pState, 0, nStateBytes);
  InfoNES_MemoryCopy(pState, &State, sizeof(State));
  return (int)sizeof(State);
}

int InfoNES_pAPULoadState(const void *pState, int nStateBytes)
{
  const InfoNESApuStateImageT *pImage =
    (const InfoNESApuStateImageT *)pState;

  if (!pImage || nStateBytes != (int)sizeof(*pImage) ||
      pImage->uMagic != INFONES_APU_STATE_MAGIC ||
      ( pImage->uVersion != 1 &&
        pImage->uVersion != INFONES_APU_STATE_VERSION ) ||
      pImage->nEvents < 0 ||
      pImage->nEvents > INFONES_APU_STATE_EVENT_MAX)
    return 0;

  ApuCtrl = pImage->Ctrl;
  ApuCtrlNew = pImage->CtrlNew;

#define APU_LOAD_REGS(N) \
  ApuC##N##a = pImage->C##N[0]; \
  ApuC##N##b = pImage->C##N[1]; \
  ApuC##N##c = pImage->C##N[2]; \
  ApuC##N##d = pImage->C##N[3]
  APU_LOAD_REGS(1);
  ApuC1Skip = pImage->C1Skip; ApuC1Index = pImage->C1Index;
  ApuC1EnvPhase = pImage->C1EnvPhase; ApuC1EnvVol = pImage->C1EnvVol;
  ApuC1Atl = pImage->C1Atl; ApuC1SweepPhase = pImage->C1SweepPhase;
  ApuC1Freq = pImage->C1Freq;

  APU_LOAD_REGS(2);
  ApuC2Skip = pImage->C2Skip; ApuC2Index = pImage->C2Index;
  ApuC2EnvPhase = pImage->C2EnvPhase; ApuC2EnvVol = pImage->C2EnvVol;
  ApuC2Atl = pImage->C2Atl; ApuC2SweepPhase = pImage->C2SweepPhase;
  ApuC2Freq = pImage->C2Freq;

  APU_LOAD_REGS(3);
  ApuC3Skip = pImage->C3Skip; ApuC3Index = pImage->C3Index;
  ApuC3Atl = pImage->C3Atl; ApuC3Llc = pImage->C3Llc;
  ApuC3ReloadFlag = ( pImage->uVersion >= 2 )
      ? ( pImage->C3CounterStarted ? 1 : 0 ) : 0;

  APU_LOAD_REGS(4);
  ApuC4Sr = pImage->C4Sr; ApuC4Fdc = pImage->C4Fdc;
  if ( !ApuC4Sr ) ApuC4Sr = 1;
  ApuC4Skip = pImage->C4Skip; ApuC4Index = pImage->C4Index;
  ApuC4Atl = pImage->C4Atl; ApuC4EnvVol = pImage->C4EnvVol;
  ApuC4EnvPhase = pImage->C4EnvPhase;
#undef APU_LOAD_REGS

  InfoNES_MemoryCopy(ApuC5Reg, pImage->C5Reg, sizeof(ApuC5Reg));
  ApuC5Enable = pImage->C5Enable; ApuC5Looping = pImage->C5Looping;
  ApuC5CurByte = pImage->C5CurByte;
  ApuC5DpcmValue = ( pImage->uVersion >= 2 )
      ? ( pImage->C5DpcmValue & 0x7f )
      : ( ( pImage->C5Reg[1] & 1 ) + ( pImage->C5DpcmValue << 1 ) );
  ApuC5Freq = pImage->C5Freq; ApuC5Phaseacc = pImage->C5Phaseacc;
  ApuC5Address = pImage->C5Address;
  ApuC5CacheAddr = pImage->C5CacheAddr;
  ApuC5DmaLength = pImage->C5DmaLength;
  ApuC5CacheDmaLength = pImage->C5CacheDmaLength;

  /* Never restore raw pointers. They are derived from the restored duty
     cycle; the frontend clears its mixer after a state load. */
  ApuC1Wave = pulse_waves[(ApuC1a & 0xc0) >> 6];
  ApuC2Wave = pulse_waves[(ApuC2a & 0xc0) >> 6];
  ApuC1Skip = ApuC1Freq
      ? ( ApuPulseMagic << 1 ) / ( ApuC1Freq + 1 ) : 0;
  ApuC2Skip = ApuC2Freq
      ? ( ApuPulseMagic << 1 ) / ( ApuC2Freq + 1 ) : 0;
  ApuC3Skip = ApuC3Freq
      ? ApuTriangleMagic / ( ApuC3Freq + 1 ) : 0;
  ApuC4Skip = ApuC4Freq ? ApuNoiseMagic / ApuC4Freq : 0;
  InfoNES_MemorySet(wave_buffers, 0, sizeof(wave_buffers));
  cur_event = pImage->nEvents;
  /* Event timestamps are relative to the frame baseline.  The CPU snapshot
     starts a fresh wrapping clock, so establish the matching baseline here. */
  entertime = K6502_GetPassedClocks();
  if (cur_event > 0)
  {
    InfoNES_MemoryCopy(
      ApuEventQueue,
      pImage->Events,
      cur_event * sizeof(ApuEvent_t)
    );
  }
  InfoNES_SoundReset();
  return 1;
}

/*===================================================================*/
/*                                                                   */
/*            InfoNES_pApuInit() : Initialize pApu                   */
/*                                                                   */
/*===================================================================*/

void InfoNES_pAPUInit(void)
{
  /* Sound Hardware Init */
  InfoNES_SoundInit();

  ApuQuality = pAPU_QUALITY - 1;            // 1: 22050, 2: 44100 [samples/sec]

  ApuPulseMagic      = ApuQual[ ApuQuality ].pulse_magic;
  ApuTriangleMagic   = ApuQual[ ApuQuality ].triangle_magic;
  ApuNoiseMagic      = ApuQual[ ApuQuality ].noise_magic;
  ApuSamplesPerSync  = ApuQual[ ApuQuality ].samples_per_sync;
  ApuCyclesPerSample = ApuQual[ ApuQuality ].cycles_per_sample;
  ApuSampleRate      = ApuQual[ ApuQuality ].sample_rate;
  ApuCycleRate       = ApuQual[ ApuQuality ].cycle_rate;
	
  InfoNES_SoundOpen( ApuSamplesPerSync, ApuSampleRate );

  /*-------------------------------------------------------------------*/
  /* Initialize Rectangular, Noise Wave's Regs                         */
  /*-------------------------------------------------------------------*/
  ApuCtrl = ApuCtrlNew = 0;
  ApuC1Wave = pulse_50;
  ApuC2Wave = pulse_50;

  ApuC1a = ApuC1b = ApuC1c = ApuC1d = 0;
  ApuC2a = ApuC2b = ApuC2c = ApuC2d = 0;
  ApuC4a = ApuC4b = ApuC4c = ApuC4d = 0;

  ApuC1Skip = ApuC2Skip = ApuC4Skip = 0;
  ApuC1Index = ApuC2Index = ApuC4Index = 0;
  ApuC1EnvPhase = ApuC2EnvPhase = ApuC4EnvPhase = 0;
  ApuC1EnvVol = ApuC2EnvVol = ApuC4EnvVol = 0;
  ApuC1Atl = ApuC2Atl = ApuC4Atl = 0;
  ApuC1SweepPhase = ApuC2SweepPhase = 0;
  ApuC1Freq = ApuC2Freq = ApuC4Freq = 0;
  ApuC4Sr = 1;
  ApuC4Fdc = 0;

  /*-------------------------------------------------------------------*/
  /*   Initialize Triangle Wave's Regs                                 */
  /*-------------------------------------------------------------------*/
  ApuC3a = ApuC3b = ApuC3c = ApuC3d = 0;
  ApuC3Atl = ApuC3Llc = 0;
  ApuC3ReloadFlag = 0;

  /*-------------------------------------------------------------------*/
  /*   Initialize DPCM's Regs                                          */
  /*-------------------------------------------------------------------*/
  ApuC5Reg[0] = ApuC5Reg[1] = ApuC5Reg[2] = ApuC5Reg[3] = 0;
  ApuC5Enable = ApuC5Looping = ApuC5CurByte = ApuC5DpcmValue = 0;
  ApuC5Freq = ApuDpcmCycles[0] << 16;
  ApuC5Phaseacc = ApuC5Freq;
  ApuC5Address = ApuC5CacheAddr = 0;
  ApuC5DmaLength = ApuC5CacheDmaLength = 0;

  /*-------------------------------------------------------------------*/
  /*   Initialize Wave Buffers                                         */
  /*-------------------------------------------------------------------*/
  InfoNES_MemorySet( (void *)wave_buffers[0], 0, 735 );  
  InfoNES_MemorySet( (void *)wave_buffers[1], 0, 735 );  
  InfoNES_MemorySet( (void *)wave_buffers[2], 0, 735 );  
  InfoNES_MemorySet( (void *)wave_buffers[3], 0, 735 );  
  InfoNES_MemorySet( (void *)wave_buffers[4], 0, 735 );  

  /* InfoNES_Reset resets the 6502 immediately after pAPU initialization. */
  entertime = 0;
  cur_event = 0;
}

/*===================================================================*/
/*                                                                   */
/*            InfoNES_pApuDone() : Finalize pApu                     */
/*                                                                   */
/*===================================================================*/

void InfoNES_pAPUDone(void)
{
  InfoNES_SoundClose();
}

/*
 * End of InfoNES_pAPU.cpp
 */
