// SPDX-License-Identifier: GPL-2.0
/*
 * Simple virtual ADC codec (capture-only)
 *
 * Use when the real ADC is inside the SoC (or otherwise not controlled here),
 * but ASoC needs a codec DAI endpoint for routing with simple-audio-card.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <linux/bitfield.h>
#include <linux/bits.h>

#define CV1800B_RXADC_CTRL0  0x00
#define CV1800B_RXADCC_CTRL1 0x04
#define CV1800B_RXADC_STATUS 0x08
#define CV1800B_RXADC_CLK    0x0c
#define CV1800B_RXADC_ANA0   0x10
#define CV1800B_RXADC_ANA1   0x14
#define CV1800B_RXADC_ANA2   0x18
#define CV1800B_RXADC_ANA3   0x1c
#define CV1800B_RXADC_ANA4   0x20

/* CV1800B_RXADC_CTRL0 */
#define reg_rxadc_en  BIT(0) // done
#define reg_i2s_tx_en BIT(1) // done

/* CV1800B_RXADCC_CTRL1 */
#define reg_rxadc_cic_opt  GENMASK(1, 0)
#define reg_rxadc_igr_init BIT(8) // done

/* CV1800B_RXADC_ANA0 */
#define reg_gstepl_rxpga      GENMASK(12, 0) // done
#define reg_g6dbl_rxpga	      BIT(13)
#define reg_gainl_rxadc	      GENMASK(15, 14)
#define reg_gstepr_rxpga      GENMASK(28, 16) // done
#define reg_g6dbr_rxpga	      BIT(29)
#define reg_gainr_rxadc	      GENMASK(31, 30)
#define reg_comb_left_volume  GENMASK(15, 0)
#define reg_comb_right_volume GENMASK(31, 16)

/* CV1800B_RXADC_ANA2 */
#define reg_mutel_rxpga BIT(0)
#define reg_muter_rxpga BIT(1)

/* CV1800B_RXADC_CLK */
#define reg_rxadc_clk_inv BIT(0)
#define reg_rxadc_sck_div GENMASK(15, 8) // done
#define reg_rxadc_dlyen	  GENMASK(23, 16) // done

struct cv1800b_adc_codec {
	void __iomem *regs;
	struct device *dev;
};

struct cv1800b_rxadc_gain {
	u8 step; /* 0..12 */
	u8 coarse; /* 0..3  (0/6/12/18 dB) */
	bool g6db; /* +6 dB */
};

// static struct cv1800b_rxadc_gain cv1800b_rxadc_decode_db(int db)
// {
// 	struct cv1800b_rxadc_gain g;
// 	int rem;

// 	/* clamp + floor to even */
// 	if (db < 0)
// 		db = 0;
// 	if (db > 48)
// 		db = 48;
// 	db &= ~1;

// 	/* Policy: use g6db only when needed to reach >24 dB */
// 	g.g6db = (db > 24);
// 	rem = db - (g.g6db ? 6 : 0);

// 	g.coarse = rem / 6;
// 	if (g.coarse > 3)
// 		g.coarse = 3;
// 	rem -= g.coarse * 6;

// 	g.step = rem / 2;
// 	if (g.step > 12)
// 		g.step = 12;

// 	return g;
// }

// static u32 cv1800b_rxadc_decode_reg(u32 ana0, bool right)
// {
// 	u32 step_mask;
// 	u8 coarse;
// 	bool g6db;
// 	u8 step;
// 	u32 db;

// 	if (!right) {
// 		step_mask = FIELD_GET(reg_gstepl_rxpga, ana0);
// 		coarse = FIELD_GET(reg_gainl_rxadc, ana0);
// 		g6db = FIELD_GET(reg_g6dbl_rxpga, ana0);
// 	} else {
// 		step_mask = FIELD_GET(reg_gstepr_rxpga, ana0);
// 		coarse = FIELD_GET(reg_gainr_rxadc, ana0);
// 		g6db = FIELD_GET(reg_g6dbr_rxpga, ana0);
// 	}

// 	/* one-hot decode; fall back to 0 if invalid */
// 	if (step_mask)
// 		step = __ffs(step_mask);
// 	else
// 		step = 0;

// 	if (step > 12)
// 		step = 12;
// 	coarse = min_t(u8, coarse, 3);

// 	db = 6 * coarse + 2 * step + (g6db ? 6 : 0);

// 	return db;
// }

static int cv1800b_adc_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct cv1800b_adc_codec *priv = snd_soc_dai_get_drvdata(dai);
	u32 val;

	/* set clock */
	val = readl(priv->regs + CV1800B_RXADC_CLK);
	val = u32_replace_bits(val, 4 -1, reg_rxadc_sck_div);
	val = u32_replace_bits(val, 0x19, reg_rxadc_dlyen);
	writel(val, priv->regs + CV1800B_RXADC_CLK);
	/* init adc */
	val = readl(priv->regs + CV1800B_RXADCC_CTRL1);
	val = u32_replace_bits(val, 1, reg_rxadc_igr_init);
	val = u32_replace_bits(val, 0, reg_rxadc_cic_opt);
	writel(val, priv->regs + CV1800B_RXADCC_CTRL1);

	// todo unhardcode volume
	val = readl(priv->regs + CV1800B_RXADC_ANA0);
	val = u32_replace_bits(val, BIT(12), reg_gstepl_rxpga);
	val = u32_replace_bits(val, BIT(12), reg_gstepr_rxpga);
	writel(val, priv->regs + CV1800B_RXADC_ANA0);
	return 0;
}
static int cv1800b_adc_dai_trigger(struct snd_pcm_substream *substream, int cmd,
				   struct snd_soc_dai *dai)
{
	struct cv1800b_adc_codec *priv = snd_soc_dai_get_drvdata(dai);
	u32 val;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:

		val = readl(priv->regs + CV1800B_RXADC_CTRL0);
		val = u32_replace_bits(val, 1, reg_rxadc_en);
		val = u32_replace_bits(val, 1, reg_i2s_tx_en);
		writel(val, priv->regs + CV1800B_RXADC_CTRL0);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		val = readl(priv->regs + CV1800B_RXADC_CTRL0);
		val = u32_replace_bits(val, 1, reg_rxadc_en);
		val = u32_replace_bits(val, 1, reg_i2s_tx_en);
		writel(val, priv->regs + CV1800B_RXADC_CTRL0);
		break;
	}

	return 0;
}

static int cv1800b_adc_dai_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				      unsigned int freq, int dir)
{
	struct cv1800b_adc_codec *priv = snd_soc_dai_get_drvdata(dai);
	dev_dbg(priv->dev, "codec set_sysclk\n");
	return 0;
}

static int cv1800b_adc_dai_set_bclk_ratio(struct snd_soc_dai *dai,
					  unsigned int ratio)
{
	struct cv1800b_adc_codec *priv = snd_soc_dai_get_drvdata(dai);
	dev_dbg(priv->dev, "codec set_bclk_ratio\n");
	return 0;
}

static const struct snd_soc_dai_ops cv1800b_adc_dai_ops = {
	.hw_params = cv1800b_adc_hw_params,
	.set_sysclk = cv1800b_adc_dai_set_sysclk,
	.set_bclk_ratio = cv1800b_adc_dai_set_bclk_ratio,
	.trigger = cv1800b_adc_dai_trigger,
};

static struct snd_soc_dai_driver cv1800b_adc_dai = {
	.name = "adc-hifi",
	.capture = { .stream_name = "ADC Capture",
		     .channels_min = 1,
		     .channels_max = 2,
		     .rates = SNDRV_PCM_RATE_8000_48000,
		     .formats = SNDRV_PCM_FMTBIT_S16_LE },
	.ops = &cv1800b_adc_dai_ops,
};

static const struct snd_soc_component_driver cv1800b_adc_component = {
	.name = "cv1800b-adc-codec",
};

static int cv1800b_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cv1800b_adc_codec *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	platform_set_drvdata(pdev, priv);
	return devm_snd_soc_register_component(
		&pdev->dev, &cv1800b_adc_component, &cv1800b_adc_dai, 1);
}

static const struct of_device_id cv1800b_adc_of_match[] = {
	{ .compatible = "sophgo,cv1800b-adc-codec" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cv1800b_adc_of_match);

static struct platform_driver cv1800b_adc_driver = {
	.probe = cv1800b_adc_probe,
	.driver = {
		.name = "cv1800b-adc-codec",
		.of_match_table = cv1800b_adc_of_match,
	},
};
module_platform_driver(cv1800b_adc_driver);

MODULE_DESCRIPTION("ADC codec for CV1800B");
MODULE_AUTHOR("Anton D. Stavinskii <stavinsky@gmail.com>");
MODULE_LICENSE("GPL");