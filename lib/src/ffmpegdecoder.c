
#include <chiaki/ffmpegdecoder.h>

#include <libavutil/pixdesc.h>
#include <libavcodec/avcodec.h>
#include <assert.h>

static enum AVCodecID chiaki_codec_av_codec_id(ChiakiCodec codec)
{
	switch (codec)
	{
	case CHIAKI_CODEC_H265:
	case CHIAKI_CODEC_H265_HDR:
		return AV_CODEC_ID_H265;
	default:
		return AV_CODEC_ID_H264;
	}
}

CHIAKI_EXPORT enum AVPixelFormat chiaki_hw_get_format(AVCodecContext *av_codec_ctx, const enum AVPixelFormat *pix_fmts)
{
	ChiakiFfmpegDecoder *decoder = av_codec_ctx->opaque;
	for (const enum AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p)
	{
		if (*p == decoder->hw_pix_fmt)
		{
			CHIAKI_LOGV(decoder->log, "AVPixelFormat %s selected", av_get_pix_fmt_name(*p));
			return *p;
		}
	}
	CHIAKI_LOGW(decoder->log, "Failed to find compatible GPU AV formt, falling back to CPU");
	av_buffer_unref(&av_codec_ctx->hw_device_ctx);
	return chiaki_ffmpeg_decoder_get_pixel_format(decoder);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_ffmpeg_decoder_init(ChiakiFfmpegDecoder *decoder, ChiakiLog *log,
														 ChiakiCodec codec, const char *hw_decoder_name,
														 ChiakiFfmpegFrameAvailable frame_available_cb, void *frame_available_cb_user)
{
	decoder->log = log;
	decoder->frame_available_cb = frame_available_cb;
	decoder->frame_available_cb_user = frame_available_cb_user;
	ChiakiErrorCode err = chiaki_mutex_init(&decoder->mutex, false);
	if (err != CHIAKI_ERR_SUCCESS)
		return err;

	decoder->hw_device_ctx = NULL;
	decoder->hw_pix_fmt = AV_PIX_FMT_NONE;

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 10, 100)
	CHIAKI_LOGI(log, "LIBAVCODEC version < 58,10,100");
	avcodec_register_all();
#endif
	// First we find the requested codec
	enum AVCodecID av_codec = chiaki_codec_av_codec_id(codec);
	decoder->av_codec = avcodec_find_decoder(av_codec);
	if (!decoder->av_codec)
	{
		CHIAKI_LOGE(log, "%s Codec not available", chiaki_codec_name(codec));
		goto error_mutex;
	}
	// Then we allocate a AVCodecContext structure for it
	decoder->codec_context = avcodec_alloc_context3(decoder->av_codec);
	if (!decoder->codec_context)
	{
		CHIAKI_LOGE(log, "Failed to alloc codec context");
		goto error_mutex;
	}

	// If we were passed the name of a hardware accelerator attempt to configure it
	if (hw_decoder_name)
	{
		// Grab the AVHWDeviceType for the name we were given
		CHIAKI_LOGI(log, "Using hardware decoder \"%s\"", hw_decoder_name);
		enum AVHWDeviceType type = av_hwdevice_find_type_by_name(hw_decoder_name);
		if (type == AV_HWDEVICE_TYPE_NONE)
		{
			CHIAKI_LOGE(log, "Hardware decoder \"%s\" not found", hw_decoder_name);
			goto error_codec_context;
		}

		// Allocate the hwdevice context
		AVBufferRef *av_gpu_decoder = NULL;
		const int hwdevice_res = av_hwdevice_ctx_create(&av_gpu_decoder, type, NULL, NULL, 0);
		if (hwdevice_res < 0)
		{
			CHIAKI_LOGE(log, "%s av_hwdevice_ctx_create failed: %s", hw_decoder_name, av_err2str(hwdevice_res));
			goto error_codec_context;
		}

		// iterate over this codec's supported hardware configs.
		// NOTE(rossdylan): D3D11va has two different hwaccel modules in ffmpeg.
		// 1. is d3d11va which is actually some legacy crap that only exposes an adhoc hw config
		// 2. is d3d11va2 which is the one we actaully want.
		// The worst part is that d3d11va /kinda/ looks right, however I have no idea how
		// to do the adhoc configuration it requires.
		for (int i = 0;; i++)
		{
			const AVCodecHWConfig *config = avcodec_get_hw_config(decoder->av_codec, i);
			if (!config)
			{
				// Dump out some interesting information if we've failed to find any supported
				// hardware configs.
				CHIAKI_LOGE(log, "avcodec_get_hw_config failed: Decoder %s does not support device type %s", decoder->av_codec->name, av_hwdevice_get_type_name(type));
				CHIAKI_LOGE(log, "avcodec_config: %s", avcodec_configuration());
				goto error_codec_context;
			}
			CHIAKI_LOGV(log, "found hw_config type: '%s', methods: %d, pix_fmt: '%s'", av_hwdevice_get_type_name(config->device_type), config->methods, av_get_pix_fmt_name(config->pix_fmt));
			if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == type)
			{
				decoder->hw_pix_fmt = config->pix_fmt;
				break;
			}
		}
		decoder->codec_context->opaque = decoder;
		decoder->codec_context->hw_device_ctx = av_buffer_ref(av_gpu_decoder);
		decoder->hw_device_ctx = av_buffer_ref(av_gpu_decoder);
		decoder->codec_context->get_format = chiaki_hw_get_format;
	}

	if (avcodec_open2(decoder->codec_context, decoder->av_codec, NULL) < 0)
	{
		CHIAKI_LOGE(log, "Failed to open codec context");
		goto error_codec_context;
	}

	return CHIAKI_ERR_SUCCESS;
error_codec_context:
	if (decoder->hw_device_ctx)
		av_buffer_unref(&decoder->hw_device_ctx);
	avcodec_free_context(&decoder->codec_context);
error_mutex:
	chiaki_mutex_fini(&decoder->mutex);
	return CHIAKI_ERR_UNKNOWN;
}

CHIAKI_EXPORT void chiaki_ffmpeg_decoder_fini(ChiakiFfmpegDecoder *decoder)
{
	avcodec_close(decoder->codec_context);
	avcodec_free_context(&decoder->codec_context);
	if (decoder->hw_device_ctx)
		av_buffer_unref(&decoder->hw_device_ctx);
}

CHIAKI_EXPORT bool chiaki_ffmpeg_decoder_video_sample_cb(uint8_t *buf, size_t buf_size, void *user)
{
	ChiakiFfmpegDecoder *decoder = user;

	chiaki_mutex_lock(&decoder->mutex);
	AVPacket packet;
	av_init_packet(&packet);
	packet.data = buf;
	packet.size = buf_size;
	int r;
send_packet:
	r = avcodec_send_packet(decoder->codec_context, &packet);
	if (r != 0)
	{
		if (r == AVERROR(EAGAIN))
		{
			CHIAKI_LOGE(decoder->log, "AVCodec internal buffer is full removing frames before pushing");
			AVFrame *frame = av_frame_alloc();
			if (!frame)
			{
				CHIAKI_LOGE(decoder->log, "Failed to alloc AVFrame");
				goto hell;
			}
			r = avcodec_receive_frame(decoder->codec_context, frame);
			av_frame_free(&frame);
			if (r != 0)
			{
				CHIAKI_LOGE(decoder->log, "Failed to pull frame");
				goto hell;
			}
			goto send_packet;
		}
		else
		{
			char errbuf[128];
			av_make_error_string(errbuf, sizeof(errbuf), r);
			CHIAKI_LOGE(decoder->log, "Failed to push frame: %s", errbuf);
			goto hell;
		}
	}
	chiaki_mutex_unlock(&decoder->mutex);

	decoder->frame_available_cb(decoder, decoder->frame_available_cb_user);
	return true;
hell:
	chiaki_mutex_unlock(&decoder->mutex);
	return false;
}

static AVFrame *pull_from_hw(ChiakiFfmpegDecoder *decoder, AVFrame *hw_frame)
{
	AVFrame *sw_frame = av_frame_alloc();
	if (av_hwframe_transfer_data(sw_frame, hw_frame, 0) < 0)
	{
		CHIAKI_LOGE(decoder->log, "Failed to transfer frame from hardware");
		av_frame_unref(sw_frame);
		sw_frame = NULL;
	}
	av_frame_unref(hw_frame);
	return sw_frame;
}

CHIAKI_EXPORT AVFrame *chiaki_ffmpeg_decoder_pull_frame(ChiakiFfmpegDecoder *decoder)
{
	chiaki_mutex_lock(&decoder->mutex);
	// always try to pull as much as possible and return only the very last frame
	AVFrame *frame_last = NULL;
	AVFrame *frame = NULL;
	while (true)
	{
		AVFrame *next_frame;
		if (frame_last)
		{
			av_frame_unref(frame_last);
			next_frame = frame_last;
		}
		else
		{
			next_frame = av_frame_alloc();
			if (!next_frame)
				break;
		}
		frame_last = frame;
		frame = next_frame;
		int r = avcodec_receive_frame(decoder->codec_context, frame);
		if (!r)
			frame = decoder->hw_device_ctx ? pull_from_hw(decoder, frame) : frame;
		else
		{
			if (r != AVERROR(EAGAIN))
				CHIAKI_LOGE(decoder->log, "Decoding with FFMPEG failed");
			av_frame_free(&frame);
			frame = frame_last;
			break;
		}
	}
	chiaki_mutex_unlock(&decoder->mutex);

	return frame;
}

CHIAKI_EXPORT enum AVPixelFormat chiaki_ffmpeg_decoder_get_pixel_format(ChiakiFfmpegDecoder *decoder)
{
	// TODO: this is probably very wrong, especially for hdr
	return decoder->hw_device_ctx
			   ? AV_PIX_FMT_NV12
			   : AV_PIX_FMT_YUV420P;
}
