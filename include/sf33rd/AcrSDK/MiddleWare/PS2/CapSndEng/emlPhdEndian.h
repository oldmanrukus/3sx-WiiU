#ifndef EML_PHD_ENDIAN_H
#define EML_PHD_ENDIAN_H

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
/* Converts a PS2 sound bank's numeric fields to host order, in place and only
   once per bank. No-op on little-endian hosts, where the data is native. */
void emlPhdFixEndian(void* phd);
#else
#define emlPhdFixEndian(phd) ((void)(phd))
#endif

#endif
