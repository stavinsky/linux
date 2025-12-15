// SPDX-License-Identifier: GPL-2.0
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mux/driver.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#define CV1800B_TDM_MAX 4

#define CV1800B_I2S_TDM_SCLK_IN_SEL    0x000
#define CV1800B_I2S_TDM_FS_IN_SEL      0x004
#define CV1800B_I2S_TDM_SDI_IN_SEL     0x008
#define CV1800B_I2S_TDM_SDO_OUT_SEL    0x00c

#define CV1800B_I2S_BCLK_OEN_SEL       0x030
#define CV1800B_AUDIO_PDM_CTRL         0x040

/* 3-bit fields per TDM, packed at [2:0], [6:4], [10:8], [14:12] */
#define CV1800B_TDM0_SEL_MASK             GENMASK(2, 0)
#define CV1800B_TDM1_SEL_MASK             GENMASK(6, 4)
#define CV1800B_TDM2_SEL_MASK             GENMASK(10, 8)
#define CV1800B_TDM3_SEL_MASK             GENMASK(14, 12)

/* AUDIO_PDM_CTRL */
#define CV1800B_AUDIO_PDM_SEL_I2S1_MASK   BIT(1)

enum cv1800b_sig {
	SIG_SCLK_IN,
	SIG_FS_IN,
	SIG_SDI_IN,
	SIG_SDO_OUT,
	SIG_MAX,
};

static inline enum cv1800b_sig idx_to_sig(unsigned int idx)
{
	return idx / CV1800B_TDM_MAX;
}

static inline unsigned int idx_to_tdm(unsigned int idx)
{
	return idx % CV1800B_TDM_MAX;
}

/* For each signal, which register contains its fields */
static const unsigned int sig_reg[SIG_MAX] = {
	[SIG_SCLK_IN] = CV1800B_I2S_TDM_SCLK_IN_SEL,
	[SIG_FS_IN]   = CV1800B_I2S_TDM_FS_IN_SEL,
	[SIG_SDI_IN]  = CV1800B_I2S_TDM_SDI_IN_SEL,
	[SIG_SDO_OUT] = CV1800B_I2S_TDM_SDO_OUT_SEL,
};

static const unsigned int tdm_field[CV1800B_TDM_MAX] = {
	CV1800B_TDM0_SEL_MASK,
	CV1800B_TDM1_SEL_MASK,
	CV1800B_TDM2_SEL_MASK,
	CV1800B_TDM3_SEL_MASK,
};

struct cv1800b_aiao_mux {
	struct device *dev;
	void __iomem *base;
};

static int cv1800b_mux_set(struct mux_control *mux, int state)
{
	struct cv1800b_aiao_mux *priv = mux_chip_priv(mux->chip);
	u32 idx = mux_control_get_index(mux);
	enum cv1800b_sig sig = idx_to_sig(idx);
	u32 tdm = idx_to_tdm(idx);
	u32 shift = tdm * 4;
	u32 tmp;
	void __iomem *reg;

	if (sig >= SIG_MAX || tdm >= CV1800B_TDM_MAX)
		return -EINVAL;

	if (state < 0 || state > 7)
		return -EINVAL;

	reg = priv->base + sig_reg[sig];
	tmp = readl(reg);
	tmp &= ~(0x7 << shift);
	tmp |= ((u32)state & 0x7) << shift;
	writel(tmp, reg);

	dev_dbg(priv->dev, "mux is set for tdm: %i, value: %i, sig: %i", tdm,
		state, sig);
	return 0;
}

static const struct mux_control_ops cv1800b_mux_ops = {
	.set = cv1800b_mux_set,
};

static int cv1800b_aiao_mux_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cv1800b_aiao_mux *priv;
	struct mux_chip *mux_chip;
	void __iomem *regs;
	int ret;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	mux_chip = devm_mux_chip_alloc(dev, CV1800B_TDM_MAX * SIG_MAX,
				       sizeof(*priv));
	if (IS_ERR(mux_chip))
		return PTR_ERR(mux_chip);

	mux_chip->ops = &cv1800b_mux_ops;

	priv = mux_chip_priv(mux_chip);
	priv->dev = dev;
	priv->base = regs;

	for (int i = 0; i < mux_chip->controllers; i++)
		mux_chip->mux[i].states = 8;

	ret = devm_mux_chip_register(dev, mux_chip);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, priv);
	return 0;
}

static const struct of_device_id cv1800b_aiao_mux_of_match[] = {
	{ .compatible = "sophgo,cv1800b-aiao-mux" },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, cv1800b_aiao_mux_of_match);

static struct platform_driver cv1800b_aiao_mux_driver = {
	.probe = cv1800b_aiao_mux_probe,
	.driver = {
		.name = "cv1800b-aiao-mux",
		.of_match_table = cv1800b_aiao_mux_of_match,
	},
};
module_platform_driver(cv1800b_aiao_mux_driver);
MODULE_DESCRIPTION("Sophgo cv1800b I2S/TDM Multiplexer driver");
MODULE_AUTHOR("Anton D. Stavinsky <stavinsky@gmail.com>");
MODULE_LICENSE("GPL");
