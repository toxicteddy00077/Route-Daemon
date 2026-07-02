#ifndef SHAPER_H
#define SHAPER_H

#include "config.h"

// ingress + egress shaping via cake on ifb0 + WAN. 0 kbps = skip.

int  shaper_init(const rd_config *cfg);
int  shaper_teardown(void);
bool shaper_is_active(void);

#endif
