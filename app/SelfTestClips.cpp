// Self-test for the clip pipeline (ClipMedia): builds a clip with three known tones as three audio tracks,
// then checks probing, decoding, trimming and the mixdown against what must come out.
#include "ClipEditor.h"
#include "ClipMedia.h"
#include "SelfTest.h"

#include <QDir>
#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include <cmath>
#include <cstring>

namespace {

constexpr double kSeconds = 3.0;
constexpr double kTones[3] = {440.0, 880.0, 1320.0};

// 3 s, 240x426 grey-ramp video + three stereo AAC tracks, one pure tone each.
bool makeClip(const QString &path, QString *err)
{
	AVFormatContext *oc = nullptr;
	avformat_alloc_output_context2(&oc, nullptr, "mp4", path.toUtf8().constData());
	const AVCodec *vc = avcodec_find_encoder_by_name("libx264");
	const AVCodec *ac = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (!oc || !vc || !ac) {
		*err = "encoders missing";
		return false;
	}
	AVCodecContext *venc = avcodec_alloc_context3(vc);
	venc->width = 240;
	venc->height = 426;
	venc->pix_fmt = AV_PIX_FMT_YUV420P;
	venc->time_base = {1, 30};
	venc->framerate = {30, 1};
	venc->gop_size = 60;
	venc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	av_opt_set(venc->priv_data, "preset", "ultrafast", 0);
	avcodec_open2(venc, vc, nullptr);
	AVStream *vs = avformat_new_stream(oc, nullptr);
	avcodec_parameters_from_context(vs->codecpar, venc);
	vs->time_base = venc->time_base;

	AVCodecContext *aenc[3];
	AVStream *as[3];
	for (int t = 0; t < 3; ++t) {
		aenc[t] = avcodec_alloc_context3(ac);
		aenc[t]->sample_rate = 48000;
		aenc[t]->sample_fmt = AV_SAMPLE_FMT_FLTP;
		av_channel_layout_default(&aenc[t]->ch_layout, 2);
		aenc[t]->bit_rate = 128000;
		aenc[t]->time_base = {1, 48000};
		aenc[t]->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		avcodec_open2(aenc[t], ac, nullptr);
		as[t] = avformat_new_stream(oc, nullptr);
		avcodec_parameters_from_context(as[t]->codecpar, aenc[t]);
		as[t]->time_base = aenc[t]->time_base;
	}
	avio_open(&oc->pb, path.toUtf8().constData(), AVIO_FLAG_WRITE);
	if (avformat_write_header(oc, nullptr) < 0) {
		*err = "write_header failed";
		return false;
	}

	auto drain = [&](AVCodecContext *enc, AVStream *st) {
		for (;;) {
			AVPacket *p = av_packet_alloc();
			if (avcodec_receive_packet(enc, p) < 0) {
				av_packet_free(&p);
				return;
			}
			av_packet_rescale_ts(p, enc->time_base, st->time_base);
			p->stream_index = st->index;
			av_interleaved_write_frame(oc, p);
			av_packet_free(&p);
		}
	};

	AVFrame *vf = av_frame_alloc();
	vf->format = AV_PIX_FMT_YUV420P;
	vf->width = 240;
	vf->height = 426;
	av_frame_get_buffer(vf, 0);
	const int frames = int(kSeconds * 30);
	for (int i = 0; i < frames; ++i) {
		av_frame_make_writable(vf);
		for (int y = 0; y < vf->height; ++y)
			memset(vf->data[0] + y * vf->linesize[0], 40 + (i * 2 + y / 4) % 180, size_t(vf->width));
		memset(vf->data[1], 128, size_t(vf->linesize[1]) * size_t(vf->height / 2));
		memset(vf->data[2], 128, size_t(vf->linesize[2]) * size_t(vf->height / 2));
		vf->pts = i;
		avcodec_send_frame(venc, vf);
		drain(venc, vs);
	}
	avcodec_send_frame(venc, nullptr);
	drain(venc, vs);

	for (int t = 0; t < 3; ++t) {
		AVFrame *af = av_frame_alloc();
		int64_t n = 0;
		for (; n + 1024 <= int64_t(kSeconds * 48000); n += 1024) {
			af->format = AV_SAMPLE_FMT_FLTP;
			af->sample_rate = 48000;
			av_channel_layout_default(&af->ch_layout, 2);
			af->nb_samples = 1024;
			av_frame_get_buffer(af, 0);
			for (int i = 0; i < 1024; ++i) {
				const float v = 0.4f * float(std::sin(2 * M_PI * kTones[t] * double(n + i) / 48000.0));
				reinterpret_cast<float *>(af->data[0])[i] = v;
				reinterpret_cast<float *>(af->data[1])[i] = v;
			}
			af->pts = n;
			avcodec_send_frame(aenc[t], af);
			drain(aenc[t], as[t]);
			av_frame_unref(af);
		}
		avcodec_send_frame(aenc[t], nullptr);
		drain(aenc[t], as[t]);
		av_frame_free(&af);
	}
	av_write_trailer(oc);
	avio_closep(&oc->pb);
	avformat_free_context(oc);
	return true;
}

// Strength of one frequency in interleaved stereo (Goertzel), normalised so a full-scale sine = 1.
double tone(const std::vector<float> &pcm, double hz)
{
	const size_t n = pcm.size() / 2;
	if (n < 1000)
		return 0;
	const double w = 2 * M_PI * hz / 48000.0, c = 2 * std::cos(w);
	double s1 = 0, s2 = 0;
	for (size_t i = 0; i < n; ++i) {
		const double s = pcm[i * 2] + c * s1 - s2;
		s2 = s1;
		s1 = s;
	}
	return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / (double(n) / 2.0);
}

} // namespace

namespace SelfTest {

int clips(const QString &outDir)
{
	QDir().mkpath(outDir);
	QFile report(QDir(outDir).filePath("clips.txt"));
	report.open(QIODevice::WriteOnly | QIODevice::Truncate);
	int failures = 0;
	auto check = [&](bool ok, const QString &what) {
		report.write((QString(ok ? "PASS  " : "FAIL  ") + what + "\n").toUtf8());
		report.flush();
		failures += ok ? 0 : 1;
	};

	const QString src = QDir(outDir).filePath("source.mp4");
	QString err;
	if (!makeClip(src, &err)) {
		check(false, "could not build the test clip: " + err);
		return 1;
	}
	const ClipMedia::Info info = ClipMedia::probe(src);
	check(info.ok && info.audioTracks == 3, QString("probe: 3 audio tracks (%1)").arg(info.audioTracks));
	check(std::fabs(info.duration - kSeconds) < 0.2, QString("probe: duration %1").arg(info.duration));
	check(info.width == 240 && info.height == 426, "probe: size");

	std::vector<float> pcm;
	for (int t = 0; t < 3; ++t) {
		const bool ok = ClipMedia::decodeAudio(src, t, &pcm, &err);
		check(ok && std::fabs(double(pcm.size()) / 96000 - kSeconds) < 0.2, QString("decode track %1 length (%2 s)").arg(t).arg(double(pcm.size()) / 96000));
		const double own = tone(pcm, kTones[t]), other = tone(pcm, kTones[(t + 1) % 3]);
		check(own > 0.3 && other < 0.03, QString("track %1 carries its own tone (%2 vs %3)").arg(t).arg(own, 0, 'f', 2).arg(other, 0, 'f', 3));
	}

	ClipMedia::VideoReader reader;
	check(reader.open(src, 270, 480, &err), "preview reader opens");
	const QImage a = reader.frameAt(0.5), b = reader.frameAt(2.5), c = reader.frameAt(0.2);
	check(!a.isNull() && !b.isNull() && !c.isNull(), "preview frames at 0.5 s, 2.5 s and back to 0.2 s");
	check(!a.isNull() && a.height() <= 480 && a.width() <= 270, "preview frame is scaled to fit");
	check(reader.frameAt(30.0).isNull(), "no frame past the end");
	if (!a.isNull())
		a.save(QDir(outDir).filePath("frame.png"));

	auto exportWith = [&](const QString &name, double s, double e, std::vector<float> gains, ClipMedia::Info *outInfo, std::vector<float> *outPcm) {
		ClipMedia::ExportJob job;
		job.input = src;
		job.output = QDir(outDir).filePath(name);
		job.start = s;
		job.end = e;
		job.gains = gains;
		const bool ok = ClipMedia::exportClip(job, [&](double) { return true; }, &err);
		check(ok, QString("export %1 %2").arg(name, ok ? QString() : err));
		if (!ok)
			return false;
		*outInfo = ClipMedia::probe(job.output);
		if (outInfo->audioTracks > 0)
			ClipMedia::decodeAudio(job.output, 0, outPcm, &err);
		else
			outPcm->clear();
		return outInfo->ok;
	};

	ClipMedia::Info o;
	std::vector<float> op;
	if (exportWith("whole_track1.mp4", 0, 0, {1, 0, 0}, &o, &op)) {
		check(o.audioTracks == 1 && std::fabs(o.duration - kSeconds) < 0.3, QString("whole clip, one track: 1 audio stream, %1 s").arg(o.duration));
		check(tone(op, 440) > 0.25 && tone(op, 880) < 0.05, "mixdown keeps track 1 and drops the others");
	}
	if (exportWith("trim_tracks23.mp4", 1.0, 2.0, {0, 1, 1}, &o, &op)) {
		check(std::fabs(o.duration - 1.0) < 0.2, QString("trim 1.0-2.0 s gives %1 s").arg(o.duration));
		check(tone(op, 880) > 0.25 && tone(op, 1320) > 0.25 && tone(op, 440) < 0.05, "trimmed mix has tracks 2 and 3 only");
		check(o.width == 240 && o.height == 426, "trimmed video keeps its size");
		ClipMedia::VideoReader r;
		QString e;
		const bool opened = r.open(QDir(outDir).filePath("trim_tracks23.mp4"), 240, 426, &e);
		check(opened && !r.frameAt(0.3).isNull(), "trimmed video decodes");
	}
	if (exportWith("half_gain.mp4", 0.5, 2.5, {0.5f, 0.0f, 0.0f}, &o, &op))
		check(std::fabs(tone(op, 440) - 0.2) < 0.06, QString("gain 0.5 halves the level (%1)").arg(tone(op, 440), 0, 'f', 3));
	if (exportWith("silent.mp4", 0.5, 2.0, {0, 0, 0}, &o, &op))
		check(o.audioTracks == 0, "all tracks muted: no audio stream");

	{ // cancelling removes the partial file
		ClipMedia::ExportJob job;
		job.input = src;
		job.output = QDir(outDir).filePath("cancelled.mp4");
		job.start = 0.5;
		job.end = 2.5;
		job.gains = {1, 1, 1};
		const bool ok = ClipMedia::exportClip(job, [](double) { return false; }, &err);
		check(!ok && !QFile::exists(job.output), "cancelled export leaves no file");
	}
	// ---- The editor window itself: open, balance tracks through the controls, save.
	{
		QPointer<ClipEditor> editor = new ClipEditor(src, "Test clip", "private", true);
		check(editor->valid(), "editor opens the clip");
		editor->show();
		QEventLoop loop;
		QTimer::singleShot(1200, &loop, &QEventLoop::quit); // lets the audio decode finish
		loop.exec();
		editor->grab().save(QDir(outDir).filePath("editor.png"));

		const auto toggles = editor->findChildren<QToolButton *>();
		// Rows are Mute, Solo per track in order: unmute track 2 (index 3) and mute the Mix (index 0).
		QToolButton *muteMix = nullptr, *muteTrack2 = nullptr;
		int muteSeen = 0;
		for (QToolButton *b : toggles)
			if (b->text() == "M") {
				if (muteSeen == 0)
					muteMix = b;
				if (muteSeen == 1)
					muteTrack2 = b;
				++muteSeen;
			}
		check(muteSeen == 3, QString("editor shows a mute button per audio track (%1)").arg(muteSeen));
		if (muteMix && muteTrack2) {
			check(!muteMix->isChecked() && muteTrack2->isChecked(), "defaults: Mix on, other tracks muted");
			muteMix->setChecked(true);
			muteTrack2->setChecked(false);
		}
		QString savedPath;
		QObject::connect(editor, &ClipEditor::exported, [&](const QString &p, bool, const QString &, const QString &) { savedPath = p; });
		QPushButton *save = nullptr;
		for (QPushButton *b : editor->findChildren<QPushButton *>())
			if (b->text() == "Save edited clip")
				save = b;
		check(save != nullptr, "editor has a Save button");
		if (save) {
			save->click();
			for (int i = 0; i < 100 && savedPath.isEmpty(); ++i) {
				QEventLoop l;
				QTimer::singleShot(100, &l, &QEventLoop::quit);
				l.exec();
			}
			check(!savedPath.isEmpty() && QFile::exists(savedPath), "editor saved an edited clip");
			if (!savedPath.isEmpty()) {
				std::vector<float> got;
				QString e;
				ClipMedia::decodeAudio(savedPath, 0, &got, &e);
				check(tone(got, 880) > 0.25 && tone(got, 440) < 0.05, "saved clip carries the track chosen in the editor (track 2 only)");
			}
		}
		check(editor.isNull(), "editor closes itself after saving");
		if (editor)
			editor->close();
	}
	return failures ? 1 : 0;
}

} // namespace SelfTest
