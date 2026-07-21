/*******************************************************************************
 *
 *  LPC-10 D6 Synthesizer
 *
 *  Author:  <87430545@qq.com>
 *
 *  Create:  Oct/31/2021 by fanoble
 *
 *******************************************************************************
 */

#ifndef __LPC_D6_SYNTH_H__
#define __LPC_D6_SYNTH_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*lpc_feed_t)(void* host, unsigned char* food);

#define LPC_CMD_STOP		   -1
#define LPC_CMD_NONE			0
#define LPC_CMD_RESET			1
#define LPC_CMD_PAYLOAD			2

#define LPC_STD_VARIANT_BBK		0
#define LPC_STD_VARIANT_Subor	1
#define LPC_STD_VARIANT_Generic	2

/*
 * lpc_d6_synth_do() return values.
 *
 * LPC_RESULT_NEED_DATA is used by the FCEUX integration to yield without
 * corrupting the decoder state when the emulated 16-byte FIFO temporarily
 * runs dry.  Existing blocking callers may continue to ignore this value.
 */
#define LPC_RESULT_ERROR       -1
#define LPC_RESULT_OK           0
#define LPC_RESULT_EOS          1
#define LPC_RESULT_NEED_DATA    2

void* lpc_d6_synth_new(lpc_feed_t feed, void* host, int variant);
int   lpc_d6_synth_do(void* lpc, short* pcm, int* pcm_size, int* restart);
int   lpc_d6_synth_reset(void* lpc);
void  lpc_d6_synth_delete(void* lpc);

/* Helpers for emulator save states and callback rebinding. */
int   lpc_d6_synth_size(void);
void  lpc_d6_synth_rebind(void* lpc, lpc_feed_t feed, void* host);

#ifdef __cplusplus
}
#endif

#endif
