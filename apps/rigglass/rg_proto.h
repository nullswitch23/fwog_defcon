#ifndef RG_PROTO_H
#define RG_PROTO_H
#include <stdint.h>

#ifdef _MSC_VER
#pragma pack(push, 1)
#define RG_PACKED
#else
#define RG_PACKED __attribute__((packed))
#endif

#define RG_MSG_ST 0x73u
/* 255 = helper could not read this sensor (GPU / CPU temp). */
#define RG_NA     255u

typedef struct RG_PACKED {
    uint8_t type;
    uint8_t cpu, cpu_2m, cpu_10m;
    uint8_t ram, ram_2m, ram_10m;
    uint8_t net, net_2m, net_10m;
    uint8_t gpu, gpu_2m, gpu_10m;
    uint8_t tmp, tmp_2m, tmp_10m;
    char    host[16];
} rg_status_t;
_Static_assert(sizeof(rg_status_t) == 32, "rg_status_t");

#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif
