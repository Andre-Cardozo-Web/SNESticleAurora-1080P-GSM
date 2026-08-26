#include <stdio.h>
#include <string.h>

#include "types.h"
#include "mixbuffer.h"
#include "prof.h"
#include "file.h"
#include "mainloop_exec.h"
#include "mainloop_shared.h"

/* MAINLOOP_SNESSTATEDEBUG lives in mainloop_shared.h (included above). */

extern "C" {
#include "sncpu_c.h"
#include "snspc_c.h"
}

extern "C" Int32 SNCPUExecute_ASM(SNCpuT *pCpu);

#if MAINLOOP_SNESSTATEDEBUG
static SnesStateT _TestState[3];
#endif

Bool _ExecuteSnes(CRenderSurface *pSurface, CMixBuffer *pMixBuffer, Emu::SysInputT *pInput, Emu::System::ModeE eMode)
{

        #if !TESTASM  

            #if !MAINLOOP_SNESSTATEDEBUG
//            pMixBuffer=NULL;
            /* AURORA_RUNTIME_LEAN_V1_SNES_EXEC_20260824
             * CPU overclock selection is gone, so these two executor pointers
             * are process-lifetime constants in the normal SNESticle path.
             * Configure them once instead of rewriting them every frame. */
            static Bool s_bAuroraExecutorsConfigured = FALSE;
            if (!s_bAuroraExecutorsConfigured)
            {
                SNCPUSetExecuteFunc(SNCPUExecute_ASM);
                SNSPCSetExecuteFunc(SNSPCExecute_C);
                s_bAuroraExecutorsConfigured = TRUE;
            }

		    PROF_ENTER("SnesExecuteFrame");
			/* AURORA_SAFE_FRAMESKIP_GG_ZOOM_V2_2: caller owns the one-shot
			 * NULL surface decision; CPU/SPC still execute every frame. */
		    _pSystem->ExecuteFrame(pInput, pSurface, pMixBuffer, eMode);
		    PROF_LEAVE("SnesExecuteFrame");
            #else

			if (_pSnes->GetFrame() > 50*60)
			{
            	SNCPUSetExecuteFunc(SNCPUExecute_C);
				_pSnes->SaveState(&_TestState[0]);
				_pSnes->ExecuteFrame(pInput, pSurface, NULL);
				_pSnes->SaveState(&_TestState[1]);


            	SNCPUSetExecuteFunc(SNCPUExecute_ASM);
				_pSnes->RestoreState(&_TestState[0]);
				_pSnes->ExecuteFrame(pInput, pSurface, NULL);
				_pSnes->SaveState(&_TestState[2]);

				if (memcmp(&_TestState[1], &_TestState[2],sizeof(SnesStateT)))
				{
					printf("State fault (frame= %d)\n", _pSnes->GetFrame());
            	    SNStateCompare(&_TestState[1],&_TestState[2]);

            	    FileWriteMem("host0:c:/emu/fault.sns", &_TestState[0], sizeof(_TestState[0]));


            	   
					printf("Resuming frame...\n");

//          	      bStateDebug = TRUE;

            	    SNCPUSetExecuteFunc(SNCPUExecute_C);
    				_pSnes->RestoreState(&_TestState[0]);
				    _pSnes->ExecuteFrame(pInput, pSurface, NULL);

            	    SNCPUSetExecuteFunc(SNCPUExecute_ASM);
				    _pSnes->RestoreState(&_TestState[0]);
				    _pSnes->ExecuteFrame(pInput, pSurface, NULL);

            	    while (1);

				}
			} else
			{
	            SNCPUSetExecuteFunc(SNCPUExecute_C);
	            SNSPCSetExecuteFunc(SNSPCExecute_C);

			    PROF_ENTER("SnesExecuteFrame");
	  		    _pSystem->ExecuteFrame(pInput, pSurface, pMixBuffer);
			    PROF_LEAVE("SnesExecuteFrame");
			}
            #endif


//  		    _pSnes->ExecuteFrame(&Input, NULL, &_AudMix);
//  		    _pSnes->ExecuteFrame(&Input, pSurface, NULL);
        #endif

    return TRUE;
}
