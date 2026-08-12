
#ifndef _GPPRIM_H
#define _GPPRIM_H

#include <tamtypes.h>
#ifdef __cplusplus
extern "C" {
#endif

void GPPrimRect(unsigned x1, unsigned y1, unsigned c1, unsigned x2, unsigned y2, unsigned c2, unsigned z, unsigned abe);
void GPPrimEnableZBuf(void);
void GPPrimDisableZBuf(void);
void GPPrimTexRect(u32 x1, u32 y1, u32 u1, u32 v1, u32 x2, u32 y2, u32 u2, u32 v2, u32 z, u32 colour, unsigned abe);
void GPPrimSetTex(u32 tbp, u32 tbw, u32 texwidthlog2, u32 texheightlog2, u32 tpsm, u32 cbp, u32 cbw, u32 cpsm, int filter);
void GPPrimUploadTexture(int TBP, int TBW, int xofs, int yofs, int pxlfmt, void *tex, int wpxls, int hpxls);

/* Set the logical->physical coordinate transform applied by GPPrimRect and
 * GPPrimTexRect to position coordinates (x,y). UVs are not transformed.
 *
 * Used by the gsKit init path (gskit_backend.c::GSK_Init) to map the
 * legacy 256x240 UI layout onto the 640x480 physical framebuffer. */
void GPPrimSetScale(float sx, float sy);
void GPPrimSetTransform(float sx, float sy, float ox, float oy);

/* Read back the current logical->physical transform. The font path uses
 * it to place an exact 2x glyph draw without fractional NEAREST
 * resampling. */
float GPPrimGetScaleX(void);
float GPPrimGetScaleY(void);
float GPPrimGetOffsetX(void);
float GPPrimGetOffsetY(void);

/* Like GPPrimTexRect but x/y are PHYSICAL framebuffer coordinates: the
 * logical->physical position scale is NOT applied (UVs are never scaled,
 * same as GPPrimTexRect).  Used by the font to draw glyphs at an exact
 * integer multiple of the atlas for crisp, uniform letters. */
void GPPrimTexRectAbs(u32 x1, u32 y1, u32 u1, u32 v1, u32 x2, u32 y2, u32 u2, u32 v2, u32 z, u32 colour, unsigned abe);
#ifdef __cplusplus
}
#endif
#endif
