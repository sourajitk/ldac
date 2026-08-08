/*
 * LDAC Clean-room Decoder for Windows
 * Portable header for MSVC, Clang, and GCC
 */

#ifndef _LDACDEC_PUBLIC_H_
#define _LDACDEC_PUBLIC_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LDAC_API
  #if defined(_WIN32) && defined(LDAC_BUILD_DLL)
    #define LDAC_API __declspec(dllexport)
  #elif defined(_WIN32) && !defined(LDAC_BUILD_STATIC)
    #define LDAC_API __declspec(dllimport)
  #else
    #define LDAC_API
  #endif
#endif

/* Forward declarations */
typedef struct ldacdec_s ldacdec_t;

/* Decoder API */
LDAC_API ldacdec_t* ldacdec_create(void);
LDAC_API void       ldacdec_destroy(ldacdec_t *p_dec);
LDAC_API int        ldacdecInit(ldacdec_t *p_dec);
LDAC_API int        ldacDecode(ldacdec_t *p_dec, const uint8_t *stream, int16_t *pcm, int *bytesUsed);
LDAC_API int        ldacDecode24(ldacdec_t *p_dec, const uint8_t *stream, int32_t *pcm, int *bytesUsed);
LDAC_API int        ldacDecodeFloat(ldacdec_t *p_dec, const uint8_t *stream, float *pcm, int *bytesUsed);
LDAC_API int        ldacNullPacket(ldacdec_t *p_dec, uint8_t *output, int *bytesUsed);
LDAC_API int        ldacdecGetSampleRate(ldacdec_t *p_dec);
LDAC_API int        ldacdecGetChannelCount(ldacdec_t *p_dec);
LDAC_API int        ldacdecGetFrameSamples(ldacdec_t *p_dec);

/* Standard libldacBT_dec compatibility wrappers */
LDAC_API void* ldacBT_get_handle_decode(void);
LDAC_API void  ldacBT_free_handle_decode(void *hLdacBT);
LDAC_API int   ldacBT_init_handle_decode(void *hLdacBT, int channel_mode, int sample_rate, int nshift, int var1, int var2);
LDAC_API int   ldacBT_decode(void *hLdacBT, const unsigned char *p_stream, void *p_pcm, int fmt, int stream_sz, int *used_bytes, int *pcm_sz);

#ifdef __cplusplus
}
#endif

#endif /* _LDACDEC_PUBLIC_H_ */
