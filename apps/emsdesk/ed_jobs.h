#ifndef ED_JOBS_H
#define ED_JOBS_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "fwog_main.h"

enum {
    ED_SEL_HOME = 0,
    ED_SEL_BS   = 1,
    ED_SEL_TF   = 2,
    ED_SEL_TE   = 3,
    ED_SEL_IB   = 4,
    ED_SEL_FOB  = 5,
    ED_SEL_CM   = 6,
    ED_SEL_OC   = 7
};

void ed_note_sel(const uint8_t *buf, size_t n);
uint8_t ed_take_sel(void);

void ed_bs_job_enter(void);
void ed_bs_job_leave(void);
void ed_bs_job_tick(void);

void ed_tf_job_enter(void);
void ed_tf_job_leave(void);
void ed_tf_job_tick(void);

void ed_te_job_enter(void);
void ed_te_job_leave(void);
void ed_te_job_tick(void);

void ed_ib_job_enter(void);
void ed_ib_job_leave(void);
void ed_ib_job_tick(void);

void ed_fob_job_enter(void);
void ed_fob_job_leave(void);
void ed_fob_job_tick(void);

void ed_cm_job_enter(void);
void ed_cm_job_leave(void);
void ed_cm_job_tick(void);

void ed_oc_job_enter(void);
void ed_oc_job_leave(void);
void ed_oc_job_tick(void);

extern uint8_t g_ed_cap[20480];

#endif
