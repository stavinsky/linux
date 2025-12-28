// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/clk.h>

#define CV1800B_TDM_MAX	    4
#define MUX_UNSET	    (-1)
#define MAX_VALID_MUX_STATE 7

#define CV1800B_I2S_TDM_SCLK_IN_SEL 0x000
#define CV1800B_I2S_TDM_FS_IN_SEL   0x004
#define CV1800B_I2S_TDM_SDI_IN_SEL  0x008
#define CV1800B_I2S_TDM_SDO_OUT_SEL 0x00c
#define CV1800B_I2S_BCLK_OEN_SEL    0x030
#define CV1800B_AUDIO_PDM_CTRL	    0x040

enum cv1800b_mux_items {
	CV1800B_MUX_SCLK_IN,
	CV1800B_MUX_FS_IN,
	CV1800B_MUX_SDI_IN,
	CV1800B_MUX_SDO_OUT,
	CV1800B_MUX_MAX
};

static const char *const cv1800b_aiao_opt_props[] = {
	[CV1800B_MUX_SCLK_IN] = "sophgo,sclk-in",
	[CV1800B_MUX_FS_IN] = "sophgo,fs-in",
	[CV1800B_MUX_SDI_IN] = "sophgo,sdi-in",
	[CV1800B_MUX_SDO_OUT] = "sophgo,sdo-out",
};

static const unsigned int cv1800b_aiao_mux_reg[CV1800B_MUX_MAX] = {
	[CV1800B_MUX_SCLK_IN] = CV1800B_I2S_TDM_SCLK_IN_SEL,
	[CV1800B_MUX_FS_IN] = CV1800B_I2S_TDM_FS_IN_SEL,
	[CV1800B_MUX_SDI_IN] = CV1800B_I2S_TDM_SDI_IN_SEL,
	[CV1800B_MUX_SDO_OUT] = CV1800B_I2S_TDM_SDO_OUT_SEL,
};
struct cv1800b_aiao_link_priv {
	bool is_internal;
};
struct cv1800b_aiao_card_priv {
	void __iomem *regs;
	struct device *dev;
	struct snd_soc_card card;
	struct snd_soc_dai_link *links;
	int mux[CV1800B_MUX_MAX][CV1800B_TDM_MAX];
	u8 mux_sdo_used;
	struct cv1800b_aiao_link_priv *links_cfg;
};

static int cv1800b_aiao_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct cv1800b_aiao_card_priv *priv = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *dai;
	struct cv1800b_aiao_link_priv *cfg =
		&priv->links_cfg[rtd->dai_link->id];
	/* we support only i2s 32 bit per channel and 2 channels*/
	u32 tdm_slots = 2;
	u32 tdm_slot_width = 32;
	u32 bclk_ratio = tdm_slot_width * tdm_slots;
	u32 tx_mask = (1U << tdm_slots) - 1;
	u32 rx_mask = (1U << tdm_slots) - 1;
	unsigned int rate = params_rate(params);
	int ret;
	u32 i;
	u32 target_mclk = rate * 256; // 12288000 for 48khz

	for_each_rtd_cpu_dais(rtd, i, dai) {
		ret = snd_soc_dai_set_sysclk(dai, 0, target_mclk,
					     SND_SOC_CLOCK_OUT);
		if (ret) {
			dev_dbg(priv->dev, "1\n");
			return ret;
		}
		ret = snd_soc_dai_set_bclk_ratio(dai, bclk_ratio);
		if (ret) {
			dev_dbg(priv->dev, "2\n");
			return ret;
		}
		ret = snd_soc_dai_set_tdm_slot(dai, tx_mask, rx_mask, tdm_slots,
					       tdm_slot_width);
		if (ret) {
			dev_dbg(priv->dev, "3\n");
			return ret;
		}
	}
	// if (cfg->is_internal) {
	// 	for_each_rtd_codec_dais(rtd, i, dai) {
	// 		ret = snd_soc_dai_set_sysclk(dai, 0, target_mclk,
	// 					     SND_SOC_CLOCK_OUT);
	// 		if (ret) {
	// 			dev_dbg(priv->dev, "4\n");
	// 			return ret;
	// 		}
	// 		ret = snd_soc_dai_set_bclk_ratio(dai, bclk_ratio);
	// 		if (ret) {
	// 			dev_dbg(priv->dev, "5\n");
	// 			return ret;
	// 		}
	// 	}
	// }
	return 0;
}
static void cv1800b_aiao_link_shutdown(struct snd_pcm_substream *substream)
{
	// struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	// struct cv1800b_aiao_card_priv *priv =
	// 	snd_soc_card_get_drvdata(rtd->card);
}

static const struct snd_soc_ops cv1800b_aiao_card_ops = {
	.hw_params = cv1800b_aiao_hw_params,
	.shutdown = cv1800b_aiao_link_shutdown,
};

static unsigned int
cv1800b_aiao_parse_dai_format(struct cv1800b_aiao_card_priv *priv,
			      struct device_node *ln, struct device_node *codec,
			      struct device_node *cpu)
{
	unsigned int fmt;

	struct device_node *bitclkmaster = NULL;
	struct device_node *framemaster = NULL;

	fmt = snd_soc_daifmt_parse_format(ln, NULL);
	if (!(fmt & SND_SOC_DAIFMT_FORMAT_MASK))
		fmt |= SND_SOC_DAIFMT_I2S;

	snd_soc_daifmt_parse_clock_provider_as_phandle(ln, NULL, &bitclkmaster,
						       &framemaster);

	if (bitclkmaster == codec && framemaster == codec)
		fmt |= SND_SOC_DAIFMT_CBP_CFP;
	else
		fmt |= SND_SOC_DAIFMT_CBC_CFC;

	dev_dbg(priv->dev, "codec==bitclkmaster: %u, codec==framemaster: %u",
		codec == bitclkmaster, codec == framemaster);
	dev_dbg(priv->dev, "codec=%pOF cpu=%pOF bit=%pOF frame=%pOF\n", codec,
		cpu, bitclkmaster, framemaster);

	of_node_put(bitclkmaster);
	of_node_put(framemaster);
	return fmt;
}

static int cv1800b_aiao_parse_mux_config(struct cv1800b_aiao_card_priv *priv,
					 struct device_node *ln,
					 struct snd_soc_dai_link_component *cpu)
{
	u32 tdm_id;
	int ret;
	u32 i;
	u32 val;

	ret = of_property_read_u32(cpu->of_node, "sophgo,tdm-id", &tdm_id);
	if (ret || tdm_id >= CV1800B_TDM_MAX)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(cv1800b_aiao_opt_props); i++) {
		ret = of_property_read_u32(ln, cv1800b_aiao_opt_props[i], &val);
		/* mux properties are optional */
		if (ret)
			continue;
		if (val > MAX_VALID_MUX_STATE) {
			dev_dbg(priv->dev, "wrong mux value for link %s\n",
				ln->name);
			return -EINVAL;
		}
		/* if two tdm request the same output result is unknown */
		if (i == CV1800B_MUX_SDO_OUT) {
			int cur = priv->mux[i][tdm_id];
			/* no override */
			if (cur != MUX_UNSET && cur != val)
				return -EINVAL;
			if (cur == MUX_UNSET &&
			    (priv->mux_sdo_used & (u8)BIT(val)))
				return -EINVAL;
			if (cur == MUX_UNSET)
				priv->mux_sdo_used |= (u8)BIT(val);
		}
		priv->mux[i][tdm_id] = val;
	}
	return 0;
}

static int cv1800b_aiao_parse_dai_link(struct device_node *ln,
				       struct cv1800b_aiao_card_priv *priv,
				       int i)
{
	struct snd_soc_dai_link *link = &priv->links[i];
	struct device_node *cpu_node = NULL;
	struct device_node *codec_node = NULL;
	struct device *dev = priv->dev;
	int ret;
	const char *link_name;
	struct cv1800b_aiao_link_priv *cfg;

	link->id = i;
	cfg = &priv->links_cfg[link->id];
	cpu_node = of_get_child_by_name(ln, "cpu");
	if (!cpu_node) {
		ret = -ENODEV;
		goto put_nodes;
	}

	ret = snd_soc_of_get_dai_link_cpus(priv->dev, cpu_node, link);
	if (ret) {
		dev_dbg(dev, "link: %i, cpu not found\n", i);
		goto put_nodes;
	}

	codec_node = of_get_child_by_name(ln, "codec");
	if (!codec_node) {
		ret = -ENODEV;
		goto put_nodes;
	}
	ret = snd_soc_of_get_dai_link_codecs(priv->dev, codec_node, link);
	if (ret) {
		dev_dbg(dev, "link: %i, codec not found\n", i);
		goto put_nodes;
	}
	link->platforms = link->cpus;
	link->num_platforms = link->num_cpus;

	if (!of_property_read_string(ln, "link-name", &link_name))
		link->name = devm_kstrdup(dev, link_name, GFP_KERNEL);
	else
		link->name = devm_kasprintf(dev, GFP_KERNEL, "link%d", i);
	link->stream_name = link->name;

	link->dai_fmt =
		cv1800b_aiao_parse_dai_format(priv, ln, codec_node, cpu_node);

	ret = cv1800b_aiao_parse_mux_config(priv, ln, &link->cpus[0]);
	if (ret)
		goto put_nodes;

	cfg->is_internal = of_device_is_compatible(link->codecs[0].of_node,
						   "sophgo,cv1800b-adc-codec");
	if (cfg->is_internal)
		dev_dbg(dev, "%s is using internal codec", link->name);
	link->ops = &cv1800b_aiao_card_ops;

put_nodes:
	of_node_put(cpu_node);
	of_node_put(codec_node);
	if (ret) {
		snd_soc_of_put_dai_link_codecs(link);
		snd_soc_of_put_dai_link_cpus(link);
		link->platforms = NULL;
		link->num_platforms = 0;
		return ret;
	}
	return 0;
}

static void cv1800b_aiao_update_mux_field(u32 tdm, u32 val, u32 *reg)
{
	u32 shift = tdm * 4;
	*reg &= ~(0x7 << shift);
	*reg |= (val & 0x7) << shift;
}

static void cv1800b_aiao_set_mux_config(struct cv1800b_aiao_card_priv *priv)
{
	u32 tdm;
	int i;
	u32 val;

	for (i = 0; i < ARRAY_SIZE(cv1800b_aiao_mux_reg); i++) {
		void __iomem *addr =
			(u8 __iomem *)priv->regs + cv1800b_aiao_mux_reg[i];
		u32 reg = readl(addr);
		for (tdm = 0; tdm < CV1800B_TDM_MAX; tdm++) {
			val = priv->mux[i][tdm];
			if (val == MUX_UNSET)
				continue;
			cv1800b_aiao_update_mux_field(tdm, val, &reg);
		}
		writel(reg, addr);
		dev_dbg(priv->dev, "%s: %#x\n", cv1800b_aiao_opt_props[i], reg);
	}
}

static int cv1800b_aiao_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node, *ln;
	struct cv1800b_aiao_card_priv *priv;
	const char *model = "cv1800b audio";
	int nlinks = 0, i = 0;
	int ret, tdm, mux;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, priv);

	priv->dev = dev;
	priv->mux_sdo_used = 0;
	priv->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	for (tdm = 0; tdm < CV1800B_TDM_MAX; tdm++)
		for (mux = 0; mux < CV1800B_MUX_MAX; mux++)
			priv->mux[mux][tdm] = MUX_UNSET;

	for_each_available_child_of_node(np, ln) {
		if (of_node_name_prefix(ln, "link@"))
			nlinks += 1;
	}
	dev_dbg(dev, "found %i links\n", nlinks);
	if (nlinks == 0)
		return -ENODEV;

	priv->links_cfg =
		devm_kcalloc(dev, nlinks, sizeof(*priv->links_cfg), GFP_KERNEL);
	if (!priv->links_cfg)
		return -ENOMEM;

	priv->links =
		devm_kcalloc(dev, nlinks, sizeof(*priv->links), GFP_KERNEL);
	if (!priv->links)
		return -ENOMEM;

	of_property_read_string(np, "model", &model);

	for_each_available_child_of_node(np, ln) {
		if (!of_node_name_prefix(ln, "link@"))
			continue;
		ret = cv1800b_aiao_parse_dai_link(ln, priv, i);
		if (ret) {
			of_node_put(ln);
			return ret;
		}
		i++;
	}

	priv->card.name = model;
	priv->card.dev = dev;
	priv->card.owner = THIS_MODULE;
	priv->card.dai_link = priv->links;
	priv->card.num_links = nlinks;

	cv1800b_aiao_set_mux_config(priv);

	snd_soc_card_set_drvdata(&priv->card, priv);
	return devm_snd_soc_register_card(dev, &priv->card);
}
static void cv1800b_aiao_remove(struct platform_device *pdev)
{
	// struct cv1800b_aiao_card_priv *priv = platform_get_drvdata(pdev);
	// struct cv1800b_aiao_link_priv *cfg;
	// int i;

	// todo remove me before submit
}
static const struct of_device_id cv1800b_aiao_of_match[] = {
	{ .compatible = "sophgo,cv1800b-aiao" },
	{}
};

MODULE_DEVICE_TABLE(of, cv1800b_aiao_of_match);

static struct platform_driver cv1800b_aiao_driver = {
	.probe = cv1800b_aiao_probe,
	.remove= cv1800b_aiao_remove,
	.driver = {
		.name = "cv1800b-sound-card",
		.of_match_table = cv1800b_aiao_of_match,
	},
};

module_platform_driver(cv1800b_aiao_driver);

MODULE_DESCRIPTION("SG2002 sound card");
MODULE_LICENSE("GPL");