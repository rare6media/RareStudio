#include "ClipMedia.h"

#include <QFile>
#include <QScopeGuard>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <deque>

namespace ClipMedia {

namespace {

QString errText(int code)
{
	char buf[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(code, buf, sizeof(buf));
	return QString::fromUtf8(buf);
}

struct Input {
	AVFormatContext *fmt = nullptr;
	int video = -1;
	std::vector<int> audio; // stream indexes, in file order

	~Input() { avformat_close_input(&fmt); }

	bool open(const QString &path, QString *error)
	{
		int rc = avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr);
		if (rc < 0) {
			*error = "Could not open the clip: " + errText(rc);
			return false;
		}
		if ((rc = avformat_find_stream_info(fmt, nullptr)) < 0) {
			*error = "Could not read the clip: " + errText(rc);
			return false;
		}
		for (unsigned i = 0; i < fmt->nb_streams; ++i) {
			const AVMediaType t = fmt->streams[i]->codecpar->codec_type;
			if (t == AVMEDIA_TYPE_VIDEO && video < 0 && !(fmt->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC))
				video = int(i);
			else if (t == AVMEDIA_TYPE_AUDIO)
				audio.push_back(int(i));
		}
		return true;
	}
	double startTime() const { return fmt->start_time == AV_NOPTS_VALUE ? 0.0 : fmt->start_time / double(AV_TIME_BASE); }
	double duration() const { return fmt->duration > 0 ? fmt->duration / double(AV_TIME_BASE) : 0.0; }
};

AVCodecContext *openDecoder(AVStream *st, QString *error)
{
	const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
	AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
	if (!ctx) {
		*error = "No decoder for this clip.";
		return nullptr;
	}
	avcodec_parameters_to_context(ctx, st->codecpar);
	ctx->pkt_timebase = st->time_base;
	if (const int rc = avcodec_open2(ctx, codec, nullptr); rc < 0) {
		*error = "Could not open the decoder: " + errText(rc);
		avcodec_free_context(&ctx);
		return nullptr;
	}
	return ctx;
}

double frameRate(AVFormatContext *fmt, AVStream *st)
{
	const AVRational r = av_guess_frame_rate(fmt, st, nullptr);
	return r.num > 0 && r.den > 0 ? av_q2d(r) : 30.0;
}

} // namespace

// ---- probe / audio --------------------------------------------------------------------------

Info probe(const QString &path)
{
	Info info;
	Input in;
	if (!in.open(path, &info.error))
		return info;
	if (in.video < 0) {
		info.error = "That file has no video.";
		return info;
	}
	AVStream *vs = in.fmt->streams[in.video];
	info.width = vs->codecpar->width;
	info.height = vs->codecpar->height;
	info.fps = frameRate(in.fmt, vs);
	info.duration = in.duration();
	if (info.duration <= 0 && vs->duration > 0)
		info.duration = vs->duration * av_q2d(vs->time_base);
	info.audioTracks = int(in.audio.size());
	info.ok = true;
	return info;
}

bool decodeAudio(const QString &path, int track, std::vector<float> *pcm, QString *error)
{
	pcm->clear();
	Input in;
	if (!in.open(path, error))
		return false;
	if (track < 0 || track >= int(in.audio.size())) {
		*error = "That audio track does not exist.";
		return false;
	}
	AVStream *st = in.fmt->streams[in.audio[track]];
	AVCodecContext *dec = openDecoder(st, error);
	if (!dec)
		return false;
	auto freeDec = qScopeGuard([&] { avcodec_free_context(&dec); });

	AVChannelLayout out = {};
	av_channel_layout_default(&out, 2);
	SwrContext *swr = nullptr;
	if (swr_alloc_set_opts2(&swr, &out, AV_SAMPLE_FMT_FLT, kRate, &dec->ch_layout, dec->sample_fmt, dec->sample_rate, 0, nullptr) < 0 ||
	    swr_init(swr) < 0) {
		swr_free(&swr);
		*error = "Could not convert this audio.";
		return false;
	}
	auto freeSwr = qScopeGuard([&] { swr_free(&swr); });

	// Streams rarely start exactly with the file; keep them in step by padding the front.
	const double lead = std::max(0.0, st->start_time == AV_NOPTS_VALUE ? 0.0 : st->start_time * av_q2d(st->time_base) - in.startTime());
	pcm->assign(size_t(lead * kRate) * 2, 0.0f);

	AVPacket *pkt = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();
	auto freeAv = qScopeGuard([&] {
		av_packet_free(&pkt);
		av_frame_free(&frame);
	});

	auto convert = [&](AVFrame *f) {
		const int cap = swr_get_out_samples(swr, f ? f->nb_samples : 0);
		if (cap <= 0)
			return;
		const size_t old = pcm->size();
		pcm->resize(old + size_t(cap) * 2);
		uint8_t *dst = reinterpret_cast<uint8_t *>(pcm->data() + old);
		const int n = swr_convert(swr, &dst, cap, f ? const_cast<const uint8_t **>(f->extended_data) : nullptr, f ? f->nb_samples : 0);
		pcm->resize(old + size_t(std::max(n, 0)) * 2);
	};
	auto drain = [&] {
		while (avcodec_receive_frame(dec, frame) == 0) {
			convert(frame);
			av_frame_unref(frame);
		}
	};
	while (av_read_frame(in.fmt, pkt) >= 0) {
		if (pkt->stream_index == in.audio[track] && avcodec_send_packet(dec, pkt) >= 0)
			drain();
		av_packet_unref(pkt);
	}
	avcodec_send_packet(dec, nullptr);
	drain();
	// Samples still held inside the resampler.
	for (;;) {
		const size_t old = pcm->size();
		pcm->resize(old + 4096 * 2);
		uint8_t *dst = reinterpret_cast<uint8_t *>(pcm->data() + old);
		const int n = swr_convert(swr, &dst, 4096, nullptr, 0);
		pcm->resize(old + size_t(std::max(n, 0)) * 2);
		if (n <= 0)
			break;
	}
	return true;
}

// ---- preview frames --------------------------------------------------------------------------

struct VideoReader::Impl {
	Input in;
	AVCodecContext *dec = nullptr;
	SwsContext *sws = nullptr;
	AVPacket *pkt = nullptr;
	AVFrame *frame = nullptr;
	AVStream *st = nullptr;
	double tb = 0, fps = 30, lastT = -1;
	int outW = 0, outH = 0;
	QImage last;

	~Impl()
	{
		sws_freeContext(sws);
		av_packet_free(&pkt);
		av_frame_free(&frame);
		avcodec_free_context(&dec);
	}
};

VideoReader::VideoReader() : d(new Impl) {}
VideoReader::~VideoReader() = default;

bool VideoReader::open(const QString &path, int maxW, int maxH, QString *error)
{
	if (!d->in.open(path, error))
		return false;
	if (d->in.video < 0) {
		*error = "That file has no video.";
		return false;
	}
	d->st = d->in.fmt->streams[d->in.video];
	d->dec = openDecoder(d->st, error);
	if (!d->dec)
		return false;
	d->tb = av_q2d(d->st->time_base);
	d->fps = frameRate(d->in.fmt, d->st);
	const double scale = std::min(double(maxW) / d->dec->width, double(maxH) / d->dec->height);
	d->outW = std::max(2, int(d->dec->width * scale) & ~1);
	d->outH = std::max(2, int(d->dec->height * scale) & ~1);
	d->pkt = av_packet_alloc();
	d->frame = av_frame_alloc();
	return true;
}

double VideoReader::duration() const
{
	return d->in.fmt ? d->in.duration() : 0.0;
}

QImage VideoReader::frameAt(double seconds)
{
	if (!d->dec)
		return {};
	const double base = d->in.startTime();
	const double target = seconds + base;
	const double half = 0.5 / d->fps;
	// Close to the last frame: it is still the right one.
	if (!d->last.isNull() && target >= d->lastT - half && target < d->lastT + half)
		return d->last;
	// Going backwards or jumping far ahead: restart from the keyframe before the target.
	if (d->lastT < 0 || target < d->lastT || target > d->lastT + 1.0) {
		av_seek_frame(d->in.fmt, d->in.video, int64_t(target / d->tb), AVSEEK_FLAG_BACKWARD);
		avcodec_flush_buffers(d->dec);
		d->lastT = -1;
	}
	while (av_read_frame(d->in.fmt, d->pkt) >= 0) {
		const bool mine = d->pkt->stream_index == d->in.video;
		if (mine) {
			// While catching up, frames nobody will see need not be fully decoded.
			const double pt = d->pkt->pts == AV_NOPTS_VALUE ? target : d->pkt->pts * d->tb;
			d->dec->skip_frame = pt < target - 0.4 ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
			avcodec_send_packet(d->dec, d->pkt);
		}
		av_packet_unref(d->pkt);
		while (mine && avcodec_receive_frame(d->dec, d->frame) == 0) {
			const double ft = d->frame->best_effort_timestamp * d->tb;
			if (ft < target - half) {
				av_frame_unref(d->frame);
				continue;
			}
			d->sws = sws_getCachedContext(d->sws, d->frame->width, d->frame->height, AVPixelFormat(d->frame->format), d->outW, d->outH,
						      AV_PIX_FMT_BGRA, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
			QImage img(d->outW, d->outH, QImage::Format_ARGB32);
			uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
			int linesize[4] = {int(img.bytesPerLine()), 0, 0, 0};
			sws_scale(d->sws, d->frame->data, d->frame->linesize, 0, d->frame->height, dst, linesize);
			d->lastT = ft;
			d->last = img;
			av_frame_unref(d->frame);
			d->dec->skip_frame = AVDISCARD_DEFAULT;
			return img;
		}
	}
	return {}; // past the end
}

// ---- export ------------------------------------------------------------------------------------

namespace {

struct Out {
	AVFormatContext *oc = nullptr;
	AVCodecContext *venc = nullptr;
	AVCodecContext *aenc = nullptr;
	AVDictionary *opts = nullptr;
	bool opened = false;
	bool headerWritten = false;
	~Out()
	{
		avcodec_free_context(&venc);
		avcodec_free_context(&aenc);
		av_dict_free(&opts);
		if (oc) {
			if (opened)
				avio_closep(&oc->pb);
			avformat_free_context(oc);
		}
	}
};

} // namespace

bool exportClip(const ExportJob &job, const std::function<bool(double)> &progress, QString *error)
{
	Input in;
	if (!in.open(job.input, error))
		return false;
	if (in.video < 0) {
		*error = "That file has no video.";
		return false;
	}
	AVStream *vin = in.fmt->streams[in.video];
	const double dur = in.duration();
	const double start = std::max(0.0, job.start);
	const double end = job.end > 0 ? std::min(job.end, dur) : dur;
	if (end - start < 0.2) {
		*error = "The selected part is too short.";
		return false;
	}
	const bool copyVideo = start <= 0.05 && end >= dur - 0.05;
	const double fps = frameRate(in.fmt, vin);
	const double tb = av_q2d(vin->time_base);

	// ---- Mix the audio tracks down (decoded fully, then cut and balanced).
	const size_t frames = size_t(std::llround((end - start) * kRate));
	std::vector<float> mix(frames * 2, 0.0f);
	bool anyAudio = false;
	for (int t = 0; t < int(in.audio.size()); ++t) {
		const float gain = t < int(job.gains.size()) ? job.gains[t] : 1.0f;
		if (gain <= 0.001f)
			continue;
		std::vector<float> pcm;
		if (!decodeAudio(job.input, t, &pcm, error))
			return false;
		const size_t from = size_t(std::llround(start * kRate)) * 2;
		for (size_t i = 0; i < mix.size() && from + i < pcm.size(); ++i)
			mix[i] += pcm[from + i] * gain;
		anyAudio = true;
	}
	for (float &x : mix) { // gentle limiter: no hard clipping when tracks add up
		const float a = std::fabs(x);
		if (a > 0.9f)
			x = std::copysign(0.9f + 0.1f * std::tanh((a - 0.9f) / 0.1f), x);
	}

	// ---- Output file.
	Out out;
	int rc = avformat_alloc_output_context2(&out.oc, nullptr, "mp4", job.output.toUtf8().constData());
	if (rc < 0 || !out.oc) {
		*error = "Could not create the output: " + errText(rc);
		return false;
	}
	auto fail = [&](const QString &msg, int code = 0) {
		*error = code < 0 ? msg + ": " + errText(code) : msg;
		return false;
	};
	const bool globalHeader = out.oc->oformat->flags & AVFMT_GLOBALHEADER;

	AVStream *vout = avformat_new_stream(out.oc, nullptr);
	AVCodecContext *vdec = nullptr;
	SwsContext *sws = nullptr;
	AVFrame *encFrame = nullptr, *decFrame = av_frame_alloc();
	AVPacket *pkt = av_packet_alloc();
	std::deque<AVPacket *> audioPackets;
	auto cleanup = qScopeGuard([&] {
		avcodec_free_context(&vdec);
		sws_freeContext(sws);
		av_frame_free(&encFrame);
		av_frame_free(&decFrame);
		av_packet_free(&pkt);
		for (AVPacket *p : audioPackets)
			av_packet_free(&p);
	});

	if (copyVideo) {
		avcodec_parameters_copy(vout->codecpar, vin->codecpar);
		vout->codecpar->codec_tag = 0;
		vout->time_base = vin->time_base;
	} else {
		vdec = openDecoder(vin, error);
		if (!vdec)
			return false;
		const AVCodec *codec = avcodec_find_encoder_by_name("libx264");
		out.venc = codec ? avcodec_alloc_context3(codec) : nullptr;
		if (!out.venc)
			return fail("This build has no H.264 encoder for trimming");
		out.venc->width = vdec->width & ~1;
		out.venc->height = vdec->height & ~1;
		out.venc->pix_fmt = AV_PIX_FMT_YUV420P;
		out.venc->framerate = av_d2q(fps, 100000);
		out.venc->time_base = av_inv_q(out.venc->framerate);
		out.venc->gop_size = int(fps * 2);
		out.venc->color_range = vdec->color_range;
		out.venc->colorspace = vdec->colorspace;
		out.venc->color_primaries = vdec->color_primaries;
		out.venc->color_trc = vdec->color_trc;
		if (globalHeader)
			out.venc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		av_opt_set(out.venc->priv_data, "preset", "veryfast", 0);
		av_opt_set(out.venc->priv_data, "crf", "17", 0);
		if ((rc = avcodec_open2(out.venc, codec, nullptr)) < 0)
			return fail("Could not start the video encoder", rc);
		avcodec_parameters_from_context(vout->codecpar, out.venc);
		vout->time_base = out.venc->time_base;
		encFrame = av_frame_alloc();
		encFrame->format = out.venc->pix_fmt;
		encFrame->width = out.venc->width;
		encFrame->height = out.venc->height;
		if ((rc = av_frame_get_buffer(encFrame, 0)) < 0)
			return fail("Out of memory", rc);
	}

	AVStream *aout = nullptr;
	if (anyAudio) {
		const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
		out.aenc = codec ? avcodec_alloc_context3(codec) : nullptr;
		if (!out.aenc)
			return fail("This build has no AAC encoder");
		out.aenc->sample_rate = kRate;
		out.aenc->sample_fmt = AV_SAMPLE_FMT_FLTP;
		av_channel_layout_default(&out.aenc->ch_layout, 2);
		out.aenc->bit_rate = 192000;
		out.aenc->time_base = AVRational{1, kRate};
		if (globalHeader)
			out.aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		if ((rc = avcodec_open2(out.aenc, codec, nullptr)) < 0)
			return fail("Could not start the audio encoder", rc);
		aout = avformat_new_stream(out.oc, nullptr);
		avcodec_parameters_from_context(aout->codecpar, out.aenc);
		aout->time_base = out.aenc->time_base;

		// Encode all the audio now; the packets are interleaved with the video as it is written.
		AVFrame *af = av_frame_alloc();
		af->format = AV_SAMPLE_FMT_FLTP;
		af->sample_rate = kRate;
		av_channel_layout_default(&af->ch_layout, 2);
		af->nb_samples = out.aenc->frame_size > 0 ? out.aenc->frame_size : 1024;
		auto freeAf = qScopeGuard([&] { av_frame_free(&af); });
		int64_t pts = 0;
		auto pull = [&] {
			for (;;) {
				AVPacket *p = av_packet_alloc();
				if (avcodec_receive_packet(out.aenc, p) < 0) {
					av_packet_free(&p);
					return;
				}
				audioPackets.push_back(p);
			}
		};
		for (size_t pos = 0; pos < frames; pos += size_t(af->nb_samples)) {
			if (av_frame_get_buffer(af, 0) < 0 || av_frame_make_writable(af) < 0)
				return fail("Out of memory");
			float *l = reinterpret_cast<float *>(af->data[0]), *r = reinterpret_cast<float *>(af->data[1]);
			for (int i = 0; i < af->nb_samples; ++i) {
				const size_t k = (pos + size_t(i)) * 2;
				l[i] = k + 1 < mix.size() ? mix[k] : 0.0f; // the last frame is padded with silence
				r[i] = k + 1 < mix.size() ? mix[k + 1] : 0.0f;
			}
			af->pts = pts;
			pts += af->nb_samples;
			if (avcodec_send_frame(out.aenc, af) < 0)
				return fail("Audio encoding failed");
			pull();
			av_frame_unref(af);
			af->format = AV_SAMPLE_FMT_FLTP;
			af->sample_rate = kRate;
			av_channel_layout_default(&af->ch_layout, 2);
			af->nb_samples = out.aenc->frame_size > 0 ? out.aenc->frame_size : 1024;
		}
		avcodec_send_frame(out.aenc, nullptr);
		pull();
	}

	if ((rc = avio_open(&out.oc->pb, job.output.toUtf8().constData(), AVIO_FLAG_WRITE)) < 0)
		return fail("Could not write the file", rc);
	out.opened = true;
	av_dict_set(&out.opts, "movflags", "+faststart", 0);
	if ((rc = avformat_write_header(out.oc, &out.opts)) < 0)
		return fail("Could not write the file", rc);
	out.headerWritten = true;

	bool cancelled = false;
	// Writes the audio packets that are due by `upTo` seconds, keeping the file interleaved.
	auto flushAudio = [&](double upTo) {
		while (!audioPackets.empty() && audioPackets.front()->pts * av_q2d(out.aenc->time_base) <= upTo) {
			AVPacket *p = audioPackets.front();
			audioPackets.pop_front();
			p->stream_index = aout->index;
			av_packet_rescale_ts(p, out.aenc->time_base, aout->time_base);
			av_interleaved_write_frame(out.oc, p);
			av_packet_free(&p);
		}
	};
	auto writeVideo = [&](AVPacket *p, AVRational from, double seconds) {
		flushAudio(seconds);
		p->stream_index = vout->index;
		av_packet_rescale_ts(p, from, vout->time_base);
		return av_interleaved_write_frame(out.oc, p);
	};
	auto report = [&](double seconds) {
		if (!progress(std::clamp((seconds - start) / (end - start), 0.0, 1.0)))
			cancelled = true;
	};

	if (copyVideo) {
		while (!cancelled && av_read_frame(in.fmt, pkt) >= 0) {
			if (pkt->stream_index == in.video) {
				const double t = (pkt->pts == AV_NOPTS_VALUE ? 0 : pkt->pts) * tb - in.startTime();
				if (t > end) {
					av_packet_unref(pkt);
					break;
				}
				if (pkt->pts != AV_NOPTS_VALUE)
					pkt->pts -= int64_t(in.startTime() / tb);
				if (pkt->dts != AV_NOPTS_VALUE)
					pkt->dts -= int64_t(in.startTime() / tb);
				writeVideo(pkt, vin->time_base, t);
				report(t);
			}
			av_packet_unref(pkt);
		}
	} else {
		av_seek_frame(in.fmt, in.video, int64_t((start + in.startTime()) / tb), AVSEEK_FLAG_BACKWARD);
		avcodec_flush_buffers(vdec);
		int64_t lastPts = -1;
		bool done = false;
		auto encode = [&](AVFrame *f) { // f == nullptr flushes
			if (avcodec_send_frame(out.venc, f) < 0)
				return;
			for (;;) {
				AVPacket *p = av_packet_alloc();
				if (avcodec_receive_packet(out.venc, p) < 0) {
					av_packet_free(&p);
					return;
				}
				const double t = p->pts * av_q2d(out.venc->time_base);
				writeVideo(p, out.venc->time_base, t);
				av_packet_free(&p);
			}
		};
		while (!done && !cancelled && av_read_frame(in.fmt, pkt) >= 0) {
			const bool mine = pkt->stream_index == in.video;
			if (mine)
				avcodec_send_packet(vdec, pkt);
			av_packet_unref(pkt);
			while (mine && avcodec_receive_frame(vdec, decFrame) == 0) {
				const double ft = decFrame->best_effort_timestamp * tb - in.startTime();
				if (ft < start - 0.5 / fps) {
					av_frame_unref(decFrame);
					continue;
				}
				if (ft >= end) {
					av_frame_unref(decFrame);
					done = true;
					break;
				}
				av_frame_make_writable(encFrame);
				sws = sws_getCachedContext(sws, decFrame->width, decFrame->height, AVPixelFormat(decFrame->format), out.venc->width,
							   out.venc->height, out.venc->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
				sws_scale(sws, decFrame->data, decFrame->linesize, 0, decFrame->height, encFrame->data, encFrame->linesize);
				encFrame->pts = std::max<int64_t>(lastPts + 1, std::llround((ft - start) * fps));
				lastPts = encFrame->pts;
				encode(encFrame);
				report(ft);
				av_frame_unref(decFrame);
			}
		}
		if (!cancelled)
			encode(nullptr);
	}

	if (cancelled) {
		out.headerWritten = false;
		*error = "Cancelled";
		avio_closep(&out.oc->pb);
		out.opened = false;
		QFile::remove(job.output);
		return false;
	}
	flushAudio(1e9);
	if ((rc = av_write_trailer(out.oc)) < 0)
		return fail("Could not finish the file", rc);
	progress(1.0);
	return true;
}

} // namespace ClipMedia
