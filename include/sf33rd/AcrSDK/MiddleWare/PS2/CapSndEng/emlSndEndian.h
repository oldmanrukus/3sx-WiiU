#ifndef EML_SND_ENDIAN_H
#define EML_SND_ENDIAN_H

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
/* Convert PS2 sound data's numeric fields to host order, in place and only once
   per block. No-ops on little-endian hosts, where the data is already native. */
void emlPhdFixEndian(void* phd);
void emlSpuMapFixEndian(void* map);
#else
#define emlPhdFixEndian(phd) ((void)(phd))
#define emlSpuMapFixEndian(map) ((void)(map))
#endif

#endif
