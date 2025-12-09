// SPDX-License-Identifier: GPL-2.0

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

const struct regmap_config aiao_regmap_cfg = {
	.name = "tdm",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
};

/// aiao fiels
#define I2S_TDM_SCLK_IN_SEL 0x000
#define I2S_TDM_FS_IN_SEL 0x004
#define I2S_TDM_SDI_IN_SEL 0x008
#define I2S_TDM_SDO_OUT_SEL 0x00c
#define I2S_BCLK_OEN_SEL 0x030
#define AUDIO_PDM_CTRL 0x040
#define I2S_SYS_INT_EN 0x060
#define I2S_SYS_INTS 0x064
/// tdm registers
#define I2S_BLK_MODE_SETTING 0x000
#define I2S_FRAME_SETTING 0x004
#define I2S_SLOT_SETTING1 0x008
#define I2S_SLOT_SETTING2 0x00C
#define I2S_DATA_FORMAT 0x010
#define I2S_BLK_CFG 0x014
#define I2S_I2S_ENABLE 0x018
#define I2S_I2S_RESET 0x01C
#define I2S_I2S_INT_EN 0x020
#define I2S_I2S_INT 0x024
#define I2S_FIFO_THRESHOLD 0x028
#define I2S_LRCK_MASTER 0x02C
#define I2S_FIFO_RESET 0x030
#define I2S_RX_STATUS 0x040
#define I2S_TX_STATUS 0x048
#define I2S_CLK_CTRL0 0x060
#define I2S_CLK_CTRL1 0x064
#define I2S_PCM_SYNTH 0x068
#define I2S_RX_RD_PORT 0x080
#define I2S_TX_WR_PORT 0x0C0

enum sg_i2s_capability {
	SG_I2S_CAP_RX,
	SG_I2S_CAP_TX,
	SG_I2S_CAP_TXRX,
};

enum aiao_fields {

	F_I2S_TDM_0_SCLK_IN_SEL,
	F_I2S_TDM_1_SCLK_IN_SEL,
	F_I2S_TDM_2_SCLK_IN_SEL,
	F_I2S_TDM_3_SCLK_IN_SEL,

	F_I2S_TDM_0_FS_IN_SEL,
	F_I2S_TDM_1_FS_IN_SEL,
	F_I2S_TDM_2_FS_IN_SEL,
	F_I2S_TDM_3_FS_IN_SEL,
	F_I2S_TDM_0_SDI_IN_SEL,
	F_I2S_TDM_1_SDI_IN_SEL,
	F_I2S_TDM_2_SDI_IN_SEL,
	F_I2S_TDM_3_SDI_IN_SEL,
	F_I2S_TDM_0_SDO_OUT_SEL,
	F_I2S_TDM_1_SDO_OUT_SEL,
	F_I2S_TDM_2_SDO_OUT_SEL,
	F_I2S_TDM_3_SDO_OUT_SEL,
	F_AUDIO_PDM_SEL_I2S1,
	F_AIAO_MAX_FIELDS,
};
static const struct reg_field aiao_fields[] = {
	[F_I2S_TDM_0_SCLK_IN_SEL] = REG_FIELD(I2S_TDM_SCLK_IN_SEL, 0, 2),
	[F_I2S_TDM_1_SCLK_IN_SEL] = REG_FIELD(I2S_TDM_SCLK_IN_SEL, 4, 6),
	[F_I2S_TDM_2_SCLK_IN_SEL] = REG_FIELD(I2S_TDM_SCLK_IN_SEL, 8, 10),
	[F_I2S_TDM_3_SCLK_IN_SEL] = REG_FIELD(I2S_TDM_SCLK_IN_SEL, 12, 14),

	[F_I2S_TDM_0_FS_IN_SEL] = REG_FIELD(I2S_TDM_FS_IN_SEL, 0, 2),
	[F_I2S_TDM_1_FS_IN_SEL] = REG_FIELD(I2S_TDM_FS_IN_SEL, 4, 6),
	[F_I2S_TDM_2_FS_IN_SEL] = REG_FIELD(I2S_TDM_FS_IN_SEL, 8, 10),
	[F_I2S_TDM_3_FS_IN_SEL] = REG_FIELD(I2S_TDM_FS_IN_SEL, 12, 14),

	[F_I2S_TDM_0_SDI_IN_SEL] = REG_FIELD(I2S_TDM_SDI_IN_SEL, 0, 2),
	[F_I2S_TDM_1_SDI_IN_SEL] = REG_FIELD(I2S_TDM_SDI_IN_SEL, 4, 6),
	[F_I2S_TDM_2_SDI_IN_SEL] = REG_FIELD(I2S_TDM_SDI_IN_SEL, 8, 10),
	[F_I2S_TDM_3_SDI_IN_SEL] = REG_FIELD(I2S_TDM_SDI_IN_SEL, 12, 14),

	[F_I2S_TDM_0_SDO_OUT_SEL] = REG_FIELD(I2S_TDM_SDO_OUT_SEL, 0, 2),
	[F_I2S_TDM_1_SDO_OUT_SEL] = REG_FIELD(I2S_TDM_SDO_OUT_SEL, 4, 6),
	[F_I2S_TDM_2_SDO_OUT_SEL] = REG_FIELD(I2S_TDM_SDO_OUT_SEL, 8, 10),
	[F_I2S_TDM_3_SDO_OUT_SEL] = REG_FIELD(I2S_TDM_SDO_OUT_SEL, 12, 14),
	[F_AUDIO_PDM_SEL_I2S1] = REG_FIELD(AUDIO_PDM_CTRL, 1, 1),
};

enum tdm_fields {
	F_TX_MODE,
	F_MASTER_MODE,
	F_DMA_MODE,
	F_MCLK_DIV,
	F_BCLK_DIV,
	F_AUD_CLK_SEL,
	F_BCLK_OUT_CLK_FORCE_EN,
	F_MCLK_OUT_EN,
	F_AUD_EN,
	F_I2S_RESET_RX,
	F_I2S_RESET_TX,
	F_RX_FIFO_RESET,
	F_TX_FIFO_RESET,
	F_I2S_ENABLE,
	F_RX_BLK_CLK_FORCE_EN,
	F_RX_FIFO_DMA_CLK_FORCE_EN,
	F_TX_BLK_CLK_FORCE_EN,
	F_TX_FIFO_DMA_CLK_FORCE_EN,
	F_FRAME_LENGTH,
	F_FS_ACTIVE_LENGTH,
	F_I2S_INT_EN,
	F_WORD_LENGTH,
	F_RX_FIFO_THRESHOLD,
	F_TX_FIFO_THRESHOLD,
	F_TX_FIFO_HIGH_THRESHOLD,
	F_SLOT_NUM,
	F_SLOT_SIZE,
	F_DATA_SIZE,
	F_FB_OFFSET,
	F_SLOT_EN,
	F_I2S_LRCK_MASTER_ENABLE,
	F_TX_SOURCE_LEFT_ALIGN,
	F_AUTO_DISABLE_WITH_CH_EN,
	/* End of register map */
	F_MAX_FIELDS,
};

static const struct reg_field sg2002_tdm_fields[] = {
	[F_TX_MODE] = REG_FIELD(I2S_BLK_MODE_SETTING, 0, 0),
	[F_MASTER_MODE] = REG_FIELD(I2S_BLK_MODE_SETTING, 1, 1),
	[F_DMA_MODE] = REG_FIELD(I2S_BLK_MODE_SETTING, 7, 7),
	[F_MCLK_DIV] = REG_FIELD(I2S_CLK_CTRL1, 0, 15),
	[F_BCLK_DIV] = REG_FIELD(I2S_CLK_CTRL1, 16, 31),
	[F_AUD_CLK_SEL] = REG_FIELD(I2S_CLK_CTRL0, 0, 0),
	[F_BCLK_OUT_CLK_FORCE_EN] = REG_FIELD(I2S_CLK_CTRL0, 6, 6),
	[F_MCLK_OUT_EN] = REG_FIELD(I2S_CLK_CTRL0, 7, 7),
	[F_AUD_EN] = REG_FIELD(I2S_CLK_CTRL0, 8, 8),
	[F_I2S_RESET_RX] = REG_FIELD(I2S_I2S_RESET, 0, 0),
	[F_I2S_RESET_TX] = REG_FIELD(I2S_I2S_RESET, 1, 1),
	[F_RX_FIFO_RESET] = REG_FIELD(I2S_FIFO_RESET, 0, 0),
	[F_TX_FIFO_RESET] = REG_FIELD(I2S_FIFO_RESET, 16, 16),
	[F_I2S_ENABLE] = REG_FIELD(I2S_I2S_ENABLE, 0, 0),
	[F_RX_BLK_CLK_FORCE_EN] = REG_FIELD(I2S_BLK_CFG, 8, 8),
	[F_RX_FIFO_DMA_CLK_FORCE_EN] = REG_FIELD(I2S_BLK_CFG, 9, 9),
	[F_TX_BLK_CLK_FORCE_EN] = REG_FIELD(I2S_BLK_CFG, 16, 16),
	[F_TX_FIFO_DMA_CLK_FORCE_EN] = REG_FIELD(I2S_BLK_CFG, 17, 17),
	[F_FRAME_LENGTH] = REG_FIELD(I2S_FRAME_SETTING, 0, 8),
	[F_FS_ACTIVE_LENGTH] = REG_FIELD(I2S_FRAME_SETTING, 16, 23),
	[F_I2S_INT_EN] = REG_FIELD(I2S_I2S_INT_EN, 8, 8),
	[F_SLOT_EN] = REG_FIELD(I2S_SLOT_SETTING2, 0, 15),
	[F_I2S_LRCK_MASTER_ENABLE] = REG_FIELD(I2S_LRCK_MASTER, 0, 0),
	[F_TX_SOURCE_LEFT_ALIGN] = REG_FIELD(I2S_DATA_FORMAT, 6, 6),
	[F_AUTO_DISABLE_WITH_CH_EN] = REG_FIELD(I2S_BLK_CFG, 4, 4),
	[F_WORD_LENGTH] = REG_FIELD(I2S_DATA_FORMAT, 1, 2),
	[F_RX_FIFO_THRESHOLD] = REG_FIELD(I2S_FIFO_THRESHOLD, 0, 4),
	[F_TX_FIFO_THRESHOLD] = REG_FIELD(I2S_FIFO_THRESHOLD, 16, 20),
	[F_TX_FIFO_HIGH_THRESHOLD] = REG_FIELD(I2S_FIFO_THRESHOLD, 24, 28),
	[F_SLOT_NUM] = REG_FIELD(I2S_SLOT_SETTING1, 0, 3),
	[F_SLOT_SIZE] = REG_FIELD(I2S_SLOT_SETTING1, 8, 13),
	[F_DATA_SIZE] = REG_FIELD(I2S_SLOT_SETTING1, 16, 20),
	[F_FB_OFFSET] = REG_FIELD(I2S_SLOT_SETTING1, 24, 28),
};
struct sg2002_i2s {
	struct regmap *aiao;
	struct regmap *regs;
	u32 tdm_id;
	struct clk *clk;
	struct clk *clk_mclk;
	struct device *dev;
	struct snd_dmaengine_dai_dma_data playback_dma;
	struct snd_dmaengine_dai_dma_data capture_dma;
	phys_addr_t phys_base;
	struct regmap_field *fields[F_MAX_FIELDS];
	struct regmap_field *aiao_fields[F_AIAO_MAX_FIELDS];
	enum sg_i2s_capability cap;
	bool only_clock_mode;
};

static int sg2002_regmap_init(struct sg2002_i2s *i2s)
{
	struct regmap_field **fields = i2s->fields;
	int i;

	for (i = 0; i < F_MAX_FIELDS; i++) {
		fields[i] = devm_regmap_field_alloc(i2s->dev, i2s->regs,
						    sg2002_tdm_fields[i]);
		if (IS_ERR(i2s->fields[i]))
			return dev_err_probe(i2s->dev, PTR_ERR(i2s->fields[i]),
					     "can't alloc field[%d]\n", i);
	}
	return 0;
};
static int sg2002_aiao_regmap(struct sg2002_i2s *i2s)
{
	struct regmap_field **fields = i2s->aiao_fields;
	int i;

	for (i = 0; i < F_AIAO_MAX_FIELDS; i++) {
		fields[i] = devm_regmap_field_alloc(i2s->dev, i2s->aiao,
						    aiao_fields[i]);
		if (IS_ERR(fields[i]))
			return dev_err_probe(i2s->dev, PTR_ERR(fields[i]),
					     "can't alloc field[%d]\n", i);
	}
	return 0;
}
static int sg2002_setup_dma_struct(struct sg2002_i2s *i2s, struct resource *res)
{
	i2s->phys_base = res->start;

	i2s->playback_dma.addr = i2s->phys_base + I2S_TX_WR_PORT;
	i2s->playback_dma.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s->playback_dma.fifo_size = 1024;
	i2s->playback_dma.maxburst = 8;

	i2s->capture_dma.addr = i2s->phys_base + I2S_RX_RD_PORT;
	i2s->capture_dma.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s->capture_dma.fifo_size = 1024;
	i2s->capture_dma.maxburst = 8;
	return 0;
}

static const struct snd_dmaengine_pcm_config sg_i2s_pcm_config = {
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
};

static int sg_i2s_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct sg2002_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	regmap_field_write(i2s->fields[F_SLOT_SIZE], 32);
	regmap_field_write(i2s->fields[F_DATA_SIZE], 24);
	regmap_field_write(i2s->fields[F_SLOT_NUM], 1);
	regmap_field_write(i2s->fields[F_WORD_LENGTH], 0b10);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		regmap_field_write(i2s->fields[F_TX_MODE], 1);
	else
		regmap_field_write(i2s->fields[F_TX_MODE], 0);
	return 0;
}
static void sg_reset_fifo(struct sg2002_i2s *i2s)
{
	regmap_field_write(i2s->fields[F_RX_FIFO_RESET], 1);
	regmap_field_write(i2s->fields[F_TX_FIFO_RESET], 1);
	udelay(10);
	regmap_field_write(i2s->fields[F_RX_FIFO_RESET], 0);
	regmap_field_write(i2s->fields[F_TX_FIFO_RESET], 0);
};
static void sg_reset_i2s(struct sg2002_i2s *i2s)
{
	regmap_field_write(i2s->fields[F_I2S_RESET_RX], 1);
	regmap_field_write(i2s->fields[F_I2S_RESET_TX], 1);
	udelay(10);
	regmap_field_write(i2s->fields[F_I2S_RESET_RX], 0);
	regmap_field_write(i2s->fields[F_I2S_RESET_TX], 0);
};
static int sg_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			  struct snd_soc_dai *dai)
{
	struct sg2002_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		sg_reset_fifo(i2s);
		sg_reset_i2s(i2s);
		regmap_field_write(i2s->fields[F_I2S_ENABLE], 1);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		regmap_field_write(i2s->fields[F_I2S_ENABLE], 0);
		sg_reset_fifo(i2s);
		sg_reset_i2s(i2s);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sg_i2s_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct sg2002_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	pr_info("%s: dai=%s substream=%d i2s=%p\n", __func__, dai->name,
		substream->stream, i2s);
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai_link *dai_link = rtd->dai_link;

	dai_link->trigger_stop = SND_SOC_TRIGGER_ORDER_LDC;
	return 0;
}

static int sg_i2s_component_probe(struct snd_soc_component *component)
{
	dev_info(component->dev, "I2S component probe\n");
	return 0;
}

static int sg_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct sg2002_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	if (!i2s) {
		dev_err(dai->dev, "no drvdata in DAI probe\n");
		return -ENODEV;
	}

	snd_soc_dai_init_dma_data(dai, &i2s->playback_dma, &i2s->capture_dma);
	snd_soc_dai_set_drvdata(dai, i2s);
	return 0;
}

static const struct snd_soc_dai_ops sg_i2s_dai_ops = {
	.probe = sg_i2s_dai_probe,
	.startup = sg_i2s_startup,
	.hw_params = sg_i2s_hw_params,
	.trigger = sg_i2s_trigger,
};

static struct snd_soc_dai_driver sg_i2s_dai_template = {
	.name = "sg2002-i2s",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE,
	},
		.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE,
	},

	.ops = &sg_i2s_dai_ops,
};
static const struct snd_soc_component_driver sg_i2s_component = {
	.name = "sg2002-i2s",
	.probe = sg_i2s_component_probe,
};

static void sg2002_i2s_hw_disable(struct sg2002_i2s *i2s)
{
	if (!i2s->regs) // todo check if it is correct
		return;

	regmap_field_write(i2s->fields[F_I2S_ENABLE], 0);
	regmap_field_write(i2s->fields[F_AUD_EN], 0);
	regmap_field_write(i2s->fields[F_MCLK_OUT_EN], 0);

	regmap_field_write(i2s->fields[F_I2S_RESET_RX], 1);
	regmap_field_write(i2s->fields[F_I2S_RESET_TX], 1);
	regmap_field_write(i2s->fields[F_RX_FIFO_RESET], 1);
	regmap_field_write(i2s->fields[F_TX_FIFO_RESET], 1);
}

static void setup_aiao(struct sg2002_i2s *i2s)
{
	regmap_field_write(i2s->aiao_fields[F_AUDIO_PDM_SEL_I2S1], 0);
};
static void setup_tdm(struct sg2002_i2s *i2s)
{
	bool is_master_mode =
		of_property_read_bool(i2s->dev->of_node, "sg,tdm-master");
	regmap_field_write(i2s->fields[F_MASTER_MODE], is_master_mode);
	regmap_field_write(i2s->fields[F_DMA_MODE], 1);
	regmap_field_write(i2s->fields[F_MCLK_DIV], 1);
	regmap_field_write(i2s->fields[F_BCLK_DIV], 16);

	regmap_field_write(i2s->fields[F_AUD_CLK_SEL], 0);
	regmap_field_write(i2s->fields[F_MCLK_OUT_EN], 0);
	regmap_field_write(i2s->fields[F_AUD_EN], 1); ///

	regmap_field_write(i2s->fields[F_RX_FIFO_THRESHOLD], 4);
	regmap_field_write(i2s->fields[F_TX_FIFO_THRESHOLD], 4);
	regmap_field_write(i2s->fields[F_TX_FIFO_HIGH_THRESHOLD], 4);

	regmap_field_write(i2s->fields[F_FRAME_LENGTH], 63);
	regmap_field_write(i2s->fields[F_FS_ACTIVE_LENGTH], 31);

	regmap_field_write(i2s->fields[F_I2S_ENABLE], 0); ////
	if (i2s->only_clock_mode)
		regmap_field_write(i2s->fields[F_I2S_LRCK_MASTER_ENABLE], 1);

};
static void sg2002_i2s_mux_setup(struct sg2002_i2s *i2s)
{
	u32 sdi_in;
	u32 sdo_out;
	u32 fs_in;
	u32 sclk_in;

	if (i2s->tdm_id > 3) {
		dev_warn(i2s->dev,
			 "tdm-id %u out of range, skipping mux setup\n",
			 i2s->tdm_id);
		return;
	}

	if (!of_property_read_u32(i2s->dev->of_node, "sg,fs-in", &fs_in)) {
		if (fs_in < 7) {
			switch (i2s->tdm_id) {
			case 0:
				regmap_field_write(
					i2s->aiao_fields[F_I2S_TDM_0_FS_IN_SEL],
					fs_in);
				break;
			case 1:
				regmap_field_write(
					i2s->aiao_fields[F_I2S_TDM_1_FS_IN_SEL],
					fs_in);
				break;
			case 2:
				regmap_field_write(
					i2s->aiao_fields[F_I2S_TDM_2_FS_IN_SEL],
					fs_in);
				break;
			case 3:
				regmap_field_write(
					i2s->aiao_fields[F_I2S_TDM_3_FS_IN_SEL],
					fs_in);
				break;
			}

		} else {
			dev_warn(
				i2s->dev,
				"sg,fs-in %u out of range, skipping mux setup\n",
				fs_in);
		}
	}
	if (!of_property_read_u32(i2s->dev->of_node, "sg,sclk-in", &sclk_in)) {
		if (sclk_in < 7) {
			switch (i2s->tdm_id) {
			case 0:
				regmap_field_write(
					i2s->aiao_fields[F_I2S_TDM_0_SCLK_IN_SEL],
					sclk_in);
				break;
			case 1:
				regmap_field_write(
					i2s->aiao_fields[F_I2S_TDM_1_SCLK_IN_SEL],
					sclk_in);
				break;
			case 2:
				regmap_field_write(
					i2s->aiao_fields[F_I2S_TDM_2_SCLK_IN_SEL],
					sclk_in);
				break;
			case 3:
				regmap_field_write(
					i2s->aiao_fields[F_I2S_TDM_3_SCLK_IN_SEL],
					sclk_in);
				break;
			}

		} else {
			dev_warn(
				i2s->dev,
				"sg,sclk-in %u out of range, skipping mux setup\n",
				sclk_in);
		}
	}
	if (!of_property_read_u32(i2s->dev->of_node, "sg,sdi-in", &sdi_in)) {
		if (sdi_in < 7) {
			switch (i2s->tdm_id) {
			case 0:
				regmap_field_write(
					i2s->aiao_fields[F_I2S_TDM_0_SDI_IN_SEL],
					sdi_in);
				break;
			case 1:
				regmap_field_write(
					i2s->aiao_fields[F_I2S_TDM_1_SDI_IN_SEL],
					sdi_in);
				break;
			case 2:
				regmap_field_write(
					i2s->aiao_fields[F_I2S_TDM_2_SDI_IN_SEL],
					sdi_in);
				break;
			case 3:
				regmap_field_write(
					i2s->aiao_fields[F_I2S_TDM_3_SDI_IN_SEL],
					sdi_in);
				break;
			}

		} else {
			dev_warn(
				i2s->dev,
				"sg,sdi-in %u out of range, skipping mux setup\n",
				sdi_in);
		}
	}
	if (!of_property_read_u32(i2s->dev->of_node, "sg,sdo-out", &sdo_out)) {
		if (sdo_out < 7) {
			switch (i2s->tdm_id) {
			case 0:
				dev_warn(i2s->dev,
					 "tdm0: sg,sdo-out must be 4, got %u\n",
					 sdo_out);
				break;
			case 1:
				regmap_field_write(
					i2s->aiao_fields[F_I2S_TDM_1_SDO_OUT_SEL],
					sdo_out);
				break;
			case 2:
				regmap_field_write(
					i2s->aiao_fields[F_I2S_TDM_2_SDO_OUT_SEL],
					sdo_out);
				break;
			case 3:
				regmap_field_write(
					i2s->aiao_fields[F_I2S_TDM_3_SDO_OUT_SEL],
					sdo_out);
				break;
			}
		} else {
			dev_warn(
				i2s->dev,
				"sg,sdo-out %u out of range, skipping mux setup\n",
				sdo_out);
		}
	}
}
static void sg2002_i2s_set_cap(struct sg2002_i2s *i2s)
{
	const char *cap;

	if (!of_property_read_string(i2s->dev->of_node, "capability", &cap)) {
		if (!strcmp(cap, "rx"))
			i2s->cap = SG_I2S_CAP_RX;

		else if (!strcmp(cap, "tx"))
			i2s->cap = SG_I2S_CAP_TX;

		else if (!strcmp(cap, "txrx"))
			i2s->cap = SG_I2S_CAP_TXRX;

		else
			dev_warn(i2s->dev,
				 "unknown capability '%s', using txrx\n", cap);
	} else {
		dev_info(i2s->dev, "capability missing, default = txrx\n");
		i2s->cap = SG_I2S_CAP_TXRX;
	}
}
static int sg2002_i2s_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sg2002_i2s *i2s;
	struct device_node *np = dev->of_node;
	struct resource *res;
	void __iomem *regs;
	struct snd_soc_dai_driver *dai;
	int ret;

	i2s = devm_kzalloc(dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);
	i2s->dev = &pdev->dev;

	i2s->regs = devm_regmap_init_mmio(dev, regs, &aiao_regmap_cfg);
	if (IS_ERR(i2s->regs))
		return dev_err_probe(dev, PTR_ERR(i2s->regs), "regmap init\n");

	i2s->aiao = syscon_regmap_lookup_by_phandle(np, "aiao");
	if (IS_ERR(i2s->aiao)) {
		return dev_err_probe(dev, PTR_ERR(i2s->aiao),
				     "regmap aiao init\n");
	}

	sg2002_i2s_set_cap(i2s);

	ret = sg2002_regmap_init(i2s);
	if (ret)
		return ret;
	ret = sg2002_aiao_regmap(i2s);
	if (ret)
		return ret;

	sg2002_setup_dma_struct(i2s, res);

	ret = of_property_read_u32(np, "aiao,tdm-id", &i2s->tdm_id);
	if (ret) {
		dev_warn(dev, "aiao,tdm-id missing, defaulting to 0\n");
		i2s->tdm_id = 0;
	}

	/* Get and enable APB I2S clock (clk_apb_i2s2) */
	i2s->clk = devm_clk_get(dev, "i2s");
	if (IS_ERR(i2s->clk))
		return dev_err_probe(dev, PTR_ERR(i2s->clk),
				     "failed to get I2S clock\n");

	ret = clk_prepare_enable(i2s->clk);
	if (ret) {
		dev_err(dev, "failed to enable I2S clock: %d\n", ret);
		return ret;
	}

	i2s->clk_mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(i2s->clk_mclk))
		return dev_err_probe(dev, PTR_ERR(i2s->clk_mclk), "mclk\n");
	ret = clk_prepare_enable(i2s->clk_mclk);
	if (ret)
		return ret;

	i2s->only_clock_mode =
		of_property_read_bool(i2s->dev->of_node, "sg,only-clock");

	platform_set_drvdata(pdev, i2s);
	setup_tdm(i2s);
	setup_aiao(i2s);
	sg2002_i2s_mux_setup(i2s);
	if (i2s->only_clock_mode) {
		dev_info(dev,
			 "tdm-id %d is set to only clock generation mode\n",
			 i2s->tdm_id);
		return 0;
	}

	dai = devm_kmemdup(dev, &sg_i2s_dai_template, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;
	if (i2s->cap == SG_I2S_CAP_RX)
		memset(&dai->playback, 0, sizeof(dai->playback));
	else if (i2s->cap == SG_I2S_CAP_TX)
		memset(&dai->capture, 0, sizeof(dai->capture));


	ret = devm_snd_soc_register_component(dev, &sg_i2s_component, dai, 1);
	if (ret)
		return ret;

	ret = devm_snd_dmaengine_pcm_register(dev, &sg_i2s_pcm_config, 0);
	if (ret) {
		dev_err(dev, "dmaengine_pcm_register failed: %d\n", ret);
		return ret;
	}

	dev_info(dev, "SG2002 I2S probed: tdm-id=%u,\n", i2s->tdm_id);
	return 0;
}

static void sg2002_i2s_remove(struct platform_device *pdev)
{
	struct sg2002_i2s *i2s = platform_get_drvdata(pdev);

	if (!i2s)
		return;

	sg2002_i2s_hw_disable(i2s);

	if (i2s->clk_mclk)
		clk_disable_unprepare(i2s->clk_mclk);
	if (i2s->clk)
		clk_disable_unprepare(i2s->clk);
}

static const struct of_device_id sg2002_i2s_of_match[] = {
	{
		.compatible = "cvitek,cv1835-i2s",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sg2002_i2s_of_match);

static struct platform_driver sg2002_i2s_driver = {
	.probe = sg2002_i2s_probe,
	.remove = sg2002_i2s_remove,
	.driver = {
	    .name = "sg2002-i2s",
	    .of_match_table = sg2002_i2s_of_match,
	},
};
module_platform_driver(sg2002_i2s_driver);

MODULE_DESCRIPTION("Sophgo SG2002 I2S/TDM driver");
MODULE_AUTHOR("Anton D. Stavinsky");
MODULE_LICENSE("GPL");
