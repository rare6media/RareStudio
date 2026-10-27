#include "SelfTest.h"

#include "ObsCore.h"
#include "FiltersDialog.h"
#include "MainWindow.h"
#include "SettingsDialog.h"
#include "TransitionManager.h"
#include "TransitionsDialog.h"
#include "VerticalClip.h"

#include <obs.h>
#include <media-io/video-io.h>

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QMutex>
#include <QPainter>
#include <QTabWidget>
#include <QTimer>

#include <cmath>

namespace SelfTest {
namespace {

constexpr int kFrameW = 288, kFrameH = 162;

struct Capture {
	QMutex mutex;
	QImage latest;
	float time = 0.0f;   // transition progress when the frame was grabbed
	obs_source_t *transition = nullptr;
};

void onFrame(void *param, video_data *frame)
{
	auto *cap = static_cast<Capture *>(param);
	// BGRA bytes == QImage::Format_ARGB32 on little-endian.
	QImage img(frame->data[0], kFrameW, kFrameH, int(frame->linesize[0]), QImage::Format_ARGB32);
	QMutexLocker lock(&cap->mutex);
	cap->latest = img.copy();
	cap->time = cap->transition ? obs_transition_get_time(cap->transition) : 0.0f;
}

obs_scene_t *makeScene(const char *name, uint32_t bg, uint32_t boxColor, int boxX, int boxY)
{
	obs_scene_t *scene = ObsCore::instance().createScene(name);
	auto add = [&](const QString &n, uint32_t color, int w, int h, int x, int y) {
		obs_data_t *s = obs_data_create();
		obs_data_set_int(s, "color", color);
		obs_data_set_int(s, "width", w);
		obs_data_set_int(s, "height", h);
		obs_source_t *src = obs_source_create("color_source_v3", n.toUtf8().constData(), s, nullptr);
		obs_data_release(s);
		obs_sceneitem_t *item = obs_scene_add(scene, src);
		vec2 pos = {float(x), float(y)};
		obs_sceneitem_set_pos(item, &pos);
		obs_source_release(src);
	};
	add(QString("%1-bg").arg(name), bg, 1920, 1080, 0, 0);
	add(QString("%1-box").arg(name), boxColor, 700, 400, boxX, boxY);
	return scene;
}

void wait(int ms)
{
	QEventLoop loop;
	QTimer::singleShot(ms, &loop, &QEventLoop::quit);
	loop.exec();
}

} // namespace

int transitions(const QString &outDir)
{
	ObsCore &core = ObsCore::instance();
	QDir().mkpath(outDir);

	// ABGR colours: red / white box, blue / yellow box.
	obs_scene_t *a = makeScene("A", 0xFF2030E0, 0xFFFFFFFF, 300, 250);
	obs_scene_t *b = makeScene("B", 0xFFE06020, 0xFF20D0FF, 1000, 500);
	core.setCurrentScene(a);
	wait(600);

	Capture cap;
	video_scale_info conv = {};
	conv.format = VIDEO_FORMAT_BGRA;
	conv.width = kFrameW;
	conv.height = kFrameH;
	conv.range = VIDEO_RANGE_FULL;
	conv.colorspace = VIDEO_CS_709;
	obs_add_raw_video_callback(&conv, onFrame, &cap);

	struct Row {
		QString name;
		QList<QImage> frames;
	};
	QList<Row> rows;
	const float targets[] = {0.2f, 0.4f, 0.6f, 0.8f};
	obs_scene_t *to = b;
	const int length = 2400; // ms, slowed so frames land near the targets

	auto &tm = TransitionManager::instance();
	const QList<TransitionDef> defs = tm.defs(); // copy: setCurrent/setMs touch the list
	for (const TransitionDef &def : defs) {
		if (def.type == "stinger_transition" || def.type == "cut_transition")
			continue; // a stinger needs a video file; a cut has nothing in between
		tm.setMs(def.name, length);
		tm.setCurrent(def.name); // the manager re-installs on every scene change, so go through it
		Row row;
		row.name = def.name;
		QList<bool> got(4, false);
		row.frames = {QImage(), QImage(), QImage(), QImage()};

		core.setCurrentScene(to);
		{
			QMutexLocker lock(&cap.mutex);
			cap.transition = core.transitionSource();
		}
		QElapsedTimer t;
		t.start();
		while (t.elapsed() < length + 500) {
			wait(8);
			QMutexLocker lock(&cap.mutex);
			for (int i = 0; i < 4; ++i)
				if (!got[i] && cap.time > 0.0f && std::fabs(cap.time - targets[i]) < 0.05f && !cap.latest.isNull()) {
					row.frames[i] = cap.latest;
					got[i] = true;
				}
		}
		rows.append(row);
		to = (to == a) ? b : a;
	}
	obs_remove_raw_video_callback(onFrame, &cap);

	// Contact sheets of 6 transitions each.
	const int labelW = 190, perSheet = 6, pad = 4;
	for (int sheet = 0; sheet * perSheet < rows.size(); ++sheet) {
		const int n = std::min(perSheet, int(rows.size()) - sheet * perSheet);
		QImage img(labelW + 4 * (kFrameW + pad), n * (kFrameH + pad), QImage::Format_RGB32);
		img.fill(QColor(20, 20, 26));
		QPainter p(&img);
		p.setPen(Qt::white);
		p.setFont(QFont("Segoe UI", 9, QFont::DemiBold));
		for (int r = 0; r < n; ++r) {
			const Row &row = rows[sheet * perSheet + r];
			p.drawText(QRect(6, r * (kFrameH + pad), labelW - 10, kFrameH), Qt::AlignVCenter | Qt::TextWordWrap, row.name);
			for (int c = 0; c < 4; ++c)
				if (!row.frames[c].isNull())
					p.drawImage(labelW + c * (kFrameW + pad), r * (kFrameH + pad), row.frames[c]);
		}
		p.end();
		img.save(QDir(outDir).filePath(QString("transitions_%1.png").arg(sheet + 1)));
	}
	return 0;
}

int vertical(const QString &outDir)
{
	QDir().mkpath(outDir);
	ObsCore &core = ObsCore::instance();
	obs_scene_t *a = makeScene("A", 0xFF2030E0, 0xFFFFFFFF, 300, 250);
	core.setCurrentScene(a);

	VerticalClip clip;
	QString err;
	QFile result(QDir(outDir).filePath("vertical.txt"));
	result.open(QIODevice::WriteOnly | QIODevice::Truncate);
	auto say = [&](const QString &line) {
		result.write(line.toUtf8() + "\n");
		result.flush();
	};

	if (!clip.start(&err)) {
		say("FAIL start: " + err);
		return 1;
	}
	clip.programChanged(a);
	say("started");

	// Grab one frame straight from the vertical canvas.
	struct Grab {
		QMutex m;
		QImage img;
	} grab;
	video_scale_info conv = {};
	conv.format = VIDEO_FORMAT_BGRA;
	conv.width = 270;
	conv.height = 480;
	conv.range = VIDEO_RANGE_FULL;
	conv.colorspace = VIDEO_CS_709;
	const bool connected = video_output_connect(
		clip.video(), &conv,
		[](void *p, video_data *f) {
			auto *g = static_cast<Grab *>(p);
			QMutexLocker l(&g->m);
			g->img = QImage(f->data[0], 270, 480, int(f->linesize[0]), QImage::Format_ARGB32).copy();
		},
		&grab);
	say(connected ? "frame grab connected" : "FAIL: could not connect a frame grab to the vertical canvas");

	wait(4500); // let the replay buffer fill

	QString savedPath;
	QObject::connect(&clip, &VerticalClip::clipSaved, [&](const QString &p) { savedPath = p; });
	say(clip.saveClip() ? "save requested" : "FAIL save request");
	for (int i = 0; i < 100 && savedPath.isEmpty(); ++i)
		wait(100);

	{
		QMutexLocker l(&grab.m);
		grab.img.save(QDir(outDir).filePath("vertical.png"));
	}
	const QFileInfo fi(savedPath);
	const bool ok = fi.exists() && fi.size() > 50000;
	say(ok ? QString("OK clip %1 (%2 KB)").arg(savedPath).arg(fi.size() / 1024)
	       : QString("FAIL clip missing or tiny: [%1]").arg(savedPath));
	clip.stop();
	return ok ? 0 : 1;
}

int video(const QString &outDir)
{
	QDir().mkpath(outDir);
	QFile report(QDir(outDir).filePath("video.txt"));
	report.open(QIODevice::WriteOnly | QIODevice::Truncate);
	auto say = [&](const QString &s) {
		report.write(s.toUtf8() + "\n");
		report.flush();
	};

	ObsCore &core = ObsCore::instance();
	obs_scene_t *scene = core.createScene("videotest");
	core.setCurrentScene(scene);
	say("scene on air; adding dshow_input (default webcam)");
	wait(500);

	obs_source_t *cam = core.addSource("dshow_input", "Video Capture Device");
	say(cam ? "source added" : "addSource failed");
	if (cam) {
		// Same thing the properties dialog does: pick the first real device and push it live.
		obs_properties_t *props = obs_source_properties(cam);
		if (obs_property_t *p = obs_properties_get(props, "video_device_id")) {
			for (size_t i = 0; i < obs_property_list_item_count(p); ++i) {
				const char *id = obs_property_list_item_string(p, i);
				if (!id || !*id)
					continue;
				say(QString("device: %1").arg(QString::fromUtf8(obs_property_list_item_name(p, i))));
				obs_data_t *s = obs_source_get_settings(cam);
				obs_data_set_string(s, "video_device_id", id);
				obs_source_update(cam, s);
				obs_data_release(s);
				break;
			}
		}
		obs_properties_destroy(props);
	}
	wait(4000); // several frames through the async convert + draw path
	if (cam) {
		say(QString("size %1x%2").arg(obs_source_get_width(cam)).arg(obs_source_get_height(cam)));
		obs_source_release(cam);
	}
	say("survived");
	return 0;
}

int ui(const QString &outDir)
{
	QDir().mkpath(outDir);
	{
		MainWindow w;
		w.resize(1500, 900);
		w.show();
		wait(1500);
		w.grab().save(QDir(outDir).filePath("main.png"));

		SettingsDialog settings(&w);
		settings.show();
		wait(300);
		if (auto *tabs = settings.findChild<QTabWidget *>()) {
			for (int i = 0; i < tabs->count(); ++i) {
				tabs->setCurrentIndex(i);
				wait(150);
				settings.grab().save(QDir(outDir).filePath(QString("settings_%1.png").arg(tabs->tabText(i))));
			}
		}
		settings.close();

		TransitionsDialog transitions(&w);
		transitions.show();
		wait(300);
		transitions.grab().save(QDir(outDir).filePath("transitions_dialog.png"));
		transitions.close();

		if (obs_source_t *mic = ObsCore::instance().micAudio()) {
			FiltersDialog filters(mic, &w);
			filters.show();
			wait(300);
			filters.grab().save(QDir(outDir).filePath("filters_dialog.png"));
			filters.close();
		}
		w.close();
	}
	return 0;
}

} // namespace SelfTest
