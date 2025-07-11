// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <soc/qcom/cmd-db.h>
#include <soc/qcom/rpmh.h>
#include <soc/qcom/tcs.h>

#include <dt-bindings/clock/qcom,rpmh.h>

#define CLK_RPMH_ARC_EN_OFFSET		0
#define CLK_RPMH_VRM_EN_OFFSET		4

/**
 * struct bcm_db - Auxiliary data pertaining to each Bus Clock Manager(BCM)
 * @unit: divisor used to convert Hz value to an RPMh msg
 * @width: multiplier used to convert Hz value to an RPMh msg
 * @vcd: virtual clock domain that this bcm belongs to
 * @reserved: reserved to pad the struct
 */
struct bcm_db {
	__le32 unit;
	__le16 width;
	u8 vcd;
	u8 reserved;
};

/**
 * struct clk_rpmh - individual rpmh clock data structure
 * @hw:			handle between common and hardware-specific interfaces
 * @res_name:		resource name for the rpmh clock
 * @div:		clock divider to compute the clock rate
 * @res_addr:		base address of the rpmh resource within the RPMh
 * @res_on_val:		rpmh clock enable value
 * @state:		rpmh clock requested state
 * @aggr_state:		rpmh clock aggregated state
 * @last_sent_aggr_state: rpmh clock last aggr state sent to RPMh
 * @valid_state_mask:	mask to determine the state of the rpmh clock
 * @unit:		divisor to convert rate to rpmh msg in magnitudes of Khz
 * @dev:		device to which it is attached
 * @peer:		pointer to the clock rpmh sibling
 */
struct clk_rpmh {
	struct clk_hw hw;
	const char *res_name;
	u8 div;
	bool optional;
	u32 res_addr;
	u32 res_on_val;
	u32 state;
	u32 aggr_state;
	u32 last_sent_aggr_state;
	u32 valid_state_mask;
	u32 unit;
	struct device *dev;
	struct clk_rpmh *peer;
};

struct clk_rpmh_desc {
	struct clk_hw **clks;
	size_t num_clks;
};

static DEFINE_MUTEX(rpmh_clk_lock);

#define __DEFINE_CLK_RPMH(_platform, _name, _name_active, _res_name,	\
			  _res_en_offset, _res_on, _div, _optional)	\
	static struct clk_rpmh _platform##_##_name_active;		\
	static struct clk_rpmh _platform##_##_name = {			\
		.res_name = _res_name,					\
		.res_addr = _res_en_offset,				\
		.res_on_val = _res_on,					\
		.div = _div,						\
		.optional = _optional,					\
		.peer = &_platform##_##_name_active,			\
		.valid_state_mask = (BIT(RPMH_WAKE_ONLY_STATE) |	\
				      BIT(RPMH_ACTIVE_ONLY_STATE) |	\
				      BIT(RPMH_SLEEP_STATE)),		\
		.hw.init = &(struct clk_init_data){			\
			.ops = &clk_rpmh_ops,				\
			.name = #_name,					\
			.parent_data =  &(const struct clk_parent_data){ \
					.fw_name = "xo",		\
					.name = "xo_board",		\
			},						\
			.num_parents = 1,				\
		},							\
	};								\
	static struct clk_rpmh _platform##_##_name_active = {		\
		.res_name = _res_name,					\
		.res_addr = _res_en_offset,				\
		.res_on_val = _res_on,					\
		.div = _div,						\
		.optional = _optional,					\
		.peer = &_platform##_##_name,				\
		.valid_state_mask = (BIT(RPMH_WAKE_ONLY_STATE) |	\
					BIT(RPMH_ACTIVE_ONLY_STATE)),	\
		.hw.init = &(struct clk_init_data){			\
			.ops = &clk_rpmh_ops,				\
			.name = #_name_active,				\
			.parent_data =  &(const struct clk_parent_data){ \
					.fw_name = "xo",		\
					.name = "xo_board",		\
			},						\
			.num_parents = 1,				\
		},							\
	}

#define DEFINE_CLK_RPMH_FIXED(_platform, _name, _name_active,	\
				  _parent_name, _name_active_parent,	\
				  _div)					\
	static struct clk_fixed_factor _platform##_##_name = {		\
		.mult = 1,						\
		.div = _div,						\
		.hw.init = &(struct clk_init_data){			\
			.ops = &clk_fixed_factor_ops,			\
			.name = #_name,					\
			.parent_data =  &(const struct clk_parent_data){ \
					.fw_name = #_parent_name,	\
					.name = #_parent_name,		\
			},						\
			.num_parents = 1,				\
		},							\
	};								\
	static struct clk_fixed_factor _platform##_##_name_active = {	\
		.mult = 1,						\
		.div = _div,						\
		.hw.init = &(struct clk_init_data){			\
			.ops = &clk_fixed_factor_ops,			\
			.name = #_name_active,				\
			.parent_data =  &(const struct clk_parent_data){ \
					.fw_name = #_name_active_parent,\
					.name = #_name_active_parent,	\
			},						\
			.num_parents = 1,				\
		},							\
	}

#define DEFINE_CLK_RPMH_ARC(_platform, _name, _name_active, _res_name,	\
			    _res_on, _div)				\
	__DEFINE_CLK_RPMH(_platform, _name, _name_active, _res_name,	\
			  CLK_RPMH_ARC_EN_OFFSET, _res_on, _div, false)

#define DEFINE_CLK_RPMH_VRM(_platform, _name, _name_active, _res_name,	\
				_div)					\
	__DEFINE_CLK_RPMH(_platform, _name, _name_active, _res_name,	\
			  CLK_RPMH_VRM_EN_OFFSET, 1, _div, false)

#define DEFINE_CLK_RPMH_VRM_OPT(_platform, _name, _name_active,		\
			_res_name, _div)				\
	__DEFINE_CLK_RPMH(_platform, _name, _name_active, _res_name,	\
			  CLK_RPMH_VRM_EN_OFFSET, 1, _div, true)



#define DEFINE_CLK_RPMH_BCM(_platform, _name, _res_name)		\
	static struct clk_rpmh _platform##_##_name = {			\
		.res_name = _res_name,					\
		.valid_state_mask = BIT(RPMH_ACTIVE_ONLY_STATE),	\
		.div = 1,						\
		.hw.init = &(struct clk_init_data){			\
			.ops = &clk_rpmh_bcm_ops,			\
			.name = #_name,					\
		},							\
	}

static inline struct clk_rpmh *to_clk_rpmh(struct clk_hw *_hw)
{
	return container_of(_hw, struct clk_rpmh, hw);
}

static inline bool has_state_changed(struct clk_rpmh *c, u32 state)
{
	return (c->last_sent_aggr_state & BIT(state))
		!= (c->aggr_state & BIT(state));
}

static int clk_rpmh_send(struct clk_rpmh *c, enum rpmh_state state,
			 struct tcs_cmd *cmd, bool wait)
{
	if (wait)
		return rpmh_write(c->dev, state, cmd, 1);

	return rpmh_write_async(c->dev, state, cmd, 1);
}

static int clk_rpmh_send_aggregate_command(struct clk_rpmh *c)
{
	struct tcs_cmd cmd = { 0 };
	u32 cmd_state, on_val;
	enum rpmh_state state = RPMH_SLEEP_STATE;
	int ret;
	bool wait;

	cmd.addr = c->res_addr;
	cmd_state = c->aggr_state;
	on_val = c->res_on_val;

	for (; state <= RPMH_ACTIVE_ONLY_STATE; state++) {
		if (has_state_changed(c, state)) {
			if (cmd_state & BIT(state))
				cmd.data = on_val;

			wait = cmd_state && state == RPMH_ACTIVE_ONLY_STATE;
			ret = clk_rpmh_send(c, state, &cmd, wait);
			if (ret) {
				dev_err(c->dev, "set %s state of %s failed: (%d)\n",
					!state ? "sleep" :
					state == RPMH_WAKE_ONLY_STATE	?
					"wake" : "active", c->res_name, ret);
				return ret;
			}
		}
	}

	c->last_sent_aggr_state = c->aggr_state;
	c->peer->last_sent_aggr_state =  c->last_sent_aggr_state;

	return 0;
}

/*
 * Update state and aggregate state values based on enable value.
 */
static int clk_rpmh_aggregate_state_send_command(struct clk_rpmh *c,
						bool enable)
{
	int ret;

	c->state = enable ? c->valid_state_mask : 0;
	c->aggr_state = c->state | c->peer->state;
	c->peer->aggr_state = c->aggr_state;

	ret = clk_rpmh_send_aggregate_command(c);
	if (!ret)
		return 0;

	if (ret && enable)
		c->state = 0;
	else if (ret)
		c->state = c->valid_state_mask;

	WARN(1, "clk: %s failed to %s\n", c->res_name,
	     enable ? "enable" : "disable");
	return ret;
}

static int clk_rpmh_prepare(struct clk_hw *hw)
{
	struct clk_rpmh *c = to_clk_rpmh(hw);
	int ret = 0;

	mutex_lock(&rpmh_clk_lock);
	ret = clk_rpmh_aggregate_state_send_command(c, true);
	mutex_unlock(&rpmh_clk_lock);

	return ret;
}

static void clk_rpmh_unprepare(struct clk_hw *hw)
{
	struct clk_rpmh *c = to_clk_rpmh(hw);

	mutex_lock(&rpmh_clk_lock);
	clk_rpmh_aggregate_state_send_command(c, false);
	mutex_unlock(&rpmh_clk_lock);
};

static unsigned long clk_rpmh_recalc_rate(struct clk_hw *hw,
					unsigned long prate)
{
	struct clk_rpmh *r = to_clk_rpmh(hw);

	/*
	 * RPMh clocks have a fixed rate. Return static rate.
	 */
	return prate / r->div;
}

static const struct clk_ops clk_rpmh_ops = {
	.prepare	= clk_rpmh_prepare,
	.unprepare	= clk_rpmh_unprepare,
	.recalc_rate	= clk_rpmh_recalc_rate,
};

static int clk_rpmh_bcm_send_cmd(struct clk_rpmh *c, bool enable)
{
	struct tcs_cmd cmd = { 0 };
	u32 cmd_state;
	int ret = 0;

	mutex_lock(&rpmh_clk_lock);
	if (enable) {
		cmd_state = 1;
		if (c->aggr_state)
			cmd_state = c->aggr_state;
	} else {
		cmd_state = 0;
	}

	cmd_state = min(cmd_state, BCM_TCS_CMD_VOTE_MASK);

	if (c->last_sent_aggr_state != cmd_state) {
		cmd.addr = c->res_addr;
		cmd.data = BCM_TCS_CMD(1, enable, 0, cmd_state);

		/*
		 * Send only an active only state request. RPMh continues to
		 * use the active state when we're in sleep/wake state as long
		 * as the sleep/wake state has never been set.
		 */
		ret = clk_rpmh_send(c, RPMH_ACTIVE_ONLY_STATE, &cmd, enable);
		if (ret) {
			dev_err(c->dev, "set active state of %s failed: (%d)\n",
				c->res_name, ret);
		} else {
			c->last_sent_aggr_state = cmd_state;
		}
	}

	mutex_unlock(&rpmh_clk_lock);

	return ret;
}

static int clk_rpmh_bcm_prepare(struct clk_hw *hw)
{
	struct clk_rpmh *c = to_clk_rpmh(hw);

	return clk_rpmh_bcm_send_cmd(c, true);
}

static void clk_rpmh_bcm_unprepare(struct clk_hw *hw)
{
	struct clk_rpmh *c = to_clk_rpmh(hw);

	clk_rpmh_bcm_send_cmd(c, false);
}

static int clk_rpmh_bcm_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct clk_rpmh *c = to_clk_rpmh(hw);

	c->aggr_state = rate / c->unit;
	/*
	 * Since any non-zero value sent to hw would result in enabling the
	 * clock, only send the value if the clock has already been prepared.
	 */
	if (clk_hw_is_prepared(hw))
		clk_rpmh_bcm_send_cmd(c, true);

	return 0;
}

static long clk_rpmh_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *parent_rate)
{
	return rate;
}

static unsigned long clk_rpmh_bcm_recalc_rate(struct clk_hw *hw,
					unsigned long prate)
{
	struct clk_rpmh *c = to_clk_rpmh(hw);

	return c->aggr_state * c->unit;
}

static const struct clk_ops clk_rpmh_bcm_ops = {
	.prepare	= clk_rpmh_bcm_prepare,
	.unprepare	= clk_rpmh_bcm_unprepare,
	.set_rate	= clk_rpmh_bcm_set_rate,
	.round_rate	= clk_rpmh_round_rate,
	.recalc_rate	= clk_rpmh_bcm_recalc_rate,
};

/* Resource name must match resource id present in cmd-db */
DEFINE_CLK_RPMH_ARC(sdm845, bi_tcxo, bi_tcxo_ao, "xo.lvl", 0x3, 2);
DEFINE_CLK_RPMH_VRM(sdm845, ln_bb_clk2, ln_bb_clk2_ao, "lnbclka2", 2);
DEFINE_CLK_RPMH_VRM(sdm845, ln_bb_clk3, ln_bb_clk3_ao, "lnbclka3", 2);
DEFINE_CLK_RPMH_VRM(sdm845, rf_clk1, rf_clk1_ao, "rfclka1", 1);
DEFINE_CLK_RPMH_VRM(sdm845, rf_clk2, rf_clk2_ao, "rfclka2", 1);
DEFINE_CLK_RPMH_VRM(sdm845, rf_clk3, rf_clk3_ao, "rfclka3", 1);
DEFINE_CLK_RPMH_VRM(sm8150, rf_clk3, rf_clk3_ao, "rfclka3", 1);
DEFINE_CLK_RPMH_VRM(sc8180x, rf_clk1, rf_clk1_ao, "rfclkd1", 1);
DEFINE_CLK_RPMH_VRM(sc8180x, rf_clk2, rf_clk2_ao, "rfclkd2", 1);
DEFINE_CLK_RPMH_VRM(sc8180x, rf_clk3, rf_clk3_ao, "rfclkd3", 1);
DEFINE_CLK_RPMH_VRM(sc8180x, rf_clk4, rf_clk4_ao, "rfclkd4", 1);
DEFINE_CLK_RPMH_BCM(sdm845, ipa, "IP0");
DEFINE_CLK_RPMH_BCM(sdm845, ce, "CE0");

static struct clk_hw *sdm845_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &sdm845_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &sdm845_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK2]	= &sdm845_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A]	= &sdm845_ln_bb_clk2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &sdm845_ln_bb_clk3.hw,
	[RPMH_LN_BB_CLK3_A]	= &sdm845_ln_bb_clk3_ao.hw,
	[RPMH_RF_CLK1]		= &sdm845_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &sdm845_rf_clk1_ao.hw,
	[RPMH_RF_CLK2]		= &sdm845_rf_clk2.hw,
	[RPMH_RF_CLK2_A]	= &sdm845_rf_clk2_ao.hw,
	[RPMH_RF_CLK3]		= &sdm845_rf_clk3.hw,
	[RPMH_RF_CLK3_A]	= &sdm845_rf_clk3_ao.hw,
	[RPMH_IPA_CLK]		= &sdm845_ipa.hw,
	[RPMH_CE_CLK]		= &sdm845_ce.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sdm845 = {
	.clks = sdm845_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sdm845_rpmh_clocks),
};

static struct clk_hw *sdm670_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &sdm845_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &sdm845_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK2]	= &sdm845_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A]	= &sdm845_ln_bb_clk2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &sdm845_ln_bb_clk3.hw,
	[RPMH_LN_BB_CLK3_A]	= &sdm845_ln_bb_clk3_ao.hw,
	[RPMH_RF_CLK1]		= &sdm845_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &sdm845_rf_clk1_ao.hw,
	[RPMH_RF_CLK2]		= &sdm845_rf_clk2.hw,
	[RPMH_RF_CLK2_A]	= &sdm845_rf_clk2_ao.hw,
	[RPMH_IPA_CLK]		= &sdm845_ipa.hw,
	[RPMH_CE_CLK]		= &sdm845_ce.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sdm670 = {
	.clks = sdm670_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sdm670_rpmh_clocks),
};

DEFINE_CLK_RPMH_VRM(sdx55, rf_clk1, rf_clk1_ao, "rfclkd1", 1);
DEFINE_CLK_RPMH_VRM(sdx55, rf_clk2, rf_clk2_ao, "rfclkd2", 1);
DEFINE_CLK_RPMH_BCM(sdx55, qpic_clk, "QP0");
DEFINE_CLK_RPMH_BCM(sdx55, ipa, "IP0");

static struct clk_hw *sdx55_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &sdm845_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &sdm845_bi_tcxo_ao.hw,
	[RPMH_RF_CLK1]		= &sdx55_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &sdx55_rf_clk1_ao.hw,
	[RPMH_RF_CLK2]		= &sdx55_rf_clk2.hw,
	[RPMH_RF_CLK2_A]	= &sdx55_rf_clk2_ao.hw,
	[RPMH_QPIC_CLK]		= &sdx55_qpic_clk.hw,
	[RPMH_IPA_CLK]		= &sdx55_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sdx55 = {
	.clks = sdx55_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sdx55_rpmh_clocks),
};

static struct clk_hw *sm8150_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &sdm845_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &sdm845_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK2]	= &sdm845_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A]	= &sdm845_ln_bb_clk2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &sdm845_ln_bb_clk3.hw,
	[RPMH_LN_BB_CLK3_A]	= &sdm845_ln_bb_clk3_ao.hw,
	[RPMH_RF_CLK1]		= &sdm845_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &sdm845_rf_clk1_ao.hw,
	[RPMH_RF_CLK2]		= &sdm845_rf_clk2.hw,
	[RPMH_RF_CLK2_A]	= &sdm845_rf_clk2_ao.hw,
	[RPMH_RF_CLK3]		= &sdm845_rf_clk3.hw,
	[RPMH_RF_CLK3_A]	= &sdm845_rf_clk3_ao.hw,
	[RPMH_IPA_CLK]		= &sdm845_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sm8150 = {
	.clks = sm8150_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sm8150_rpmh_clocks),
};

static struct clk_hw *sc7180_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &sdm845_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &sdm845_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK2]	= &sdm845_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A]	= &sdm845_ln_bb_clk2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &sdm845_ln_bb_clk3.hw,
	[RPMH_LN_BB_CLK3_A]	= &sdm845_ln_bb_clk3_ao.hw,
	[RPMH_RF_CLK1]		= &sdm845_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &sdm845_rf_clk1_ao.hw,
	[RPMH_RF_CLK2]		= &sdm845_rf_clk2.hw,
	[RPMH_RF_CLK2_A]	= &sdm845_rf_clk2_ao.hw,
	[RPMH_IPA_CLK]		= &sdm845_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sc7180 = {
	.clks = sc7180_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sc7180_rpmh_clocks),
};

static struct clk_hw *sc8180x_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &sdm845_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &sdm845_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK2]	= &sdm845_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A]	= &sdm845_ln_bb_clk2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &sdm845_ln_bb_clk3.hw,
	[RPMH_LN_BB_CLK3_A]	= &sdm845_ln_bb_clk3_ao.hw,
	[RPMH_RF_CLK1]		= &sc8180x_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &sc8180x_rf_clk1_ao.hw,
	[RPMH_RF_CLK2]		= &sc8180x_rf_clk2.hw,
	[RPMH_RF_CLK2_A]	= &sc8180x_rf_clk2_ao.hw,
	[RPMH_RF_CLK3]		= &sc8180x_rf_clk3.hw,
	[RPMH_RF_CLK3_A]	= &sc8180x_rf_clk3_ao.hw,
	[RPMH_IPA_CLK]		= &sdm845_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sc8180x = {
	.clks = sc8180x_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sc8180x_rpmh_clocks),
};

DEFINE_CLK_RPMH_VRM(sm8250, ln_bb_clk1, ln_bb_clk1_ao, "lnbclka1", 2);

static struct clk_hw *sm8250_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &sdm845_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &sdm845_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK1]	= &sm8250_ln_bb_clk1.hw,
	[RPMH_LN_BB_CLK1_A]	= &sm8250_ln_bb_clk1_ao.hw,
	[RPMH_LN_BB_CLK2]	= &sdm845_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A]	= &sdm845_ln_bb_clk2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &sdm845_ln_bb_clk3.hw,
	[RPMH_LN_BB_CLK3_A]	= &sdm845_ln_bb_clk3_ao.hw,
	[RPMH_RF_CLK1]		= &sdm845_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &sdm845_rf_clk1_ao.hw,
	[RPMH_RF_CLK3]		= &sdm845_rf_clk3.hw,
	[RPMH_RF_CLK3_A]	= &sdm845_rf_clk3_ao.hw,
	[RPMH_IPA_CLK]		= &sdm845_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sm8250 = {
	.clks = sm8250_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sm8250_rpmh_clocks),
};

DEFINE_CLK_RPMH_VRM(sm8350, div_clk1, div_clk1_ao, "divclka1", 2);
DEFINE_CLK_RPMH_VRM(sm8350, rf_clk4, rf_clk4_ao, "rfclka4", 1);
DEFINE_CLK_RPMH_VRM(sm8350, rf_clk5, rf_clk5_ao, "rfclka5", 1);
DEFINE_CLK_RPMH_BCM(sm8350, pka, "PKA0");
DEFINE_CLK_RPMH_BCM(sm8350, hwkm, "HK0");

static struct clk_hw *sm8350_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &sdm845_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &sdm845_bi_tcxo_ao.hw,
	[RPMH_DIV_CLK1]		= &sm8350_div_clk1.hw,
	[RPMH_DIV_CLK1_A]	= &sm8350_div_clk1_ao.hw,
	[RPMH_LN_BB_CLK1]	= &sm8250_ln_bb_clk1.hw,
	[RPMH_LN_BB_CLK1_A]	= &sm8250_ln_bb_clk1_ao.hw,
	[RPMH_LN_BB_CLK2]	= &sdm845_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A]	= &sdm845_ln_bb_clk2_ao.hw,
	[RPMH_RF_CLK1]		= &sdm845_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &sdm845_rf_clk1_ao.hw,
	[RPMH_RF_CLK3]		= &sdm845_rf_clk3.hw,
	[RPMH_RF_CLK3_A]	= &sdm845_rf_clk3_ao.hw,
	[RPMH_RF_CLK4]		= &sm8350_rf_clk4.hw,
	[RPMH_RF_CLK4_A]	= &sm8350_rf_clk4_ao.hw,
	[RPMH_RF_CLK5]		= &sm8350_rf_clk5.hw,
	[RPMH_RF_CLK5_A]	= &sm8350_rf_clk5_ao.hw,
	[RPMH_IPA_CLK]		= &sdm845_ipa.hw,
	[RPMH_PKA_CLK]		= &sm8350_pka.hw,
	[RPMH_HWKM_CLK]		= &sm8350_hwkm.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sm8350 = {
	.clks = sm8350_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sm8350_rpmh_clocks),
};

DEFINE_CLK_RPMH_VRM(sc8280xp, ln_bb_clk3, ln_bb_clk3_ao, "lnbclka3", 2);

static struct clk_hw *sc8280xp_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &sdm845_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &sdm845_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK3]       = &sc8280xp_ln_bb_clk3.hw,
	[RPMH_LN_BB_CLK3_A]     = &sc8280xp_ln_bb_clk3_ao.hw,
	[RPMH_IPA_CLK]          = &sdm845_ipa.hw,
	[RPMH_PKA_CLK]          = &sm8350_pka.hw,
	[RPMH_HWKM_CLK]         = &sm8350_hwkm.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sc8280xp = {
	.clks = sc8280xp_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sc8280xp_rpmh_clocks),
};

/* Resource name must match resource id present in cmd-db */
DEFINE_CLK_RPMH_ARC(sc7280, bi_tcxo, bi_tcxo_ao, "xo.lvl", 0x3, 4);

DEFINE_CLK_RPMH_VRM(sm8450, ln_bb_clk1, ln_bb_clk1_ao, "lnbclka1", 4);
DEFINE_CLK_RPMH_VRM(sm8450, ln_bb_clk2, ln_bb_clk2_ao, "lnbclka2", 4);

static struct clk_hw *sm8450_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &sc7280_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &sc7280_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK1]	= &sm8450_ln_bb_clk1.hw,
	[RPMH_LN_BB_CLK1_A]	= &sm8450_ln_bb_clk1_ao.hw,
	[RPMH_LN_BB_CLK2]	= &sm8450_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A]	= &sm8450_ln_bb_clk2_ao.hw,
	[RPMH_RF_CLK1]		= &sdm845_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &sdm845_rf_clk1_ao.hw,
	[RPMH_RF_CLK2]		= &sdm845_rf_clk2.hw,
	[RPMH_RF_CLK2_A]	= &sdm845_rf_clk2_ao.hw,
	[RPMH_RF_CLK3]		= &sdm845_rf_clk3.hw,
	[RPMH_RF_CLK3_A]	= &sdm845_rf_clk3_ao.hw,
	[RPMH_RF_CLK4]		= &sm8350_rf_clk4.hw,
	[RPMH_RF_CLK4_A]	= &sm8350_rf_clk4_ao.hw,
	[RPMH_IPA_CLK]		= &sdm845_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sm8450 = {
	.clks = sm8450_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sm8450_rpmh_clocks),
};

static struct clk_hw *sc7280_rpmh_clocks[] = {
	[RPMH_CXO_CLK]      = &sc7280_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]    = &sc7280_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK2]   = &sdm845_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A] = &sdm845_ln_bb_clk2_ao.hw,
	[RPMH_RF_CLK1]      = &sdm845_rf_clk1.hw,
	[RPMH_RF_CLK1_A]    = &sdm845_rf_clk1_ao.hw,
	[RPMH_RF_CLK3]      = &sdm845_rf_clk3.hw,
	[RPMH_RF_CLK3_A]    = &sdm845_rf_clk3_ao.hw,
	[RPMH_RF_CLK4]      = &sm8350_rf_clk4.hw,
	[RPMH_RF_CLK4_A]    = &sm8350_rf_clk4_ao.hw,
	[RPMH_IPA_CLK]      = &sdm845_ipa.hw,
	[RPMH_PKA_CLK]      = &sm8350_pka.hw,
	[RPMH_HWKM_CLK]     = &sm8350_hwkm.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sc7280 = {
	.clks = sc7280_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sc7280_rpmh_clocks),
};

DEFINE_CLK_RPMH_VRM(sm6350, ln_bb_clk2, ln_bb_clk2_ao, "lnbclkg2", 4);
DEFINE_CLK_RPMH_VRM(sm6350, ln_bb_clk3, ln_bb_clk3_ao, "lnbclkg3", 4);
DEFINE_CLK_RPMH_ARC(sm6350, qlink, qlink_ao, "qphy.lvl", 0x1, 4);

static struct clk_hw *sm6350_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &sc7280_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &sc7280_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK2]	= &sm6350_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A]	= &sm6350_ln_bb_clk2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &sm6350_ln_bb_clk3.hw,
	[RPMH_LN_BB_CLK3_A]	= &sm6350_ln_bb_clk3_ao.hw,
	[RPMH_QLINK_CLK]	= &sm6350_qlink.hw,
	[RPMH_QLINK_CLK_A]	= &sm6350_qlink_ao.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sm6350 = {
	.clks = sm6350_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sm6350_rpmh_clocks),
};

DEFINE_CLK_RPMH_VRM(sdx65, ln_bb_clk1, ln_bb_clk1_ao, "lnbclka1", 4);

static struct clk_hw *sdx65_rpmh_clocks[] = {
	[RPMH_CXO_CLK]          = &sc7280_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]        = &sc7280_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK1]       = &sdx65_ln_bb_clk1.hw,
	[RPMH_LN_BB_CLK1_A]     = &sdx65_ln_bb_clk1_ao.hw,
	[RPMH_RF_CLK1]          = &sdm845_rf_clk1.hw,
	[RPMH_RF_CLK1_A]        = &sdm845_rf_clk1_ao.hw,
	[RPMH_RF_CLK2]          = &sdm845_rf_clk2.hw,
	[RPMH_RF_CLK2_A]        = &sdm845_rf_clk2_ao.hw,
	[RPMH_RF_CLK3]          = &sdm845_rf_clk3.hw,
	[RPMH_RF_CLK3_A]        = &sdm845_rf_clk3_ao.hw,
	[RPMH_RF_CLK4]          = &sm8350_rf_clk4.hw,
	[RPMH_RF_CLK4_A]        = &sm8350_rf_clk4_ao.hw,
	[RPMH_IPA_CLK]          = &sdm845_ipa.hw,
	[RPMH_QPIC_CLK]         = &sdx55_qpic_clk.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sdx65 = {
	.clks = sdx65_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sdx65_rpmh_clocks),
};

static struct clk_hw *lemans_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &sdm845_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &sdm845_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK1]	= &sm8250_ln_bb_clk1.hw,
	[RPMH_LN_BB_CLK2]	= &sdm845_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A]	= &sdm845_ln_bb_clk2_ao.hw,
	[RPMH_IPA_CLK]		= &sdm845_ipa.hw,
	[RPMH_PKA_CLK]		= &sm8350_pka.hw,
	[RPMH_HWKM_CLK]		= &sm8350_hwkm.hw,
};

static const struct clk_rpmh_desc clk_rpmh_lemans = {
	.clks = lemans_rpmh_clocks,
	.num_clks = ARRAY_SIZE(lemans_rpmh_clocks),
};

DEFINE_CLK_RPMH_VRM(anorak, rf_clk1, rf_clk1_ao, "clka1", 1);
DEFINE_CLK_RPMH_VRM(anorak, ln_bb_clk7, ln_bb_clk7_ao, "clka7", 2);
DEFINE_CLK_RPMH_VRM(anorak, ln_bb_clk8, ln_bb_clk8_ao, "clka8", 4);
DEFINE_CLK_RPMH_VRM(anorak, ln_bb_clk9, ln_bb_clk9_ao, "clka9", 2);

static struct clk_hw *anorak_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &sc7280_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &sc7280_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK7]	= &anorak_ln_bb_clk7.hw,
	[RPMH_LN_BB_CLK7_A]	= &anorak_ln_bb_clk7_ao.hw,
	[RPMH_LN_BB_CLK8]	= &anorak_ln_bb_clk8.hw,
	[RPMH_LN_BB_CLK8_A]	= &anorak_ln_bb_clk8_ao.hw,
	[RPMH_LN_BB_CLK9]	= &anorak_ln_bb_clk9.hw,
	[RPMH_LN_BB_CLK9_A]	= &anorak_ln_bb_clk9_ao.hw,
	[RPMH_RF_CLK1]		= &anorak_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &anorak_rf_clk1_ao.hw,
	[RPMH_IPA_CLK]		= &sdm845_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_anorak = {
	.clks = anorak_rpmh_clocks,
	.num_clks = ARRAY_SIZE(anorak_rpmh_clocks),
};

static struct clk_hw *of_clk_rpmh_hw_get(struct of_phandle_args *clkspec,
					 void *data)
{
	struct clk_rpmh_desc *rpmh = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= rpmh->num_clks) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	if (!rpmh->clks[idx])
		return ERR_PTR(-ENOENT);

	return rpmh->clks[idx];
}

DEFINE_CLK_RPMH_ARC(kalama, xo_pad, xo_pad_ao, "xo.lvl", 0x03, 2);
DEFINE_CLK_RPMH_FIXED(kalama, bi_tcxo, bi_tcxo_ao, xo_pad, xo_pad_ao, 2);
DEFINE_CLK_RPMH_VRM_OPT(kalama, rf_clk1, rf_clk1_ao, "clka1", 1);
DEFINE_CLK_RPMH_VRM_OPT(kalama, rf_clk2, rf_clk2_ao, "clka2", 1);
DEFINE_CLK_RPMH_VRM_OPT(kalama, rf_clk3, rf_clk3_ao, "clka3", 1);
DEFINE_CLK_RPMH_VRM_OPT(kalama, rf_clk4, rf_clk4_ao, "clka4", 1);
DEFINE_CLK_RPMH_VRM_OPT(kalama, rf_clk5, rf_clk5_ao, "clka5", 2);
DEFINE_CLK_RPMH_VRM_OPT(kalama, ln_bb_clk1, ln_bb_clk1_ao, "clka6", 2);
DEFINE_CLK_RPMH_VRM_OPT(kalama, ln_bb_clk2, ln_bb_clk2_ao, "clka7", 2);
DEFINE_CLK_RPMH_VRM_OPT(kalama, ln_bb_clk3, ln_bb_clk3_ao, "clka8", 2);

static struct clk_hw *kalama_rpmh_clocks[] = {
	[RPMH_CXO_PAD_CLK]      = &kalama_xo_pad.hw,
	[RPMH_CXO_PAD_CLK_A]    = &kalama_xo_pad_ao.hw,
	[RPMH_CXO_CLK]          = &kalama_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]        = &kalama_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK1]	= &kalama_ln_bb_clk1.hw,
	[RPMH_LN_BB_CLK1_A]	= &kalama_ln_bb_clk1_ao.hw,
	[RPMH_LN_BB_CLK2]	= &kalama_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A]	= &kalama_ln_bb_clk2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &kalama_ln_bb_clk3.hw,
	[RPMH_LN_BB_CLK3_A]	= &kalama_ln_bb_clk3_ao.hw,
	[RPMH_RF_CLK1]		= &kalama_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &kalama_rf_clk1_ao.hw,
	[RPMH_RF_CLK2]		= &kalama_rf_clk2.hw,
	[RPMH_RF_CLK2_A]	= &kalama_rf_clk2_ao.hw,
	[RPMH_RF_CLK3]		= &kalama_rf_clk3.hw,
	[RPMH_RF_CLK3_A]	= &kalama_rf_clk3_ao.hw,
	[RPMH_RF_CLK4]		= &kalama_rf_clk4.hw,
	[RPMH_RF_CLK4_A]	= &kalama_rf_clk4_ao.hw,
	[RPMH_RF_CLK5]		= &kalama_rf_clk5.hw,
	[RPMH_RF_CLK5_A]	= &kalama_rf_clk5_ao.hw,
	[RPMH_IPA_CLK]		= &sdm845_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_kalama = {
	.clks = kalama_rpmh_clocks,
	.num_clks = ARRAY_SIZE(kalama_rpmh_clocks),
};

DEFINE_CLK_RPMH_ARC(pineapple, xo_pad, xo_pad_ao, "xo.lvl", 0x03, 2);
DEFINE_CLK_RPMH_FIXED(pineapple, bi_tcxo, bi_tcxo_ao, xo_pad, xo_pad_ao, 2);
//#ifdef OPLUS_FEATURE_DISPLAY
/* add for gpio5 clk */
DEFINE_CLK_RPMH_VRM_OPT(pineapple, div_clk1, div_clk1_ao, "clka11", 2);
//#endif /* OPLUS_FEATURE_DISPLAY */
DEFINE_CLK_RPMH_VRM_OPT(pineapple, rf_clk1, rf_clk1_ao, "clka1", 1);
DEFINE_CLK_RPMH_VRM_OPT(pineapple, rf_clk2, rf_clk2_ao, "clka2", 1);
DEFINE_CLK_RPMH_VRM_OPT(pineapple, rf_clk3, rf_clk3_ao, "clka3", 2);
DEFINE_CLK_RPMH_VRM_OPT(pineapple, rf_clk4, rf_clk4_ao, "clka4", 2);
DEFINE_CLK_RPMH_VRM_OPT(pineapple, rf_clk5, rf_clk5_ao, "clka5", 2);
DEFINE_CLK_RPMH_VRM_OPT(pineapple, ln_bb_clk1, ln_bb_clk1_ao, "clka6", 2);
DEFINE_CLK_RPMH_VRM_OPT(pineapple, ln_bb_clk2, ln_bb_clk2_ao, "clka7", 2);
DEFINE_CLK_RPMH_VRM_OPT(pineapple, ln_bb_clk3, ln_bb_clk3_ao, "clka8", 2);

static struct clk_hw *pineapple_rpmh_clocks[] = {
	[RPMH_CXO_PAD_CLK]      = &pineapple_xo_pad.hw,
	[RPMH_CXO_PAD_CLK_A]    = &pineapple_xo_pad_ao.hw,
	[RPMH_CXO_CLK]          = &pineapple_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]        = &pineapple_bi_tcxo_ao.hw,
//#ifdef OPLUS_FEATURE_DISPLAY
	/* add for gpio5 clk */
	[RPMH_DIV_CLK1]         = &pineapple_div_clk1.hw,
	[RPMH_DIV_CLK1_A]       = &pineapple_div_clk1_ao.hw,
//#endif /* OPLUS_FEATURE_DISPLAY */
	[RPMH_LN_BB_CLK1]	= &pineapple_ln_bb_clk1.hw,
	[RPMH_LN_BB_CLK1_A]	= &pineapple_ln_bb_clk1_ao.hw,
	[RPMH_LN_BB_CLK2]	= &pineapple_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A]	= &pineapple_ln_bb_clk2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &pineapple_ln_bb_clk3.hw,
	[RPMH_LN_BB_CLK3_A]	= &pineapple_ln_bb_clk3_ao.hw,
	[RPMH_RF_CLK1]		= &pineapple_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &pineapple_rf_clk1_ao.hw,
	[RPMH_RF_CLK2]		= &pineapple_rf_clk2.hw,
	[RPMH_RF_CLK2_A]	= &pineapple_rf_clk2_ao.hw,
	[RPMH_RF_CLK3]		= &pineapple_rf_clk3.hw,
	[RPMH_RF_CLK3_A]	= &pineapple_rf_clk3_ao.hw,
	[RPMH_RF_CLK4]		= &pineapple_rf_clk4.hw,
	[RPMH_RF_CLK4_A]	= &pineapple_rf_clk4_ao.hw,
	[RPMH_RF_CLK5]		= &pineapple_rf_clk5.hw,
	[RPMH_RF_CLK5_A]	= &pineapple_rf_clk5_ao.hw,
	[RPMH_IPA_CLK]		= &sdm845_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_pineapple = {
	.clks = pineapple_rpmh_clocks,
	.num_clks = ARRAY_SIZE(pineapple_rpmh_clocks),
};

static struct clk_hw *sm6150_rpmh_clocks[] = {
	[RPMH_CXO_CLK]		= &sdm845_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &sdm845_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK2]	= &sdm845_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A]	= &sdm845_ln_bb_clk2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &sdm845_ln_bb_clk3.hw,
	[RPMH_LN_BB_CLK3_A]	= &sdm845_ln_bb_clk3_ao.hw,
	[RPMH_RF_CLK1]		= &sdm845_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &sdm845_rf_clk1_ao.hw,
	[RPMH_RF_CLK2]		= &sdm845_rf_clk2.hw,
	[RPMH_RF_CLK2_A]	= &sdm845_rf_clk2_ao.hw,
};

static const struct clk_rpmh_desc clk_rpmh_sm6150 = {
	.clks = sm6150_rpmh_clocks,
	.num_clks = ARRAY_SIZE(sm6150_rpmh_clocks),
};

static struct clk_hw *cliffs_rpmh_clocks[] = {
	[RPMH_CXO_PAD_CLK]	= &pineapple_xo_pad.hw,
	[RPMH_CXO_PAD_CLK_A]	= &pineapple_xo_pad_ao.hw,
	[RPMH_CXO_CLK]		= &pineapple_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &pineapple_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK2]	= &pineapple_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A]	= &pineapple_ln_bb_clk2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &pineapple_ln_bb_clk3.hw,
	[RPMH_LN_BB_CLK3_A]	= &pineapple_ln_bb_clk3_ao.hw,
	[RPMH_RF_CLK1]		= &pineapple_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &pineapple_rf_clk1_ao.hw,
	[RPMH_RF_CLK2]		= &pineapple_rf_clk2.hw,
	[RPMH_RF_CLK2_A]	= &pineapple_rf_clk2_ao.hw,
	[RPMH_IPA_CLK]		= &sdm845_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_cliffs = {
	.clks = cliffs_rpmh_clocks,
	.num_clks = ARRAY_SIZE(cliffs_rpmh_clocks),
};

static struct clk_hw *niobe_rpmh_clocks[] = {
	[RPMH_CXO_PAD_CLK]	= &pineapple_xo_pad.hw,
	[RPMH_CXO_PAD_CLK_A]	= &pineapple_xo_pad_ao.hw,
	[RPMH_CXO_CLK]		= &pineapple_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &pineapple_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK3]	= &pineapple_ln_bb_clk3.hw,
	[RPMH_LN_BB_CLK3_A]	= &pineapple_ln_bb_clk3_ao.hw,
	[RPMH_RF_CLK1]		= &pineapple_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &pineapple_rf_clk1_ao.hw,
	[RPMH_IPA_CLK]		= &sdm845_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_niobe = {
	.clks = niobe_rpmh_clocks,
	.num_clks = ARRAY_SIZE(niobe_rpmh_clocks),
};

DEFINE_CLK_RPMH_VRM_OPT(volcano, ln_bb_clk2, ln_bb_clk2_ao, "clka7", 4);
DEFINE_CLK_RPMH_VRM_OPT(volcano, ln_bb_clk3, ln_bb_clk3_ao, "clka8", 4);
DEFINE_CLK_RPMH_VRM_OPT(volcano, ln_bb_clk4, ln_bb_clk4_ao, "clka9", 2);

static struct clk_hw *volcano_rpmh_clocks[] = {
	[RPMH_CXO_PAD_CLK]	= &pineapple_xo_pad.hw,
	[RPMH_CXO_PAD_CLK_A]	= &pineapple_xo_pad_ao.hw,
	[RPMH_CXO_CLK]		= &pineapple_bi_tcxo.hw,
	[RPMH_CXO_CLK_A]	= &pineapple_bi_tcxo_ao.hw,
	[RPMH_LN_BB_CLK2]	= &volcano_ln_bb_clk2.hw,
	[RPMH_LN_BB_CLK2_A]	= &volcano_ln_bb_clk2_ao.hw,
	[RPMH_LN_BB_CLK3]	= &volcano_ln_bb_clk3.hw,
	[RPMH_LN_BB_CLK3_A]	= &volcano_ln_bb_clk3_ao.hw,
	[RPMH_LN_BB_CLK4]	= &volcano_ln_bb_clk4.hw,
	[RPMH_LN_BB_CLK4_A]	= &volcano_ln_bb_clk4_ao.hw,
	[RPMH_RF_CLK1]		= &pineapple_rf_clk1.hw,
	[RPMH_RF_CLK1_A]	= &pineapple_rf_clk1_ao.hw,
	[RPMH_RF_CLK2]		= &pineapple_rf_clk2.hw,
	[RPMH_RF_CLK2_A]	= &pineapple_rf_clk2_ao.hw,
	[RPMH_RF_CLK3]		= &kalama_rf_clk3.hw,
	[RPMH_RF_CLK3_A]	= &kalama_rf_clk3_ao.hw,
	[RPMH_IPA_CLK]		= &sdm845_ipa.hw,
};

static const struct clk_rpmh_desc clk_rpmh_volcano = {
	.clks = volcano_rpmh_clocks,
	.num_clks = ARRAY_SIZE(volcano_rpmh_clocks),
};

static int clk_rpmh_probe(struct platform_device *pdev)
{
	struct clk_hw **hw_clks;
	struct clk_rpmh *rpmh_clk;
	const struct clk_rpmh_desc *desc;
	int ret, i;

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -ENODEV;

	hw_clks = desc->clks;

	for (i = 0; i < desc->num_clks; i++) {
		const char *name;
		u32 res_addr;
		size_t aux_data_len;
		const struct bcm_db *data;

		if (!hw_clks[i])
			continue;

		name = hw_clks[i]->init->name;

		if (hw_clks[i]->init->ops != &clk_fixed_factor_ops) {
			rpmh_clk = to_clk_rpmh(hw_clks[i]);
			res_addr = cmd_db_read_addr(rpmh_clk->res_name);
			if (!res_addr) {
				hw_clks[i] = NULL;

				if (rpmh_clk->optional)
					continue;

				WARN(1, "clk-rpmh: Missing RPMh resource address for %s\n",
				     rpmh_clk->res_name);
				return -ENODEV;
			}

			data = cmd_db_read_aux_data(rpmh_clk->res_name, &aux_data_len);
			if (IS_ERR(data)) {
				ret = PTR_ERR(data);
				WARN(1, "clk-rpmh: error reading RPMh aux data for %s (%d)\n",
				     rpmh_clk->res_name, ret);
				return ret;
			}

			/* Convert unit from Khz to Hz */
			if (aux_data_len == sizeof(*data))
				rpmh_clk->unit = le32_to_cpu(data->unit) * 1000ULL;

			rpmh_clk->res_addr += res_addr;
			rpmh_clk->dev = &pdev->dev;
		}

		ret = devm_clk_hw_register(&pdev->dev, hw_clks[i]);
		if (ret) {
			dev_err(&pdev->dev, "failed to register %s\n", name);
			return ret;
		}
	}

	/* typecast to silence compiler warning */
	ret = devm_of_clk_add_hw_provider(&pdev->dev, of_clk_rpmh_hw_get,
					  (void *)desc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add clock provider\n");
		return ret;
	}

	dev_dbg(&pdev->dev, "Registered RPMh clocks\n");

	return 0;
}

static const struct of_device_id clk_rpmh_match_table[] = {
	{ .compatible = "qcom,sc7180-rpmh-clk", .data = &clk_rpmh_sc7180},
	{ .compatible = "qcom,sc8180x-rpmh-clk", .data = &clk_rpmh_sc8180x},
	{ .compatible = "qcom,sc8280xp-rpmh-clk", .data = &clk_rpmh_sc8280xp},
	{ .compatible = "qcom,sdm845-rpmh-clk", .data = &clk_rpmh_sdm845},
	{ .compatible = "qcom,sdm670-rpmh-clk", .data = &clk_rpmh_sdm670},
	{ .compatible = "qcom,sdx55-rpmh-clk",  .data = &clk_rpmh_sdx55},
	{ .compatible = "qcom,sdx65-rpmh-clk",  .data = &clk_rpmh_sdx65},
	{ .compatible = "qcom,sm6350-rpmh-clk", .data = &clk_rpmh_sm6350},
	{ .compatible = "qcom,sm8150-rpmh-clk", .data = &clk_rpmh_sm8150},
	{ .compatible = "qcom,sm8250-rpmh-clk", .data = &clk_rpmh_sm8250},
	{ .compatible = "qcom,sm8350-rpmh-clk", .data = &clk_rpmh_sm8350},
	{ .compatible = "qcom,sm8450-rpmh-clk", .data = &clk_rpmh_sm8450},
	{ .compatible = "qcom,sc7280-rpmh-clk", .data = &clk_rpmh_sc7280},
	{ .compatible = "qcom,kalama-rpmh-clk", .data = &clk_rpmh_kalama},
	{ .compatible = "qcom,pineapple-rpmh-clk", .data = &clk_rpmh_pineapple},
	{ .compatible = "qcom,sm6150-rpmh-clk", .data = &clk_rpmh_sm6150},
	{ .compatible = "qcom,lemans-rpmh-clk", .data = &clk_rpmh_lemans},
	{ .compatible = "qcom,cliffs-rpmh-clk", .data = &clk_rpmh_cliffs},
	{ .compatible = "qcom,niobe-rpmh-clk", .data = &clk_rpmh_niobe},
	{ .compatible = "qcom,volcano-rpmh-clk", .data = &clk_rpmh_volcano},
	{ .compatible = "qcom,anorak-rpmh-clk", .data = &clk_rpmh_anorak},
	{ }
};
MODULE_DEVICE_TABLE(of, clk_rpmh_match_table);

static struct platform_driver clk_rpmh_driver = {
	.probe		= clk_rpmh_probe,
	.driver		= {
		.name	= "clk-rpmh",
		.of_match_table = clk_rpmh_match_table,
	},
};

static int __init clk_rpmh_init(void)
{
	return platform_driver_register(&clk_rpmh_driver);
}
core_initcall(clk_rpmh_init);

static void __exit clk_rpmh_exit(void)
{
	platform_driver_unregister(&clk_rpmh_driver);
}
module_exit(clk_rpmh_exit);

MODULE_DESCRIPTION("QCOM RPMh Clock Driver");
MODULE_LICENSE("GPL v2");
