/*
 *  PCM - Linear conversion
 *  Copyright (c) 2000 by Abramo Bagnara <abramo@alsa-project.org>
 *
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
  
#include <byteswap.h>
#include "pcm_local.h"
#include "pcm_plugin.h"

typedef struct {
	/* This field need to be the first */
	snd_pcm_plugin_t plug;
	int conv_idx;
	snd_pcm_format_t sformat;
} snd_pcm_linear_t;

int snd_pcm_linear_convert_index(snd_pcm_format_t src_format,
				 snd_pcm_format_t dst_format)
{
	int src_endian, dst_endian, sign, src_width, dst_width;

	sign = (snd_pcm_format_signed(src_format) !=
		snd_pcm_format_signed(dst_format));
#ifdef SND_LITTLE_ENDIAN
	src_endian = snd_pcm_format_big_endian(src_format);
	dst_endian = snd_pcm_format_big_endian(dst_format);
#else
	src_endian = snd_pcm_format_little_endian(src_format);
	dst_endian = snd_pcm_format_little_endian(dst_format);
#endif

	if (src_endian < 0)
		src_endian = 0;
	if (dst_endian < 0)
		dst_endian = 0;

	src_width = snd_pcm_format_width(src_format) / 8 - 1;
	dst_width = snd_pcm_format_width(dst_format) / 8 - 1;

	return src_width * 32 + src_endian * 16 + sign * 8 + dst_width * 2 + dst_endian;
}

int snd_pcm_linear_get_index(snd_pcm_format_t src_format, snd_pcm_format_t dst_format)
{
	int sign, width, endian;
	sign = (snd_pcm_format_signed(src_format) != 
		snd_pcm_format_signed(dst_format));
	width = snd_pcm_format_width(src_format) / 8 - 1;
#ifdef SND_LITTLE_ENDIAN
	endian = snd_pcm_format_big_endian(src_format);
#else
	endian = snd_pcm_format_little_endian(src_format);
#endif
	if (endian < 0)
		endian = 0;
	return width * 4 + endian * 2 + sign;
}

int snd_pcm_linear_put_index(snd_pcm_format_t src_format, snd_pcm_format_t dst_format)
{
	int sign, width, endian;
	sign = (snd_pcm_format_signed(src_format) != 
		snd_pcm_format_signed(dst_format));
	width = snd_pcm_format_width(dst_format) / 8 - 1;
#ifdef SND_LITTLE_ENDIAN
	endian = snd_pcm_format_big_endian(dst_format);
#else
	endian = snd_pcm_format_little_endian(dst_format);
#endif
	if (endian < 0)
		endian = 0;
	return width * 4 + endian * 2 + sign;
}

void snd_pcm_linear_convert(const snd_pcm_channel_area_t *dst_areas, snd_pcm_uframes_t dst_offset,
			    const snd_pcm_channel_area_t *src_areas, snd_pcm_uframes_t src_offset,
			    unsigned int channels, snd_pcm_uframes_t frames, int convidx)
{
#define CONV_LABELS
#include "plugin_ops.h"
#undef CONV_LABELS
	void *conv = conv_labels[convidx];
	unsigned int channel;
	for (channel = 0; channel < channels; ++channel) {
		const char *src;
		char *dst;
		int src_step, dst_step;
		snd_pcm_uframes_t frames1;
		const snd_pcm_channel_area_t *src_area = &src_areas[channel];
		const snd_pcm_channel_area_t *dst_area = &dst_areas[channel];
#if 0
		if (!src_area->enabled) {
			if (dst_area->wanted)
				snd_pcm_area_silence(dst_area, dst_offset, frames, dst_sfmt);
			dst_area->enabled = 0;
			continue;
		}
		dst_area->enabled = 1;
#endif
		src = snd_pcm_channel_area_addr(src_area, src_offset);
		dst = snd_pcm_channel_area_addr(dst_area, dst_offset);
		src_step = snd_pcm_channel_area_step(src_area);
		dst_step = snd_pcm_channel_area_step(dst_area);
		frames1 = frames;
		while (frames1-- > 0) {
			goto *conv;
#define CONV_END after
#include "plugin_ops.h"
#undef CONV_END
		after:
			src += src_step;
			dst += dst_step;
		}
	}
}

static int snd_pcm_linear_hw_refine_cprepare(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t *params)
{
	int err;
	snd_pcm_access_mask_t access_mask = { SND_PCM_ACCBIT_PLUGIN };
	snd_pcm_format_mask_t format_mask = { SND_PCM_FMTBIT_LINEAR };
	err = _snd_pcm_hw_param_set_mask(params, SND_PCM_HW_PARAM_ACCESS,
					 &access_mask);
	if (err < 0)
		return err;
	err = _snd_pcm_hw_param_set_mask(params, SND_PCM_HW_PARAM_FORMAT,
					 &format_mask);
	if (err < 0)
		return err;
	err = _snd_pcm_hw_params_set_subformat(params, SND_PCM_SUBFORMAT_STD);
	if (err < 0)
		return err;
	params->info &= ~(SND_PCM_INFO_MMAP | SND_PCM_INFO_MMAP_VALID);
	return 0;
}

static int snd_pcm_linear_hw_refine_sprepare(snd_pcm_t *pcm, snd_pcm_hw_params_t *sparams)
{
	snd_pcm_linear_t *linear = pcm->private_data;
	snd_pcm_access_mask_t saccess_mask = { SND_PCM_ACCBIT_MMAP };
	_snd_pcm_hw_params_any(sparams);
	_snd_pcm_hw_param_set_mask(sparams, SND_PCM_HW_PARAM_ACCESS,
				   &saccess_mask);
	_snd_pcm_hw_params_set_format(sparams, linear->sformat);
	_snd_pcm_hw_params_set_subformat(sparams, SND_PCM_SUBFORMAT_STD);
	return 0;
}

static int snd_pcm_linear_hw_refine_schange(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t *params,
					    snd_pcm_hw_params_t *sparams)
{
	int err;
	unsigned int links = (SND_PCM_HW_PARBIT_CHANNELS |
			      SND_PCM_HW_PARBIT_RATE |
			      SND_PCM_HW_PARBIT_PERIOD_SIZE |
			      SND_PCM_HW_PARBIT_BUFFER_SIZE |
			      SND_PCM_HW_PARBIT_PERIODS |
			      SND_PCM_HW_PARBIT_PERIOD_TIME |
			      SND_PCM_HW_PARBIT_BUFFER_TIME |
			      SND_PCM_HW_PARBIT_TICK_TIME);
	err = _snd_pcm_hw_params_refine(sparams, links, params);
	if (err < 0)
		return err;
	return 0;
}
	
static int snd_pcm_linear_hw_refine_cchange(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t *params,
					    snd_pcm_hw_params_t *sparams)
{
	int err;
	unsigned int links = (SND_PCM_HW_PARBIT_CHANNELS |
			      SND_PCM_HW_PARBIT_RATE |
			      SND_PCM_HW_PARBIT_PERIOD_SIZE |
			      SND_PCM_HW_PARBIT_BUFFER_SIZE |
			      SND_PCM_HW_PARBIT_PERIODS |
			      SND_PCM_HW_PARBIT_PERIOD_TIME |
			      SND_PCM_HW_PARBIT_BUFFER_TIME |
			      SND_PCM_HW_PARBIT_TICK_TIME);
	err = _snd_pcm_hw_params_refine(params, links, sparams);
	if (err < 0)
		return err;
	return 0;
}

static int snd_pcm_linear_hw_refine(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	return snd_pcm_hw_refine_slave(pcm, params,
				       snd_pcm_linear_hw_refine_cprepare,
				       snd_pcm_linear_hw_refine_cchange,
				       snd_pcm_linear_hw_refine_sprepare,
				       snd_pcm_linear_hw_refine_schange,
				       snd_pcm_plugin_hw_refine_slave);
}

static int snd_pcm_linear_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	snd_pcm_linear_t *linear = pcm->private_data;
	int err = snd_pcm_hw_params_slave(pcm, params,
					  snd_pcm_linear_hw_refine_cchange,
					  snd_pcm_linear_hw_refine_sprepare,
					  snd_pcm_linear_hw_refine_schange,
					  snd_pcm_plugin_hw_params_slave);
	if (err < 0)
		return err;
	if (pcm->stream == SND_PCM_STREAM_PLAYBACK)
		linear->conv_idx = snd_pcm_linear_convert_index(snd_pcm_hw_params_get_format(params),
								linear->sformat);
	else
		linear->conv_idx = snd_pcm_linear_convert_index(linear->sformat,
								snd_pcm_hw_params_get_format(params));
	return 0;
}

static snd_pcm_sframes_t snd_pcm_linear_write_areas(snd_pcm_t *pcm,
					  const snd_pcm_channel_area_t *areas,
					  snd_pcm_uframes_t offset,
					  snd_pcm_uframes_t size,
					  snd_pcm_uframes_t *slave_sizep)
{
	snd_pcm_linear_t *linear = pcm->private_data;
	snd_pcm_t *slave = linear->plug.slave;
	snd_pcm_uframes_t xfer = 0;
	snd_pcm_sframes_t err = 0;
	if (slave_sizep && *slave_sizep < size)
		size = *slave_sizep;
	assert(size > 0);
	while (xfer < size) {
		snd_pcm_uframes_t frames = snd_pcm_mmap_playback_xfer(slave, size - xfer);
		snd_pcm_linear_convert(snd_pcm_mmap_areas(slave), snd_pcm_mmap_offset(slave),
				       areas, offset, 
				       pcm->channels, frames, linear->conv_idx);
		err = snd_pcm_mmap_forward(slave, frames);
		if (err < 0)
			break;
		assert((snd_pcm_uframes_t)err == frames);
		offset += err;
		xfer += err;
		snd_pcm_mmap_hw_forward(pcm, err);
	}
	if (xfer > 0) {
		if (slave_sizep)
			*slave_sizep = xfer;
		return xfer;
	}
	return err;
}

static snd_pcm_sframes_t snd_pcm_linear_read_areas(snd_pcm_t *pcm,
					 const snd_pcm_channel_area_t *areas,
					 snd_pcm_uframes_t offset,
					 snd_pcm_uframes_t size,
					 snd_pcm_uframes_t *slave_sizep)
{
	snd_pcm_linear_t *linear = pcm->private_data;
	snd_pcm_t *slave = linear->plug.slave;
	snd_pcm_uframes_t xfer = 0;
	snd_pcm_sframes_t err = 0;
	if (slave_sizep && *slave_sizep < size)
		size = *slave_sizep;
	assert(size > 0);
	while (xfer < size) {
		snd_pcm_uframes_t frames = snd_pcm_mmap_capture_xfer(slave, size - xfer);
		snd_pcm_linear_convert(areas, offset, 
				       snd_pcm_mmap_areas(slave), snd_pcm_mmap_offset(slave),
				       pcm->channels, frames, linear->conv_idx);
		err = snd_pcm_mmap_forward(slave, frames);
		if (err < 0)
			break;
		assert((snd_pcm_uframes_t)err == frames);
		offset += err;
		xfer += err;
		snd_pcm_mmap_hw_forward(pcm, err);
	}
	if (xfer > 0) {
		if (slave_sizep)
			*slave_sizep = xfer;
		return xfer;
	}
	return err;
}

static void snd_pcm_linear_dump(snd_pcm_t *pcm, snd_output_t *out)
{
	snd_pcm_linear_t *linear = pcm->private_data;
	snd_output_printf(out, "Linear conversion PCM (%s)\n", 
		snd_pcm_format_name(linear->sformat));
	if (pcm->setup) {
		snd_output_printf(out, "Its setup is:\n");
		snd_pcm_dump_setup(pcm, out);
	}
	snd_output_printf(out, "Slave: ");
	snd_pcm_dump(linear->plug.slave, out);
}

snd_pcm_ops_t snd_pcm_linear_ops = {
	close: snd_pcm_plugin_close,
	info: snd_pcm_plugin_info,
	hw_refine: snd_pcm_linear_hw_refine,
	hw_params: snd_pcm_linear_hw_params,
	hw_free: snd_pcm_plugin_hw_free,
	sw_params: snd_pcm_plugin_sw_params,
	channel_info: snd_pcm_plugin_channel_info,
	dump: snd_pcm_linear_dump,
	nonblock: snd_pcm_plugin_nonblock,
	async: snd_pcm_plugin_async,
	mmap: snd_pcm_plugin_mmap,
	munmap: snd_pcm_plugin_munmap,
};

int snd_pcm_linear_open(snd_pcm_t **pcmp, const char *name, snd_pcm_format_t sformat, snd_pcm_t *slave, int close_slave)
{
	snd_pcm_t *pcm;
	snd_pcm_linear_t *linear;
	assert(pcmp && slave);
	if (snd_pcm_format_linear(sformat) != 1)
		return -EINVAL;
	linear = calloc(1, sizeof(snd_pcm_linear_t));
	if (!linear) {
		return -ENOMEM;
	}
	linear->sformat = sformat;
	linear->plug.read = snd_pcm_linear_read_areas;
	linear->plug.write = snd_pcm_linear_write_areas;
	linear->plug.slave = slave;
	linear->plug.close_slave = close_slave;

	pcm = calloc(1, sizeof(snd_pcm_t));
	if (!pcm) {
		free(linear);
		return -ENOMEM;
	}
	if (name)
		pcm->name = strdup(name);
	pcm->type = SND_PCM_TYPE_LINEAR;
	pcm->stream = slave->stream;
	pcm->mode = slave->mode;
	pcm->ops = &snd_pcm_linear_ops;
	pcm->op_arg = pcm;
	pcm->fast_ops = &snd_pcm_plugin_fast_ops;
	pcm->fast_op_arg = pcm;
	pcm->private_data = linear;
	pcm->poll_fd = slave->poll_fd;
	pcm->hw_ptr = &linear->plug.hw_ptr;
	pcm->appl_ptr = &linear->plug.appl_ptr;
	*pcmp = pcm;

	return 0;
}

int _snd_pcm_linear_open(snd_pcm_t **pcmp, const char *name,
			 snd_config_t *conf, 
			 snd_pcm_stream_t stream, int mode)
{
	snd_config_iterator_t i, next;
	const char *sname = NULL;
	int err;
	snd_pcm_t *spcm;
	snd_pcm_format_t sformat = SND_PCM_FORMAT_UNKNOWN;
	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id = snd_config_get_id(n);
		if (strcmp(id, "comment") == 0)
			continue;
		if (strcmp(id, "type") == 0)
			continue;
		if (strcmp(id, "sname") == 0) {
			err = snd_config_get_string(n, &sname);
			if (err < 0) {
				ERR("Invalid type for %s", id);
				return -EINVAL;
			}
			continue;
		}
		if (strcmp(id, "sformat") == 0) {
			const char *f;
			err = snd_config_get_string(n, &f);
			if (err < 0) {
				ERR("Invalid type for %s", id);
				return -EINVAL;
			}
			sformat = snd_pcm_format_value(f);
			if (sformat == SND_PCM_FORMAT_UNKNOWN) {
				ERR("Unknown sformat %s", f);
				return err;
			}
			if (snd_pcm_format_linear(sformat) != 1) {
				ERR("sformat is not linear");
				return -EINVAL;
			}
			continue;
		}
		ERR("Unknown field %s", id);
		return -EINVAL;
	}
	if (!sname) {
		ERR("sname is not defined");
		return -EINVAL;
	}
	if (sformat == SND_PCM_FORMAT_UNKNOWN) {
		ERR("sformat is not defined");
		return -EINVAL;
	}
	/* This is needed cause snd_config_update may destroy config */
	sname = strdup(sname);
	if (!sname)
		return  -ENOMEM;
	err = snd_pcm_open(&spcm, sname, stream, mode);
	free((void *) sname);
	if (err < 0)
		return err;
	err = snd_pcm_linear_open(pcmp, name, sformat, spcm, 1);
	if (err < 0)
		snd_pcm_close(spcm);
	return err;
}
				

