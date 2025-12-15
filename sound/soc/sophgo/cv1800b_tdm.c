// SPDX-License-Identifier: GPL-2.0

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <linux/mux/consumer.h>
#include <linux/string.h>
#include <linux/dev_printk.h>

#include <linux/bitfield.h>
#include <linux/bits.h>

#define TX_FIFO_SIZE (1024)
#define RX_FIFO_SIZE (1024)
#define TX_MAX_BURST (8)
#define RX_MAX_BURST (8)

/* tdm registers */
#define CV1800B_BLK_MODE_SETTING  0x000
#define CV1800B_FRAME_SETTING     0x004
#define CV1800B_SLOT_SETTING1     0x008
#define CV1800B_SLOT_SETTING2     0x00C
#define CV1800B_DATA_FORMAT       0x010
#define CV1800B_BLK_CFG           0x014
#define CV1800B_I2S_ENABLE        0x018
#define CV1800B_I2S_RESET         0x01C
#define CV1800B_I2S_INT_EN        0x020
#define CV1800B_I2S_INT           0x024
#define CV1800B_FIFO_THRESHOLD    0x028
#define CV1800B_LRCK_MASTER       0x02C
#define CV1800B_FIFO_RESET        0x030
#define CV1800B_RX_STATUS         0x040
#define CV1800B_TX_STATUS         0x048
#define CV1800B_CLK_CTRL0         0x060
#define CV1800B_CLK_CTRL1         0x064
#define CV1800B_PCM_SYNTH         0x068
#define CV1800B_RX_RD_PORT        0x080
#define CV1800B_TX_WR_PORT        0x0C0

#define CV1800B_MCLK_DIV              1
#define CV1800B_BCLK_DIV             16
#define CV1800B_MCLK_COEF          1024
#define CV1800B_RATE              48000

/* CV1800B_BLK_MODE_SETTING (0x000) */
#define BLK_TX_MODE_MASK              BIT(0)
#define BLK_MASTER_MODE_MASK          BIT(1)
#define BLK_DMA_MODE_MASK             BIT(7)

/* CV1800B_CLK_CTRL1 (0x064) */
#define CLK_MCLK_DIV_MASK GENMASK(15, 0)
#define CLK_BCLK_DIV_MASK GENMASK(31, 16)

/* CV1800B_CLK_CTRL0 (0x060) */
#define CLK_AUD_CLK_SEL_MASK           BIT(0)
#define CLK_BCLK_OUT_CLK_FORCE_EN_MASK BIT(6)
#define CLK_MCLK_OUT_EN_MASK           BIT(7)
#define CLK_AUD_EN_MASK                BIT(8)

/* CV1800B_I2S_RESET (0x01C) */
#define RST_I2S_RESET_RX_MASK BIT(0)
#define RST_I2S_RESET_TX_MASK BIT(1)

/* CV1800B_FIFO_RESET (0x030) */
#define FIFO_RX_RESET_MASK BIT(0)
#define FIFO_TX_RESET_MASK BIT(16)

/* CV1800B_I2S_ENABLE (0x018) */
#define I2S_ENABLE_MASK BIT(0)

/* CV1800B_BLK_CFG (0x014) */
#define BLK_AUTO_DISABLE_WITH_CH_EN_MASK  BIT(4)
#define BLK_RX_BLK_CLK_FORCE_EN_MASK      BIT(8)
#define BLK_RX_FIFO_DMA_CLK_FORCE_EN_MASK BIT(9)
#define BLK_TX_BLK_CLK_FORCE_EN_MASK      BIT(16)
#define BLK_TX_FIFO_DMA_CLK_FORCE_EN_MASK BIT(17)

/* CV1800B_FRAME_SETTING (0x004) */
#define FRAME_LENGTH_MASK     GENMASK(8, 0)
#define FS_ACTIVE_LENGTH_MASK GENMASK(23, 16)

/* CV1800B_I2S_INT_EN (0x020) */
#define INT_I2S_INT_EN_MASK BIT(8)

/* CV1800B_SLOT_SETTING2 (0x00C) */
#define SLOT_EN_MASK GENMASK(15, 0)

/* CV1800B_LRCK_MASTER (0x02C) */
#define LRCK_MASTER_ENABLE_MASK BIT(0)

/* CV1800B_DATA_FORMAT (0x010) */
#define DF_WORD_LENGTH_MASK GENMASK(2, 1)
#define DF_TX_SOURCE_LEFT_ALIGN_MASK BIT(6)

/* CV1800B_FIFO_THRESHOLD (0x028) */
#define FIFO_RX_THRESHOLD_MASK      GENMASK(4, 0)
#define FIFO_TX_THRESHOLD_MASK      GENMASK(20, 16)
#define FIFO_TX_HIGH_THRESHOLD_MASK GENMASK(28, 24)

/* CV1800B_SLOT_SETTING1 (0x008) */
#define SLOT_NUM_MASK  GENMASK(3, 0)
#define SLOT_SIZE_MASK GENMASK(13, 8)
#define DATA_SIZE_MASK GENMASK(20, 16)
#define FB_OFFSET_MASK GENMASK(28, 24)

enum cv1800b_tdm_word_length {
	CV1800B_WORD_LENGTH_8_BIT = 0,
	CV1800B_WORD_LENGTH_16_BIT = 1,
	CV1800B_WORD_LENGTH_32_BIT = 2,
};

struct cv1800b_i2s {
	void __iomem *base;
	struct clk *clk;
	struct clk *clk_mclk;
	struct device *dev;
	struct snd_dmaengine_dai_dma_data playback_dma;
	struct snd_dmaengine_dai_dma_data capture_dma;
};

static void cv1800b_setup_dma_struct(struct cv1800b_i2s *i2s,
				     phys_addr_t phys_base)
{
	i2s->playback_dma.addr = phys_base + CV1800B_TX_WR_PORT;
	i2s->playback_dma.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s->playback_dma.fifo_size = TX_FIFO_SIZE;
	i2s->playback_dma.maxburst = TX_MAX_BURST;

	i2s->capture_dma.addr = phys_base + CV1800B_RX_RD_PORT;
	i2s->capture_dma.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s->capture_dma.fifo_size = RX_FIFO_SIZE;
	i2s->capture_dma.maxburst = RX_MAX_BURST;
}

static const struct snd_dmaengine_pcm_config cv1800b_i2s_pcm_config = {
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
};

static void cv1800b_reset_fifo(struct cv1800b_i2s *i2s)
{
	u32 val;

	val = readl(i2s->base + CV1800B_FIFO_RESET);
	val = u32_replace_bits(val, 1, FIFO_RX_RESET_MASK);
	val = u32_replace_bits(val, 1, FIFO_TX_RESET_MASK);
	writel(val, i2s->base + CV1800B_FIFO_RESET);

	usleep_range(10, 20);

	val = readl(i2s->base + CV1800B_FIFO_RESET);
	val = u32_replace_bits(val, 0, FIFO_RX_RESET_MASK);
	val = u32_replace_bits(val, 0, FIFO_TX_RESET_MASK);
	writel(val, i2s->base + CV1800B_FIFO_RESET);
}

static void cv1800b_reset_i2s(struct cv1800b_i2s *i2s)
{
	u32 val;

	val = readl(i2s->base + CV1800B_I2S_RESET);
	val = u32_replace_bits(val, 1, RST_I2S_RESET_RX_MASK);
	val = u32_replace_bits(val, 1, RST_I2S_RESET_TX_MASK);
	writel(val, i2s->base + CV1800B_I2S_RESET);

	usleep_range(10, 20);

	val = readl(i2s->base + CV1800B_I2S_RESET);
	val = u32_replace_bits(val, 0, RST_I2S_RESET_RX_MASK);
	val = u32_replace_bits(val, 0, RST_I2S_RESET_TX_MASK);
	writel(val, i2s->base + CV1800B_I2S_RESET);
}

static int cv1800b_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	unsigned int rate = params_rate(params);
	unsigned int channels = params_channels(params);
	unsigned int physical_width = params_physical_width(params);
	int width = params_width(params);
	int ret;

	if (width < 0)
		return width;
	struct cv1800b_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	u32 val;
	u32 word_length_val;
	u32 tx_mode;

	ret = clk_set_rate(i2s->clk_mclk, rate * channels * physical_width *
						      CV1800B_BCLK_DIV *
						      CV1800B_MCLK_DIV);
	if (ret)
		return ret;

	val = readl(i2s->base + CV1800B_SLOT_SETTING1);
	val = u32_replace_bits(val, physical_width - 1, SLOT_SIZE_MASK);
	val = u32_replace_bits(val, width - 1, DATA_SIZE_MASK);
	val = u32_replace_bits(val, channels - 1, SLOT_NUM_MASK);
	writel(val, i2s->base + CV1800B_SLOT_SETTING1);

	val = readl(i2s->base + CV1800B_FRAME_SETTING);
	val = u32_replace_bits(val, (physical_width * 2) - 1,
			       FRAME_LENGTH_MASK);
	val = u32_replace_bits(val, (physical_width * 2) - 1,
			       FS_ACTIVE_LENGTH_MASK);
	writel(val, i2s->base + CV1800B_FRAME_SETTING);

	switch (physical_width) {
	case 8:
		word_length_val = CV1800B_WORD_LENGTH_8_BIT;
		break;
	case 16:
		word_length_val = CV1800B_WORD_LENGTH_16_BIT;
		break;
	case 32:
		word_length_val = CV1800B_WORD_LENGTH_32_BIT;
		break;
	default:
		return -EINVAL;
	}

	val = readl(i2s->base + CV1800B_DATA_FORMAT);
	val = u32_replace_bits(val, word_length_val, DF_WORD_LENGTH_MASK);
	writel(val, i2s->base + CV1800B_DATA_FORMAT);

	tx_mode = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? 1 : 0;
	val = readl(i2s->base + CV1800B_BLK_MODE_SETTING);
	val = u32_replace_bits(val, tx_mode, BLK_TX_MODE_MASK);
	writel(val, i2s->base + CV1800B_BLK_MODE_SETTING);

	cv1800b_reset_fifo(i2s);
	cv1800b_reset_i2s(i2s);
	return 0;
}

static int cv1800b_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			       struct snd_soc_dai *dai)
{
	struct cv1800b_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	u32 val;

	val = readl(i2s->base + CV1800B_I2S_ENABLE);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		val = u32_replace_bits(val, 1, I2S_ENABLE_MASK);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		val = u32_replace_bits(val, 0, I2S_ENABLE_MASK);
		break;
	default:
		return -EINVAL;
	}
	writel(val, i2s->base + CV1800B_I2S_ENABLE);
	return 0;
}

static int cv1800b_i2s_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct cv1800b_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_dai_link *dai_link = rtd->dai_link;

	dev_dbg(i2s->dev, "%s: dai=%s substream=%d i2s=%p\n", __func__,
		dai->name, substream->stream, i2s);
	/**
	 * Ensure DMA is stopped before DAI
	 * shutdown (prevents DW AXI DMAC stop/busy on next open).
	 */
	dai_link->trigger_stop = SND_SOC_TRIGGER_ORDER_LDC;
	return 0;
}

static int cv1800b_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct cv1800b_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	if (!i2s) {
		dev_err(dai->dev, "no drvdata in DAI probe\n");
		return -ENODEV;
	}

	snd_soc_dai_init_dma_data(dai, &i2s->playback_dma, &i2s->capture_dma);
	return 0;
}

static int cv1800b_i2s_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct cv1800b_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	u32 val;
	u32 master;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
		dev_dbg(i2s->dev, "set to master mode");
		master = 1;
		break;

	case SND_SOC_DAIFMT_CBC_CFC:
		master = 0;
		break;
	default:
		return -EINVAL;
	}

	val = readl(i2s->base + CV1800B_BLK_MODE_SETTING);
	val = u32_replace_bits(val, master, BLK_MASTER_MODE_MASK);
	writel(val, i2s->base + CV1800B_BLK_MODE_SETTING);
	return 0;
}

static const struct snd_soc_dai_ops cv1800b_i2s_dai_ops = {
	.probe = cv1800b_i2s_dai_probe,
	.startup = cv1800b_i2s_startup,
	.hw_params = cv1800b_i2s_hw_params,
	.trigger = cv1800b_i2s_trigger,
	.set_fmt = cv1800b_i2s_dai_set_fmt,
};

static struct snd_soc_dai_driver cv1800b_i2s_dai_template = {
	.name = "cv1800b-i2s",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &cv1800b_i2s_dai_ops,
};

static const struct snd_soc_component_driver cv1800b_i2s_component = {
	.name = "cv1800b-i2s",
};

static void cv1800b_i2s_hw_disable(struct cv1800b_i2s *i2s)
{
	u32 val;

	val = readl(i2s->base + CV1800B_I2S_ENABLE);
	val = u32_replace_bits(val, 0, I2S_ENABLE_MASK);
	writel(val, i2s->base + CV1800B_I2S_ENABLE);

	val = readl(i2s->base + CV1800B_CLK_CTRL0);
	val = u32_replace_bits(val, 0, CLK_AUD_EN_MASK);
	val = u32_replace_bits(val, 0, CLK_MCLK_OUT_EN_MASK);
	writel(val, i2s->base + CV1800B_CLK_CTRL0);

	val = readl(i2s->base + CV1800B_I2S_RESET);
	val = u32_replace_bits(val, 1, RST_I2S_RESET_RX_MASK);
	val = u32_replace_bits(val, 1, RST_I2S_RESET_TX_MASK);
	writel(val, i2s->base + CV1800B_I2S_RESET);

	val = readl(i2s->base + CV1800B_FIFO_RESET);
	val = u32_replace_bits(val, 1, FIFO_RX_RESET_MASK);
	val = u32_replace_bits(val, 1, FIFO_TX_RESET_MASK);
	writel(val, i2s->base + CV1800B_FIFO_RESET);
}

static void cv1800b_i2s_setup_tdm(struct cv1800b_i2s *i2s)
{
	u32 val;

	val = readl(i2s->base + CV1800B_BLK_MODE_SETTING);
	val = u32_replace_bits(val, 1, BLK_DMA_MODE_MASK);
	writel(val, i2s->base + CV1800B_BLK_MODE_SETTING);

	val = readl(i2s->base + CV1800B_CLK_CTRL1);
	val = u32_replace_bits(val, CV1800B_MCLK_DIV, CLK_MCLK_DIV_MASK);
	val = u32_replace_bits(val, CV1800B_BCLK_DIV, CLK_BCLK_DIV_MASK);
	writel(val, i2s->base + CV1800B_CLK_CTRL1);

	val = readl(i2s->base + CV1800B_CLK_CTRL0);
	val = u32_replace_bits(val, 0, CLK_AUD_CLK_SEL_MASK);
	val = u32_replace_bits(val, 0, CLK_MCLK_OUT_EN_MASK);
	val = u32_replace_bits(val, 1, CLK_AUD_EN_MASK);
	writel(val, i2s->base + CV1800B_CLK_CTRL0);

	val = readl(i2s->base + CV1800B_FIFO_THRESHOLD);
	val = u32_replace_bits(val, 4, FIFO_RX_THRESHOLD_MASK);
	val = u32_replace_bits(val, 4, FIFO_TX_THRESHOLD_MASK);
	val = u32_replace_bits(val, 4, FIFO_TX_HIGH_THRESHOLD_MASK);
	writel(val, i2s->base + CV1800B_FIFO_THRESHOLD);

	val = readl(i2s->base + CV1800B_I2S_ENABLE);
	val = u32_replace_bits(val, 0, I2S_ENABLE_MASK);
	writel(val, i2s->base + CV1800B_I2S_ENABLE);
}

static int cv1800b_try_select_state_optional(struct device *dev,
					     const char *name)
{
	struct mux_state *st;
	int ret;

	st = devm_mux_state_get(dev, name);
	if (IS_ERR(st)) {
		ret = PTR_ERR(st);

		if (ret == -EPROBE_DEFER)
			return ret;
		if (ret == -ENOENT || ret == -ENODEV || ret == -ENODATA)
			return 0;

		dev_err(dev, "mux state '%s' get failed: %d\n", name, ret);
		return ret;
	}

	ret = mux_state_try_select(st);
	if (ret)
		return ret;

	return 0;
}

static int cv1800b_audio_mux_probe(struct device *dev)
{
	static const char * const muxes[] = {
		"fs-in",
		"sclk-in",
		"sdo-out",
		"sdi-in",
	};
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(muxes); i++) {
		ret = cv1800b_try_select_state_optional(dev, muxes[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int cv1800b_i2s_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cv1800b_i2s *i2s;
	struct resource *res;
	void __iomem *regs;
	struct snd_soc_dai_driver *dai;
	int ret;

	i2s = devm_kzalloc(dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);
	i2s->dev = &pdev->dev;
	i2s->base = regs;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;
	cv1800b_setup_dma_struct(i2s, res->start);

	i2s->clk = devm_clk_get_enabled(dev, "i2s");
	if (IS_ERR(i2s->clk))
		return dev_err_probe(dev, PTR_ERR(i2s->clk),
				     "failed to get+enable i2s\n");
	i2s->clk_mclk = devm_clk_get_enabled(dev, "mclk");
	if (IS_ERR(i2s->clk_mclk))
		return dev_err_probe(dev, PTR_ERR(i2s->clk_mclk),
				     "failed to get+enable mclk\n");

	platform_set_drvdata(pdev, i2s);
	cv1800b_i2s_setup_tdm(i2s);

	ret = cv1800b_audio_mux_probe(dev);
	if (ret)
		return ret;

	dai = devm_kmemdup(dev, &cv1800b_i2s_dai_template, sizeof(*dai),
			   GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	ret = devm_snd_soc_register_component(dev, &cv1800b_i2s_component, dai,
					      1);
	if (ret)
		return ret;

	ret = devm_snd_dmaengine_pcm_register(dev, &cv1800b_i2s_pcm_config, 0);
	if (ret) {
		dev_err(dev, "dmaengine_pcm_register failed: %d\n", ret);
		return ret;
	}

	dev_dbg(dev, "cv1800b I2S probed:\n");
	return 0;
}

static void cv1800b_i2s_remove(struct platform_device *pdev)
{
	struct cv1800b_i2s *i2s = platform_get_drvdata(pdev);

	if (!i2s)
		return;
	cv1800b_i2s_hw_disable(i2s);
}

static const struct of_device_id cv1800b_i2s_of_match[] = {
	{ .compatible = "sophgo,cv1800b-i2s" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, cv1800b_i2s_of_match);

static struct platform_driver cv1800b_i2s_driver = {
	.probe = cv1800b_i2s_probe,
	.remove = cv1800b_i2s_remove,
	.driver = {
		.name = "cv1800b-i2s",
		.of_match_table = cv1800b_i2s_of_match,
	},
};
module_platform_driver(cv1800b_i2s_driver);

MODULE_DESCRIPTION("Sophgo cv1800b I2S/TDM driver");
MODULE_AUTHOR("Anton D. Stavinsky <stavinsky@gmail.com>");
MODULE_LICENSE("GPL");
