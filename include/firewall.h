#ifndef FIREWALL_H
#define FIREWALL_H

#include "config.h"

/* nftables-based firewall. Generates an nft ruleset from cfg and
 * applies via `nft -f`. */

int firewall_init(void);             /* sanity check: nft on PATH */
int firewall_apply(const rd_config *cfg);  /* generate + apply ruleset */
int firewall_flush(void);            /* clear all rules (cleanup) */

#endif
