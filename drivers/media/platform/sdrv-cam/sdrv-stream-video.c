/*
 * sdrv-stream-video.c
 *
 * Semidrive platform v4l2 video operation
 *
 * Copyright (C) 2019, Semidrive  Communications Inc.
 *
 * This file is licensed under a dual GPLv2 or X11 license.
 */
#include <linux/clk.h>
#include <linux/media-bus-format.h>
#include <linux/media.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-rect.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-sg.h>
#include <media/videobuf2-dma-contig.h>

#include "sdrv-csi.h"

static inline struct v4l2_subdev *video_remote_subdev(
		struct kstream_video *video, u32 *pad)
{
	struct media_pad *remote;

	remote = media_entity_remote_pad(&video->pad);

	if(!remote || !is_media_entity_v4l2_subdev(remote->entity))
		return NULL;

	if(pad)
		*pad = remote->index;

	return media_entity_to_v4l2_subdev(remote->entity);
}

static int kstream_video_querycap(struct file *file, void *fh,
		struct v4l2_capability *cap)
{
	struct kstream_video *video = video_drvdata(file);

	strlcpy(cap->driver, SDRV_IMG_NAME, sizeof(cap->driver));
	strlcpy(cap->card, SDRV_IMG_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s-%d",
			dev_name(video->dev), video->id);

	return 0;
}

static int kstream_video_init_formats(struct kstream_video *video)
{
	struct v4l2_subdev *sd;
	u32 pad;
	struct kstream_device *kstream = container_of(video,
			struct kstream_device, video);
	struct v4l2_subdev_mbus_code_enum mbus_code = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	sd = video_remote_subdev(video, &pad);
	if(sd == NULL)
		return -EINVAL;

	while (!v4l2_subdev_call(sd, pad, enum_mbus_code,
	       NULL, &mbus_code)) {
		mbus_code.index++;
		kstream_init_format_by_mbus_code(kstream, mbus_code.code);
		dev_info(video->dev, "%s: mbus_code.code=0x%x, mbus_code.index=%d\n", __func__, mbus_code.code, mbus_code.index);
	}
	return 0;
}

static int kstream_video_free_formats(struct kstream_video *video)
{
	struct kstream_device *kstream = container_of(video,
			struct kstream_device, video);

	kzfree(kstream->support_formats);
	kstream->support_formats = NULL;
	kstream->support_formats_num = 0;

	return 0;
}

static int kstream_video_g_fmt(struct file *file, void *fh,
		struct v4l2_format *f)
{
	struct kstream_video *video = video_drvdata(file);
	const struct kstream_pix_format *kpf;
	struct v4l2_subdev_pad_config *pad_cfg;
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct v4l2_subdev *sd;
	struct v4l2_format *af = &video->active_fmt;
	u32 pad;
	int ret = 0;

	if(f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	if(af->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
			af->fmt.pix_mp.width &&
			af->fmt.pix_mp.height &&
			af->fmt.pix_mp.num_planes) {
		*f = *af;
		return 0;
	}

	sd = video_remote_subdev(video, &pad);
	if(sd == NULL)
		return -EINVAL;

	pad_cfg = v4l2_subdev_alloc_pad_config(sd);
	if(pad_cfg == NULL)
		return -ENOMEM;

	ret = v4l2_subdev_call(sd, pad, get_fmt, pad_cfg, &format);
	if(ret)
		goto done;

	v4l2_fill_pix_format_mplane(&f->fmt.pix_mp, &format.format);

	kpf = kstream_get_kpfmt_by_mbus_code(format.format.code);
	if(!kpf) {
		ret = -EINVAL;
		goto done;
	}

	f->fmt.pix_mp.pixelformat = kpf->pixfmt;

done:
	v4l2_subdev_free_pad_config(pad_cfg);
	return ret;
}

static int kstream_video_enum_fmt(struct file *file, void *priv,
		struct v4l2_fmtdesc *f)
{
#if 0
	const struct kstream_pix_format *kpf;

	kpf = kstream_get_kpfmt_by_index(f->index);

	if(!kpf)
		return -EINVAL;

	f->pixelformat = kpf->pixfmt;
#else
	u32 *kpf;
	struct kstream_video *video = video_drvdata(file);
	struct kstream_device *kstream = container_of(video,
				struct kstream_device, video);

	kpf = kstream_get_support_formats_by_index(kstream, f->index);

	if(!kpf)
		return -EINVAL;

	f->pixelformat = *kpf;
#endif

	return 0;
}

static int __kstream_video_try_format_source(struct kstream_video *video,
		u32 which, struct v4l2_pix_format_mplane *pix_mp,
		const struct kstream_pix_format *kstream_fmt)
{
	struct v4l2_subdev *sd;
	struct v4l2_subdev_pad_config *pad_cfg;
	struct v4l2_subdev_format format = {
		.which = which,
	};
	enum v4l2_field field;
	int ret;

	sd = video_remote_subdev(video, NULL);
	if(sd == NULL)
		return -EINVAL;

	ret = sdrv_find_pad(sd, MEDIA_PAD_FL_SINK);
	if(ret < 0)
		return ret;

	format.pad = ret;

	v4l2_fill_mbus_format_mplane(&format.format, pix_mp);
	format.format.code = kstream_fmt->mbus_code;

	pad_cfg = v4l2_subdev_alloc_pad_config(sd);
	if(pad_cfg == NULL)
		return -ENOMEM;

	field = pix_mp->field;

	ret = v4l2_subdev_call(sd, pad, set_fmt, pad_cfg, &format);
	if(ret < 0 && ret != -ENOIOCTLCMD)
		goto done;

	if(format.format.code != kstream_fmt->mbus_code) {
		ret = -EINVAL;
		goto done;
	}

	v4l2_fill_pix_format_mplane(pix_mp, &format.format);
	pix_mp->field = field;

done:
	v4l2_subdev_free_pad_config(pad_cfg);
	return ret;

}

static int __kstream_video_try_format(struct kstream_video *video,
		u32 which, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp;
	const struct kstream_pix_format *kpf;
	u32 width, height, field;
	int i, ret;
	const u8 *bpp;
	//#ifdef CONFIG_ARCH_SEMIDRIVE_PROCESSOR9
	struct kstream_device *kstream = container_of(video,
			struct kstream_device, video);
	//#endif

	pix_mp = &f->fmt.pix_mp;

	#if 0
	kpf = kstream_get_kpfmt_by_fmt(pix_mp->pixelformat);
	#else
	int cnt = kstream_get_kpfmt_count();
	for (i = 0; i < cnt; i++) {
		kpf = kstream_get_kpfmt_by_index(i);
		if ((pix_mp->pixelformat != kpf->pixfmt) || (kpf->bt != kstream->core->bt))
			continue;
	#endif
	if(!kpf)
		return -EINVAL;

	width = pix_mp->width;
	height = pix_mp->height;
	field = pix_mp->field;
	bpp = kpf->bpp;
	memset(pix_mp, 0, sizeof(*pix_mp));

	pix_mp->pixelformat = kpf->pixfmt;
	pix_mp->width = clamp_t(u32, width, 1, SDRV_IMG_X_MAX);
	pix_mp->height = clamp_t(u32, height, 1, SDRV_IMG_Y_MAX);
	pix_mp->num_planes = strlen(kpf->bpp) / sizeof(kpf->bpp[0]);
	pix_mp->field = field;
	if(pix_mp->field == V4L2_FIELD_ANY)
		pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->colorspace = V4L2_COLORSPACE_SRGB;
	pix_mp->flags = 0;
	pix_mp->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(pix_mp->colorspace);
	pix_mp->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
			pix_mp->colorspace, pix_mp->ycbcr_enc);
	pix_mp->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(pix_mp->colorspace);

	ret = __kstream_video_try_format_source(video, which, pix_mp, kpf);
	#if 0
	if(ret)
		return ret;
	#else
	if (!ret)
		break;
	}
	if (i==cnt)
		return -EINVAL;
	#endif

	switch(pix_mp->field) {
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
	case V4L2_FIELD_ALTERNATE:
		pix_mp->height /= 2;
		break;
	case V4L2_FIELD_NONE:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
	case V4L2_FIELD_INTERLACED:
		break;
	default:
		pix_mp->field = V4L2_FIELD_NONE;
		break;
	}

	for(i = 0; i < pix_mp->num_planes; i++) {
		pix_mp->plane_fmt[i].bytesperline = pix_mp->width * bpp[i] / 8;
		pix_mp->plane_fmt[i].sizeimage =
			pix_mp->plane_fmt[i].bytesperline * pix_mp->height;
		#ifdef CONFIG_ARCH_SEMIDRIVE_PROCESSOR9
			if (kstream->hcrop_back || kstream->hcrop_front) {
				video->queue.sd_hcrop_b = pix_mp->plane_fmt[i].bytesperline * kstream->hcrop_back;
				video->queue.sd_hcrop_f = pix_mp->plane_fmt[i].bytesperline * kstream->hcrop_front;
			}
		#endif
	}

	return 0;
}

static int kstream_video_try_fmt(struct file *file,
		void *fh, struct v4l2_format *f)
{
	struct kstream_video *video = video_drvdata(file);

	return __kstream_video_try_format(video, V4L2_SUBDEV_FORMAT_TRY, f);
}

static int kstream_video_s_fmt(struct file *file, void *fh,
		struct v4l2_format *f)
{
	struct kstream_video *video = video_drvdata(file);
	int ret;

	if(vb2_is_busy(&video->queue)){
		dev_err(video->dev, "%s busy return.\n", __func__);
		return -EBUSY;
	}
	ret = __kstream_video_try_format(video, V4L2_SUBDEV_FORMAT_ACTIVE, f);
	if(ret < 0)
		return ret;

	video->active_fmt = *f;

	return 0;
}

static int kstream_g_selection(struct file *file, void *fh,
		struct v4l2_selection *s)
{
	struct kstream_video *video = video_drvdata(file);
	struct v4l2_subdev_pad_config *pad_cfg;
	struct v4l2_subdev *sd;
	u32 pad;
	int ret = 0;
	struct v4l2_subdev_selection sd_sel = {
		.target = s->target,
		.r = s->r,
	};

	sd = video_remote_subdev(video, &pad);
	if(sd == NULL)
		return -EINVAL;

	pad_cfg = v4l2_subdev_alloc_pad_config(sd);
	if(pad_cfg == NULL)
		return -ENOMEM;

	sd_sel.pad = pad;
	sd_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;

	ret = v4l2_subdev_call(sd, pad, get_selection, pad_cfg, &sd_sel);
	if(ret)
		goto done;

	s->r = sd_sel.r;
done:
	v4l2_subdev_free_pad_config(pad_cfg);
	return ret;
}

static int kstream_s_selection(struct file *file, void *fh,
		struct v4l2_selection *s)
{
	struct kstream_video *video = video_drvdata(file);
	struct v4l2_subdev_pad_config *pad_cfg;
	struct v4l2_subdev *sd;
	u32 pad;
	int ret = 0;
	struct v4l2_subdev_selection sd_sel = {
		.target = V4L2_SEL_TGT_CROP_BOUNDS,
	};

	if(s->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	sd = video_remote_subdev(video, &pad);
	if(sd == NULL)
		return -EINVAL;

	pad_cfg = v4l2_subdev_alloc_pad_config(sd);
	if(pad_cfg == NULL)
		return -ENOMEM;

	sd_sel.pad = pad;
	sd_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(sd, pad, get_selection, pad_cfg, &sd_sel);
	if(ret)
		goto done;

	v4l2_rect_map_inside(&s->r, &sd_sel.r);

	sd_sel.target = s->target;
	sd_sel.pad = pad;
	sd_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sd_sel.r = s->r;

	ret = v4l2_subdev_call(sd, pad, set_selection, pad_cfg, &sd_sel);
	if(ret)
		goto done;

	s->r = sd_sel.r;
done:
	v4l2_subdev_free_pad_config(pad_cfg);
	return ret;
}

static int kstream_cropcap(struct file *file, void *fh,
		struct v4l2_cropcap *crop)
{
	struct kstream_video *video = video_drvdata(file);
	struct v4l2_subdev *sd;
	u32 pad;

	if(crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	sd = video_remote_subdev(video, &pad);
	if(sd == NULL)
		return -EINVAL;

	return v4l2_subdev_call(sd, video, g_pixelaspect, &crop->pixelaspect);
}

static int kstream_enum_input(struct file *file, void *fh,
		struct v4l2_input *i)
{
	struct kstream_video *video = video_drvdata(file);
	struct v4l2_subdev *sd;
	u32 pad;

	if(i->index != 0)
		return -EINVAL;

	sd = video_remote_subdev(video, &pad);
	if(sd == NULL)
		return -EINVAL;

	return v4l2_subdev_call(sd, video, g_input_status, &i->status);
}

static int kstream_g_input(struct file *file, void *fh, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int kstream_s_input(struct file *file, void *fh, unsigned int i)
{
	if(i > 0)
		return -EINVAL;
	return 0;
}

static int kstream_g_parm(struct file *file, void *fh,
		struct v4l2_streamparm *a)
{
	struct kstream_video *video = video_drvdata(file);
	struct v4l2_subdev *sd;
	u32 pad;

	sd = video_remote_subdev(video, &pad);
	if(sd == NULL)
		return -EINVAL;

	return v4l2_subdev_call(sd, video, g_parm, a);
}

static int kstream_s_parm(struct file *file, void *fh,
		struct v4l2_streamparm *a)
{
	struct kstream_video *video = video_drvdata(file);
	struct v4l2_subdev *sd;
	u32 pad;

	sd = video_remote_subdev(video, &pad);
	if(sd == NULL)
		return -EINVAL;

	return v4l2_subdev_call(sd, video, s_parm, a);
}

static int kstream_querystd(struct file *file, void *fh, v4l2_std_id *a)
{
	struct kstream_video *video = video_drvdata(file);
	struct v4l2_subdev *sd;
	u32 pad;

	sd = video_remote_subdev(video, &pad);
	if(sd == NULL)
		return -EINVAL;

	return v4l2_subdev_call(sd, video, querystd, a);
}

static int kstream_s_std(struct file *file, void *fh, v4l2_std_id a)
{
	struct kstream_video *video = video_drvdata(file);
	struct v4l2_subdev *sd;
	u32 pad;

	sd = video_remote_subdev(video, &pad);
	if(sd == NULL)
		return -EINVAL;

	return v4l2_subdev_call(sd, video, s_std, a);
}

static int kstream_g_std(struct file *file, void *fh, v4l2_std_id *a)
{
	struct kstream_video *video = video_drvdata(file);
	struct v4l2_subdev *sd;
	u32 pad;

	sd = video_remote_subdev(video, &pad);
	if(sd == NULL)
		return -EINVAL;

	return v4l2_subdev_call(sd, video, g_std, a);
}

static int kstream_enum_framesizes(struct file *file, void *fh,
		struct v4l2_frmsizeenum *fsize)
{
	const struct kstream_pix_format *kpf;
	struct kstream_video *video = video_drvdata(file);
	struct v4l2_subdev_pad_config *pad_cfg;
	struct v4l2_subdev_frame_size_enum fse = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.index = fsize->index,
		.pad = 0,
	};
	struct v4l2_subdev *sd;
	u32 pad;
	int ret;

	sd = video_remote_subdev(video, &pad);
	if(sd == NULL)
		return -EINVAL;

	kpf = kstream_get_kpfmt_by_fmt(fsize->pixel_format);
	if(!kpf)
		return -EINVAL;

	fse.code = kpf->mbus_code;

	pad_cfg = v4l2_subdev_alloc_pad_config(sd);
	if(pad_cfg == NULL)
		return -ENOMEM;

	 ret = v4l2_subdev_call(sd, pad, enum_frame_size, pad_cfg, &fse);
	 if(ret)
		 goto done;

#if 0
	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = fse.min_width;
	fsize->stepwise.max_width = fse.max_width;
	fsize->stepwise.min_height = fse.min_height;
	fsize->stepwise.max_height = fse.max_height;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.step_height = 1;
#else
	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = fse.min_width;
	fsize->discrete.height = fse.min_height;
#endif

done:
	v4l2_subdev_free_pad_config(pad_cfg);
	return ret;
}

static int kstream_enum_frameintervals(struct file *file, void *fh,
		struct v4l2_frmivalenum *fival)
{
	const struct kstream_pix_format *kpf;
	struct kstream_video *video = video_drvdata(file);
	struct v4l2_subdev_pad_config *pad_cfg;
	struct v4l2_subdev_frame_interval_enum fie = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.index = fival->index,
		.pad = 0,
		.width = fival->width,
		.height = fival->height,
	};
	struct v4l2_subdev *sd;
	u32 pad;
	int ret;

	sd = video_remote_subdev(video, &pad);
	if(sd == NULL)
		return -EINVAL;

	kpf = kstream_get_kpfmt_by_fmt(fival->pixel_format);
	if(!kpf)
		return -EINVAL;

	fie.code = kpf->mbus_code;

	pad_cfg = v4l2_subdev_alloc_pad_config(sd);
	if(pad_cfg == NULL)
		return -ENOMEM;

	 ret = v4l2_subdev_call(sd, pad, enum_frame_interval, pad_cfg, &fie);
	 if(ret)
		 goto done;

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete = fie.interval;

done:
	v4l2_subdev_free_pad_config(pad_cfg);
	return ret;
}

static long kstream_vidioc_default(struct file *file, void *fh,
		bool valid_prio, unsigned int cmd, void *arg)
{
	struct kstream_video *video = video_drvdata(file);
	struct v4l2_subdev *sd;
	u32 pad;

	sd = video_remote_subdev(video, &pad);
	if(sd == NULL)
		return -EINVAL;

	switch(cmd) {
		case VIDIOC_SUBDEV_G_FRAME_INTERVAL:
		{
			struct v4l2_subdev_frame_interval *fi = arg;
			return v4l2_subdev_call(sd, video, g_frame_interval, fi);
			break;
		}
		case VIDIOC_SUBDEV_S_FRAME_INTERVAL:
		{
			struct v4l2_subdev_frame_interval *fi = arg;
			return v4l2_subdev_call(sd, video, s_frame_interval, fi);
			break;
		}
		default:
		return 0;
	}
	return 0;
}

static const struct v4l2_ioctl_ops sdrv_stream_vid_ioctl_ops = {
	.vidioc_querycap = kstream_video_querycap,
	.vidioc_enum_fmt_vid_cap_mplane = kstream_video_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane = kstream_video_g_fmt,
	.vidioc_s_fmt_vid_cap_mplane = kstream_video_s_fmt,
	.vidioc_try_fmt_vid_cap_mplane = kstream_video_try_fmt,

	.vidioc_g_selection = kstream_g_selection,
	.vidioc_s_selection = kstream_s_selection,

	.vidioc_cropcap = kstream_cropcap,

	.vidioc_enum_input = kstream_enum_input,
	.vidioc_g_input = kstream_g_input,
	.vidioc_s_input = kstream_s_input,

	.vidioc_querystd = kstream_querystd,
	.vidioc_g_std = kstream_g_std,
	.vidioc_s_std = kstream_s_std,

	.vidioc_g_parm = kstream_g_parm,
	.vidioc_s_parm = kstream_s_parm,

	.vidioc_enum_framesizes = kstream_enum_framesizes,
	.vidioc_enum_frameintervals = kstream_enum_frameintervals,

	.vidioc_default = kstream_vidioc_default,

	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};

static int kstream_video_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct kstream_video *video = video_drvdata(file);
	struct v4l2_fh *vfh;
	int ret;

	mutex_lock(&video->lock);

	vfh = kzalloc(sizeof(*vfh), GFP_KERNEL);
	if(!vfh) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	v4l2_fh_init(vfh, vdev);
	v4l2_fh_add(vfh);

	file->private_data = vfh;

	ret = v4l2_pipeline_pm_use(&vdev->entity, 1);
	if(ret < 0) {
		dev_err(video->dev, "%s: Failed to power up pipeline: %d\n", __func__, ret);
		goto err_pm_use;
	}

	kstream_video_init_formats(video);

	mutex_unlock(&video->lock);

	return ret;

err_pm_use:
	v4l2_fh_release(file);
err_alloc:
	mutex_unlock(&video->lock);

	return ret;
}

static int kstream_video_release(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct kstream_video *video = video_drvdata(file);

	vb2_fop_release(file);

	v4l2_pipeline_pm_use(&vdev->entity, 0);

	kstream_video_free_formats(video);

	file->private_data = NULL;
	return 0;
}

static const struct v4l2_file_operations sdrv_stream_vid_ops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open = kstream_video_open,
	.release = kstream_video_release,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
	.read = vb2_fop_read,
};

static int kstream_queue_setup(struct vb2_queue *q,
		unsigned int *num_buffers, unsigned int *num_planes,
		unsigned int sizes[], struct device *alloc_devs[])
{
	struct kstream_video *video = vb2_get_drv_priv(q);
	const struct v4l2_pix_format_mplane *format =
		&video->active_fmt.fmt.pix_mp;
	unsigned int i;

	if(*num_planes) {
		if(*num_planes != format->num_planes)
			return -EINVAL;

		for(i = 0; i < *num_planes; i++)
			if(sizes[i] < format->plane_fmt[i].sizeimage)
				return -EINVAL;

		return 0;
	}

	*num_planes = format->num_planes;
	for(i = 0; i < *num_planes; i++)
		sizes[i] = format->plane_fmt[i].sizeimage;

	return 0;
}

static int kstream_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct kstream_buffer *kbuffer = container_of(vbuf,
			struct kstream_buffer, vbuf);
	struct kstream_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct kstream_device *kstream = container_of(video,
			struct kstream_device, video);
	const struct v4l2_pix_format_mplane *format =
		&video->active_fmt.fmt.pix_mp;
	struct sg_table *sgt;
	unsigned int i;

	if(!kstream->iommu_enable) {
		for(i = 0; i < format->num_planes; i++) {
			kbuffer->paddr[i] = vb2_dma_contig_plane_dma_addr(vb, i);
			if(!kbuffer->paddr[i])
				return -EFAULT;
			#ifdef CONFIG_ARCH_SEMIDRIVE_PROCESSOR9
			if (video->queue.sd_hcrop_b)
				kbuffer->paddr[i] -= video->queue.sd_hcrop_b;
			#endif
		}

		return 0;
	}

	for(i = 0; i < format->num_planes; i++) {
		sgt = vb2_dma_sg_plane_desc(vb, i);
		if(!sgt)
			return -EFAULT;

		kbuffer->paddr[i] = sg_dma_address(sgt->sgl);
	}

	return 0;
}

static int kstream_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct kstream_video *video = vb2_get_drv_priv(vb->vb2_queue);
	const struct v4l2_pix_format_mplane *format =
		&video->active_fmt.fmt.pix_mp;
	unsigned int i;

	for(i = 0; i < format->num_planes; i++) {
		if(format->plane_fmt[i].sizeimage > vb2_plane_size(vb, i))
			return -EINVAL;

		vb2_set_plane_payload(vb, i, format->plane_fmt[i].sizeimage);
	}

	vbuf->field = format->field;

	return 0;
}

static void kstream_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct kstream_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct kstream_buffer *kbuffer = container_of(vbuf,
			struct kstream_buffer, vbuf);
	unsigned long flags;

	spin_lock_irqsave(&video->buf_lock, flags);

	list_add_tail(&kbuffer->queue, &video->buf_list);

	spin_unlock_irqrestore(&video->buf_lock, flags);
}

static int kstream_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct kstream_video *video = vb2_get_drv_priv(vq);
	struct v4l2_subdev *sd;

	sd = video_remote_subdev(video, NULL);
	if(sd == NULL)
		return -EINVAL;

	return v4l2_subdev_call(sd, video, s_stream, 1);
}

static void kstream_stop_streaming(struct vb2_queue *vq)
{
	struct kstream_video *video = vb2_get_drv_priv(vq);
	struct v4l2_subdev *sd;

	sd = video_remote_subdev(video, NULL);
	if(sd == NULL)
		return;

	v4l2_subdev_call(sd, video, s_stream, 0);
}

static const struct vb2_ops sdrv_video_vb2_q_ops = {
	.queue_setup = kstream_queue_setup,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.buf_init = kstream_buf_init,
	.buf_prepare = kstream_buf_prepare,
	.buf_queue = kstream_buf_queue,
	.start_streaming = kstream_start_streaming,
	.stop_streaming = kstream_stop_streaming,
};

static void sdrv_stream_video_release(struct video_device *vdev);
int sdrv_stream_video_register(struct kstream_video *video,
		struct v4l2_device *v4l2_dev)
{
	struct kstream_device *kstream = container_of(video,
			struct kstream_device, video);
	struct media_pad *pad = &video->pad;
	struct video_device *vdev;
	struct vb2_queue *q;
	int ret;

	vdev = &video->vdev;
	q = &video->queue;

	mutex_init(&video->lock);
	mutex_init(&video->q_lock);

	INIT_LIST_HEAD(&video->buf_list);
	spin_lock_init(&video->buf_lock);

	q->drv_priv = video;
	if(kstream->iommu_enable)
		q->mem_ops = &vb2_dma_sg_memops;
	else
		q->mem_ops = &vb2_dma_contig_memops;
	q->ops = &sdrv_video_vb2_q_ops;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_DMABUF | VB2_MMAP | VB2_READ | VB2_USERPTR;
	#ifdef CONFIG_ARCH_SEMIDRIVE_PROCESSOR9
		if (kstream->hcrop_back || kstream->hcrop_front)
			q->io_modes = VB2_MMAP | VB2_READ;
	#endif
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->buf_struct_size = sizeof(struct kstream_buffer);
	q->dev = video->dev;
	q->lock = &video->q_lock;
	ret = vb2_queue_init(q);
	if(ret < 0) {
		dev_err(video->dev, "Failed to init vb2 queue: %d\n", ret);
		goto error_vb2_init;
	}

	pad->flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vdev->entity, 1, pad);
	if(ret < 0) {
		dev_err(video->dev, "Failed to init video entity: %d\n", ret);
		goto error_media_init;
	}

	vdev->fops = &sdrv_stream_vid_ops;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_STREAMING |
		V4L2_CAP_READWRITE;
	vdev->ioctl_ops = &sdrv_stream_vid_ioctl_ops;
	vdev->release = sdrv_stream_video_release;
	vdev->v4l2_dev = v4l2_dev;
	vdev->vfl_dir = VFL_DIR_RX;
	vdev->queue = q;
	vdev->lock = &video->lock;
	snprintf(vdev->name, ARRAY_SIZE(vdev->name), "%s-%d-%d",
			SDRV_IMG_NAME, kstream->core->host_id, kstream->id);

	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if(ret < 0) {
		dev_err(kstream->dev, "Failed to register video device: %d\n", ret);
		goto error_video_register;
	}

	video_set_drvdata(vdev, video);
	atomic_inc(&video->core->ref_count);

	return 0;

error_video_register:
	mutex_destroy(&video->lock);
	mutex_destroy(&video->q_lock);
error_media_init:
	vb2_queue_release(vdev->queue);
error_vb2_init:

	return ret;
}

static void sdrv_stream_video_release(struct video_device *vdev)
{
	struct kstream_video *video = video_get_drvdata(vdev);

	mutex_destroy(&video->q_lock);
	mutex_destroy(&video->lock);

	atomic_dec(&video->core->ref_count);
}

void sdrv_stream_video_unregister(struct kstream_video *video)
{
	atomic_inc(&video->core->ref_count);
	video_unregister_device(&video->vdev);
	atomic_dec(&video->core->ref_count);

}
