/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2013, 2016, 2020 The Linux Foundation. All rights reserved. */
/* Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved. */

#ifndef __QCOM_CLK_BRANCH_H__
#define __QCOM_CLK_BRANCH_H__

#include <linux/clk-provider.h>

#include "clk-regmap.h"

/**
 * struct clk_branch - gating clock with status bit and dynamic hardware gating
 *
 * @hwcg_reg: dynamic hardware clock gating register
 * @hwcg_bit: ORed with @hwcg_reg to enable dynamic hardware clock gating
 * @halt_reg: halt register
 * @halt_bit: ANDed with @halt_reg to test for clock halted
 * @halt_check: type of halt checking to perform
 * @clkr: handle between common and hardware-specific interfaces
 *
 * Clock which can gate its output.
 */
struct clk_branch {
	u32	hwcg_reg;
	u32	halt_reg;
	u32	mem_enable_reg;
	u32	mem_ack_reg;
	u8	hwcg_bit;
	u8	halt_bit;
	u8	mem_enable_ack_bit;
	u8	halt_check;
#define BRANCH_VOTED			BIT(7) /* Delay on disable */
#define BRANCH_HALT			0 /* pol: 1 = halt */
#define BRANCH_HALT_VOTED		(BRANCH_HALT | BRANCH_VOTED)
#define BRANCH_HALT_ENABLE		1 /* pol: 0 = halt */
#define BRANCH_HALT_ENABLE_VOTED	(BRANCH_HALT_ENABLE | BRANCH_VOTED)
#define BRANCH_HALT_DELAY		2 /* No bit to check; just delay */
#define BRANCH_HALT_SKIP		3 /* Don't check halt bit */
#define BRANCH_HALT_INVERT		4 /* Invert logic for halt bit */

	struct clk_regmap clkr;
};

/* Branch clock common bits for HLOS-owned clocks */
#define CBCR_FORCE_MEM_CORE_ON		BIT(14)
#define CBCR_FORCE_MEM_PERIPH_ON	BIT(13)
#define CBCR_FORCE_MEM_PERIPH_OFF	BIT(12)

static inline void qcom_branch_set_force_mem_core(struct regmap *regmap,
						  struct clk_branch clk, bool on)
{
	regmap_update_bits(regmap, clk.halt_reg, CBCR_FORCE_MEM_CORE_ON,
			   on ? CBCR_FORCE_MEM_CORE_ON : 0);
}

static inline void qcom_branch_set_force_periph_on(struct regmap *regmap,
						   struct clk_branch clk, bool on)
{
	regmap_update_bits(regmap, clk.halt_reg, CBCR_FORCE_MEM_PERIPH_ON,
			   on ? CBCR_FORCE_MEM_PERIPH_ON : 0);
}

static inline void qcom_branch_set_force_periph_off(struct regmap *regmap,
						    struct clk_branch clk, bool on)
{
	regmap_update_bits(regmap, clk.halt_reg, CBCR_FORCE_MEM_PERIPH_OFF,
			   on ? CBCR_FORCE_MEM_PERIPH_OFF : 0);
}

extern const struct clk_ops clk_branch_ops;
extern const struct clk_ops clk_branch2_ops;
extern const struct clk_ops clk_branch2_hw_ctl_ops;
extern const struct clk_ops clk_branch_simple_ops;
extern const struct clk_ops clk_branch2_aon_ops;
extern const struct clk_ops clk_branch2_force_off_ops;
extern const struct clk_ops clk_branch2_mem_ops;
extern const struct clk_ops clk_branch2_crm_ops;

#define to_clk_branch(_hw) \
	container_of(to_clk_regmap(_hw), struct clk_branch, clkr)

#endif
