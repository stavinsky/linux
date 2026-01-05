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
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/soc-component.h>
#include <sound/control.h>

#define CV1800B_RXADC_WORD_LEN 16
#define CV1800B_RXADC_CHANNELS 2

#define CV1800B_RXADC_CTRL0 0x00
#define CV1800B_RXADCC_CTRL1 0x04
#define CV1800B_RXADC_STATUS 0x08
#define CV1800B_RXADC_CLK 0x0c
#define CV1800B_RXADC_ANA0 0x10
#define CV1800B_RXADC_ANA1 0x14
#define CV1800B_RXADC_ANA2 0x18
#define CV1800B_RXADC_ANA3 0x1c
#define CV1800B_RXADC_ANA4 0x20

/* CV1800B_RXADC_CTRL0 */
#define reg_rxadc_en BIT(0) // done
#define reg_i2s_tx_en BIT(1) // done

/* CV1800B_RXADCC_CTRL1 */
#define reg_rxadc_cic_opt GENMASK(1, 0)
#define reg_rxadc_igr_init BIT(8) // done

/* CV1800B_RXADC_ANA0 */
#define reg_gstepl_rxpga GENMASK(12, 0) // done
#define reg_g6dbl_rxpga BIT(13)
#define reg_gainl_rxadc GENMASK(15, 14)
#define reg_gstepr_rxpga GENMASK(28, 16) // done
#define reg_g6dbr_rxpga BIT(29)
#define reg_gainr_rxadc GENMASK(31, 30)
#define reg_comb_left_volume GENMASK(15, 0)
#define reg_comb_right_volume GENMASK(31, 16)

/* CV1800B_RXADC_ANA2 */
#define reg_mutel_rxpga BIT(0)
#define reg_muter_rxpga BIT(1)

/* CV1800B_RXADC_CLK */
#define reg_rxadc_clk_inv BIT(0)
#define reg_rxadc_sck_div GENMASK(15, 8) // done
#define reg_rxadc_dlyen GENMASK(23, 16) // done

static u32 cv1800b_gains[] = {
	0x0001, /* 0dB */
	0x0002, /* 2dB */
	0x0004, /* 4dB */
	0x0008, /* 6dB */
	0x0010, /* 8dB */
	0x0020, /* 10dB */
	0x0040, /* 12dB */
	0x0080, /* 14dB */
	0x0100, /* 16dB */
	0x0200, /* 18dB */
	0x0400, /* 20dB */
	0x0800, /* 22dB */
	0x1000, /* 24dB */
	0x2400, /* 26dB */
	0x2800, /* 28dB */
	0x3000, /* 30dB */
	0x6400, /* 32dB */
	0x6800, /* 34dB */
	0x7000, /* 36dB */
	0xA400, /* 38dB */
	0xA800, /* 40dB */
	0xB000, /* 42dB */
	0xE400, /* 44dB */
	0xE800, /* 46dB */
	0xF000, /* 48dB */
};

struct cv1800b_priv {
	void __iomem *regs;
	struct device *dev;
	unsigned int mclk_rate;
};

static int cv1800b_adc_setbclk_div(struct cv1800b_priv *priv, unsigned int rate)
{
	u32 val;
	u32 bclk_div;

	if (!priv->mclk_rate) {
		dev_err(priv->dev, "mclk_rate is not set");
		return -EINVAL;
	}
	bclk_div = priv->mclk_rate / CV1800B_RXADC_WORD_LEN /
		   CV1800B_RXADC_CHANNELS / rate / 2;
	val = readl(priv->regs + CV1800B_RXADC_CLK);
	val = u32_replace_bits(val, 4 - 1, reg_rxadc_sck_div);
	val = u32_replace_bits(val, 0x19, reg_rxadc_dlyen);
	writel(val, priv->regs + CV1800B_RXADC_CLK);

	return 0;
}

static void cv1800b_adc_enable(struct cv1800b_priv *priv, bool enable)
{
	u32 val;
	val = readl(priv->regs + CV1800B_RXADC_CTRL0);
	val = u32_replace_bits(val, enable, reg_rxadc_en);
	val = u32_replace_bits(val, enable, reg_i2s_tx_en);
	writel(val, priv->regs + CV1800B_RXADC_CTRL0);
}

static unsigned int cv1800b_adc_calc_db(u32 ana0, bool right)
{
	u32 step_mask = right ? FIELD_GET(reg_gstepr_rxpga, ana0) :
				FIELD_GET(reg_gstepl_rxpga, ana0);
	u32 coarse = right ? FIELD_GET(reg_gainr_rxadc, ana0) :
			     FIELD_GET(reg_gainl_rxadc, ana0);
	bool g6db = right ? FIELD_GET(reg_g6dbr_rxpga, ana0) :
			    FIELD_GET(reg_g6dbl_rxpga, ana0);

	u32 step = step_mask ? __ffs(step_mask) : 0;

	step = min(step, 12U);
	coarse = min(coarse, 3U);

	return 2 * step + 6 * coarse + (g6db ? 6 : 0);
}

static int cv1800b_dac_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct cv1800b_priv *priv = snd_soc_dai_get_drvdata(dai);
	u32 val;
	unsigned int rate = params_rate(params);

	cv1800b_adc_setbclk_div(priv, rate);
	/* init adc */
	val = readl(priv->regs + CV1800B_RXADCC_CTRL1);
	val = u32_replace_bits(val, 1, reg_rxadc_igr_init);
	val = u32_replace_bits(val, 0,
			       reg_rxadc_cic_opt); //todo calculcate value
	writel(val, priv->regs + CV1800B_RXADCC_CTRL1);
	return 0;
}

static int cv1800b_dac_dai_trigger(struct snd_pcm_substream *substream, int cmd,
				   struct snd_soc_dai *dai)
{
	struct cv1800b_priv *priv = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		cv1800b_adc_enable(priv, true);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		cv1800b_adc_enable(priv, false);
		break;
	}

	return 0;
}

static int cv1800b_dac_dai_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				      unsigned int freq, int dir)
{
	struct cv1800b_priv *priv = snd_soc_dai_get_drvdata(dai);
	priv->mclk_rate = freq;
	dev_dbg(priv->dev, "mclk is set to %u\n", freq);
	return 0;
}

static const struct snd_soc_dai_ops cv1800b_dac_dai_ops = {
	.hw_params = cv1800b_dac_hw_params,
	.set_sysclk = cv1800b_dac_dai_set_sysclk,
	.trigger = cv1800b_dac_dai_trigger,
};

static struct snd_soc_dai_driver cv1800b_dac_dai = {
	.name = "adc-hifi",
	.capture = { .stream_name = "ADC Capture",
		     .channels_min = 1,
		     .channels_max = 2,
		     .rates = SNDRV_PCM_RATE_8000_48000,
		     .formats = SNDRV_PCM_FMTBIT_S16_LE },
	.ops = &cv1800b_dac_dai_ops,
};

static int cv1800b_adc_volume_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct cv1800b_priv *priv = snd_soc_component_get_drvdata(component);
	u32 ana0 = readl(priv->regs + CV1800B_RXADC_ANA0);

	unsigned int left = cv1800b_adc_calc_db(ana0, false);
	unsigned int right = cv1800b_adc_calc_db(ana0, true);

	ucontrol->value.integer.value[0] = min(left / 2, 24U);
	ucontrol->value.integer.value[1] = min(right / 2, 24U);
	return 0;
}

static int cv1800b_adc_volume_set(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct cv1800b_priv *priv = snd_soc_component_get_drvdata(component);

	u32 v_left = clamp_t(u32, ucontrol->value.integer.value[0], 0, 24);
	u32 v_right = clamp_t(u32, ucontrol->value.integer.value[1], 0, 24);
	u32 val;

	val = readl(priv->regs + CV1800B_RXADC_ANA0);
	val = u32_replace_bits(val, cv1800b_gains[v_left],
			       reg_comb_left_volume);
	val = u32_replace_bits(val, cv1800b_gains[v_right],
			       reg_comb_right_volume);
	writel(val, priv->regs + CV1800B_RXADC_ANA0);

	return 0;
}

static DECLARE_TLV_DB_SCALE(cv1800b_volume_tlv, 0, 200, 0);

static const struct snd_kcontrol_new cv1800b_adc_controls[] = {
	SOC_DOUBLE_EXT_TLV("Volume", SND_SOC_NOPM, 0, 16, 24, false,
			   cv1800b_adc_volume_get, cv1800b_adc_volume_set,
			   cv1800b_volume_tlv),
};

static const struct snd_soc_component_driver cv1800b_adc_component = {
	.name = "cv1800b-adc-codec",
	.controls = cv1800b_adc_controls,
	.num_controls = ARRAY_SIZE(cv1800b_adc_controls),
};

static int cv1800b_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cv1800b_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	platform_set_drvdata(pdev, priv);
	return devm_snd_soc_register_component(
		&pdev->dev, &cv1800b_adc_component, &cv1800b_dac_dai, 1);
}

static const struct of_device_id cv1800b_dac_of_match[] = {
	{ .compatible = "sophgo,cv1800b-adc-codec" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cv1800b_dac_of_match);

static struct platform_driver cv1800b_adc_driver = {
	.probe = cv1800b_adc_probe,
	.driver = {
		.name = "cv1800b-adc-codec",
		.of_match_table = cv1800b_dac_of_match,
	},
};
module_platform_driver(cv1800b_adc_driver);

MODULE_DESCRIPTION("ADC codec for CV1800B");
MODULE_AUTHOR("Anton D. Stavinskii <stavinsky@gmail.com>");
MODULE_LICENSE("GPL");
