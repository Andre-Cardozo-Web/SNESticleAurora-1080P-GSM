static void _GskApplyRenderTransform(void)
{
    float sx = (float)_gsk_fb_width / (float)GSK_LOGICAL_W;
    float sy = (float)_gsk_fb_height / (float)GSK_LOGICAL_H;
    GPPrimSetTransform(sx, sy, 0.0f, 0.0f);
}

static void _GskApplyDisplay(void)
{
    GSGLOBAL *gs = _pGsGlobal;
    int dw, dh, magh, startx, starty;
    if (!_gsk_initialised || !gs) return;

    dw = _gsk_base_dw; dh = _gsk_base_dh; magh = _gsk_base_magh;
    startx = _gsk_base_startx; starty = _gsk_base_starty;

    if (g_GskOverscan > 0)
    {
        int sx = (_gsk_base_dw * g_GskOverscan) / 1300; int sy = (_gsk_base_dh * g_GskOverscan) / 1300;
        dw = _gsk_base_dw - sx * 2; dh = _gsk_base_dh - sy * 2;
        startx = _gsk_base_startx + sx; starty = _gsk_base_starty + sy;
    }

    if (_gsk_active_mode == GSK_VIDMODE_240P && _gsk_240p_window_x >= 0 && _gsk_240p_window_w > 0 && g_GskOverscan == 0)
    {
        const int winw = _gsk_240p_window_w;
        if (winw == 256) {
            int cMag = 10; startx += (dw - (winw * cMag)) / 2; dw = winw * cMag; magh = cMag - 1;
        } else if (winw == 352) {
            int cMag = 7; startx += (dw - (winw * cMag)) / 2; dw = winw * cMag; magh = cMag - 1;
        } else if (winw == 512) {
            int cMag = 5; startx += (dw - (winw * cMag)) / 2; dw = winw * cMag; magh = cMag - 1;
        }
        GS_SET_DISPFB2(_pGsGlobal->ScreenBuffer[(_pGsGlobal->ActiveBuffer ^ 1) & 1] / 8192, _gsk_fb_width / 64, _pGsGlobal->PSM, _gsk_240p_window_x, 0);
    }

    if (_gsk_native240p_par && _gsk_active_mode == GSK_VIDMODE_240P && g_GskOverscan == 0 && _gsk_base_magh > 0 && _gsk_240p_window_w <= 0)
    {
        int old_m = _gsk_base_magh + 1; int new_m = old_m - 1; int new_dw = (_gsk_base_dw / old_m) * new_m;
        startx += (dw - new_dw) / 2 - ((_gsk_fb_width == 256 ? 2 : 0) * new_m);
        dw = new_dw; magh = new_m - 1; starty += 1;
    }

    if (_gsk_ui256_on_320fb && _gsk_active_mode == GSK_VIDMODE_240P && _gsk_fb_width == 320)
    {
        int nm = (dw + (GSK_LOGICAL_W / 2)) / GSK_LOGICAL_W;
        if (nm < 1) nm = 1; if (nm > 16) nm = 16;
        dw = GSK_LOGICAL_W * nm; startx += (dw - (GSK_LOGICAL_W * nm)) / 2; magh = nm - 1;
    }

    if (g_GskWidescreen)
    {
        int m1 = magh + 1; int src = m1 ? dw / m1 : dw; int nm = (m1 * 4 + 1) / 3;
        if (nm > 16) nm = 16; if (nm < 1) nm = 1;
        startx -= ((nm * src) - dw) / 2; dw = nm * src; magh = nm - 1;
    }

    gs->DW     = dw;
    gs->DH     = dh;
    gs->MagH   = magh;
    gs->MagV   = _gsk_base_magv;
    gs->StartX = startx;
    gs->StartY = starty;

    gsKit_set_display_offset(gs, g_GskDispOffX * _gsk_vck, g_GskDispOffY + _gsk_game_y_bias);
    _GskApplyRenderTransform();
}

void GSK_Set240pVisibleWindow(int x, int width) { if (x < 0 || width <= 0 || x + width > _gsk_fb_width) { GSK_Clear240pVisibleWindow(); return; } if (_gsk_240p_window_x == x && _gsk_240p_window_w == width) return; _gsk_240p_window_x = x; _gsk_240p_window_w = width; _GskApplyDisplay(); }
void GSK_Clear240pVisibleWindow(void) { if (_gsk_240p_window_x < 0 && _gsk_240p_window_w == 0) return; _gsk_240p_window_x = -1; _gsk_240p_window_w = 0; _GskApplyDisplay(); }
void GSK_SetDisplayOffset(int x, int y) { g_GskDispOffX = x; g_GskDispOffY = y; _GskApplyDisplay(); }
void GSK_SetGameplayYOffsetBias(int y) { if (_gsk_game_y_bias == y) return; _gsk_game_y_bias = y; _GskApplyDisplay(); }
void GSK_SetUi256On320Framebuffer(int on) { on = on ? 1 : 0; if (_gsk_ui256_on_320fb == on) return; _gsk_ui256_on_320fb = on; _GskApplyDisplay(); }
void GSK_Set240pFramebufferWidth(int w) { if (w != 256 && w != 320 && w != 512) w = 256; _gsk_240p_fb_width = w; }
int GSK_Get240pFramebufferWidth(void) { return _gsk_240p_fb_width; }
int GSK_GetActiveFramebufferWidth(void) { if (!_gsk_initialised || !_pGsGlobal) return 0; return _pGsGlobal->Width; }
void GSK_GetPceDebugState(int *fbw, int *winw, int *dw, int *magh, int *startx, int *overscan, int *wide) { if (fbw) *fbw = _gsk_fb_width; if (winw) *winw = _gsk_240p_window_w; if (dw) *dw = _pGsGlobal ? _pGsGlobal->DW : -1; if (magh) *magh = _pGsGlobal ? _pGsGlobal->MagH : -1; if (startx) *startx = _pGsGlobal ? _pGsGlobal->StartX : -1; if (overscan) *overscan = g_GskOverscan; if (wide) *wide = g_GskWidescreen; }
void GSK_SetOverscan(int p) { if (p < 0) p = 0; if (p > 100) p = 100; g_GskOverscan = p; _GskApplyDisplay(); }
void GSK_SetWidescreen(int on) { g_GskWidescreen = on ? 1 : 0; _GskApplyDisplay(); }
void GSK_SetNative240pPar(int on) { on = on ? 1 : 0; if (_gsk_native240p_par == on) return; _gsk_native240p_par = on; _GskApplyDisplay(); }

void GSK_ReinitVideo(void)
{
    GSGLOBAL *old; if (!_gsk_initialised || !_pGsGlobal) return;
    GSK_DrainAndWait(); old = _pGsGlobal; _gsk_initialised = 0; _pGsGlobal = NULL; gsKit_deinit_global(old);
    GSK_Init(_gsk_arg_w, _gsk_arg_h, _gsk_arg_dispx, _gsk_arg_dispy, _gsk_arg_psm, _gsk_arg_psmz, _gsk_arg_mode, _gsk_arg_interlace);
}

int GSK_GetActiveVideoMode(void) { return _gsk_active_mode; }
Uint32 GSK_VramAllocTBP(Uint32 n) { u32 a; if (!_gsk_initialised || !_pGsGlobal) return 0; a = gsKit_vram_alloc(_pGsGlobal, n, GSKIT_ALLOC_USERBUFFER); return (a == GSKIT_ALLOC_ERROR) ? 0 : a / 256; }
void GSK_DrainAndWait(void) { if (!_gsk_initialised) return; gsKit_queue_exec(_pGsGlobal); gsKit_finish(); DmaSyncGIF(); }
void GSK_DrainForRawGif(void) { if (!_gsk_initialised) return; gsKit_queue_exec(_pGsGlobal); DmaSyncGIF(); }
void GSK_SetGameplayFastClear(Bool e) { _gsk_gameplay_fast_clear = e ? TRUE : FALSE; }
void GSK_SetGameplaySkipClear(Bool e) { _gsk_gameplay_skip_clear = e ? TRUE : FALSE; }
void GSK_FlushFrame(void) { if (!_gsk_initialised) return; gsKit_queue_exec(_pGsGlobal); gsKit_finish(); }
void GSK_SyncFlip(void) { if (!_gsk_initialised) return; gsKit_sync_flip(_pGsGlobal); if (_gsk_240p_window_x >= 0) _GskApplyDisplay(); }

void GSK_ResetFrame(void)
{
    GSGLOBAL *gs; u64 *p_data;
    if (!_gsk_initialised || !_pGsGlobal) return;
    gs = _pGsGlobal;

    p_data = (u64 *)gsKit_heap_alloc(gs, 4, 64, GIF_AD);
    if (!p_data) return;

    *p_data++ = GIF_TAG_AD(4); *p_data++ = GIF_AD;
    *p_data++ = GS_SETREG_FRAME_1(gs->ScreenBuffer[gs->ActiveBuffer & 1] / 8192, gs->Width / 64, gs->PSM, 0); *p_data++ = GS_REG_FRAME_1;
    *p_data++ = GS_SETREG_XYOFFSET_1(gs->OffsetX, gs->OffsetY); *p_data++ = GS_XYOFFSET_1;
    *p_data++ = GS_SETREG_ALPHA(0, 1, 0, 1, 0x80); *p_data++ = GS_REG_ALPHA_1;
    *p_data++ = (u64)1; *p_data++ = (u64)GS_REG_COLCLAMP;

    {
        u8 previous_alpha = gs->PrimAlphaEnable; gs->PrimAlphaEnable = GS_SETTING_OFF;
        if (!_gsk_gameplay_skip_clear) {
            if (_gsk_gameplay_fast_clear && gs->Width > 0 && gs->Height > 0) {
                int rows = (_gsk_active_mode == GSK_VIDMODE_240P) ? 8 : 16;
                int c_rows = (gs->Height < rows) ? gs->Height : rows;
                gsKit_set_scissor(gs, GS_SETREG_SCISSOR(0, gs->Width - 1, 0, c_rows - 1));
                gsKit_clear(gs, 0);
                gsKit_set_scissor(gs, GS_SETREG_SCISSOR(0, gs->Width - 1, 0, gs->Height - 1));
            } else {
                gsKit_clear(gs, 0);
            }
        }
        gs->PrimAlphaEnable = previous_alpha;
    }
}

void GSK_InvalidateTextureCache(void) { _gsk_invalidate_pending = 1; }
int GSK_TakeInvalidatePending(void) { int p = _gsk_invalidate_pending; _gsk_invalidate_pending = 0; return p; }
void *GSK_AsUncached(void *ptr) { Uint32 a = (Uint32)ptr; if (!a) return ptr; assert((a & 0xF0000000) == 0 && "GSK_AsUncached: Out of physical RAM"); return (void *)(a | 0x20000000); }
