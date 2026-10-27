#include "SelfTest.h"

#include "ObsCore.h"
#include "FiltersDialog.h"
#include "MainWindow.h"
#include "PropertiesDialog.h"
#include "Secrets.h"
#include "SettingsDialog.h"
#include "TransitionManager.h"
#include "TransitionsDialog.h"
#include "VerticalClip.h"

#include <obs.h>
#include <media-io/video-io.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSettings>
#include <QTableWidget>
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
#include <functional>
#include <tuple>

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
	// Test clips go next to the report, never into your Videos folder (the old setting comes back after).
	const QVariant oldDir = QSettings().value("record/dir");
	QSettings().setValue("record/dir", outDir);
	auto restoreDir = qScopeGuard([oldDir] {
		if (oldDir.isValid())
			QSettings().setValue("record/dir", oldDir);
		else
			QSettings().remove("record/dir");
	});
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

namespace {

// Runs `act` on the next modal dialog that opens (the code under test is blocked inside exec()).
void onNextModal(std::function<void(QDialog *)> act, QWidget *notThis = nullptr)
{
	auto *poll = new QTimer;
	poll->setInterval(30);
	QObject::connect(poll, &QTimer::timeout, [poll, act, notThis] {
		auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		if (!d || d == notThis)
			return;
		poll->stop();
		poll->deleteLater();
		QTimer::singleShot(200, d, [d, act] { act(d); });
	});
	poll->start();
}

void clickOk(QDialog *d)
{
	if (auto *box = d->findChild<QDialogButtonBox *>())
		if (QPushButton *ok = box->button(QDialogButtonBox::Ok))
			ok->click();
}

QLineEdit *byPlaceholder(QDialog *d, const QString &text)
{
	for (QLineEdit *e : d->findChildren<QLineEdit *>())
		if (e->placeholderText() == text)
			return e;
	return nullptr;
}

// The field next to a form label in a PropertiesDialog, or null if that row isn't shown.
QWidget *fieldFor(QDialog *d, const QString &label)
{
	auto *scroll = d->findChild<QScrollArea *>();
	auto *form = scroll && scroll->widget() ? qobject_cast<QFormLayout *>(scroll->widget()->layout()) : nullptr;
	if (!form)
		return nullptr;
	for (int r = 0; r < form->rowCount(); ++r) {
		auto *l = form->itemAt(r, QFormLayout::LabelRole);
		auto *f = form->itemAt(r, QFormLayout::FieldRole);
		if (l && f && qobject_cast<QLabel *>(l->widget()) &&
		    static_cast<QLabel *>(l->widget())->text() == label)
			return f->widget();
	}
	return nullptr;
}

} // namespace

int stability(const QString &outDir)
{
	QDir().mkpath(outDir);
	QFile report(QDir(outDir).filePath("stability.txt"));
	report.open(QIODevice::WriteOnly | QIODevice::Truncate);
	int failures = 0;
	auto say = [&](const QString &s) {
		report.write(s.toUtf8() + "\n");
		report.flush();
	};
	auto check = [&](bool ok, const QString &what) {
		say(QString(ok ? "PASS  " : "FAIL  ") + what);
		failures += ok ? 0 : 1;
	};
	const QString scenesPath = qEnvironmentVariable("APPDATA") + "/RareStudio/scenes.json";
	auto savedScenes = [&] {
		QFile f(scenesPath);
		return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
	};

	MainWindow w;
	w.resize(1400, 860);
	w.show();
	wait(1200);
	check(QSettings().value("session/running").toBool(), "session marked running (unclean exit is detectable)");
	auto *table = w.findChild<QTableWidget *>();
	QPushButton *addDest = nullptr;
	for (QPushButton *b : w.findChildren<QPushButton *>())
		if (b->text().contains("Add destination"))
			addDest = b;
	check(table && addDest, "stream panel found");
	if (!table || !addDest)
		return 1;

	// RS_STAB_ONLY=1|2|4|8 (bitmask) runs just some parts, for bisecting a failure.
	const int parts = qEnvironmentVariableIsSet("RS_STAB_ONLY") ? qEnvironmentVariableIntValue("RS_STAB_ONLY") : 15;

	// 1. The crash you hit: Add destination (used to null-deref inside the autosave of a half-built row).
	const int rowsBefore = table->rowCount();
	if (parts & 1) {
	for (const auto &[platform, server, key] :
	     {std::tuple<QString, QString, QString>{"Twitch", "", "live_test_twitch"},
	      {"Kick", "", "sk_test_kick"},
	      {"TikTok", "rtmp://push.example.invalid/game", "tiktok_test_key"}}) {
		onNextModal([platform, server, key, outDir](QDialog *d) {
			if (auto *combo = d->findChild<QComboBox *>())
				combo->setCurrentIndex(combo->findText(platform));
			if (!server.isEmpty())
				byPlaceholder(d, "rtmp://...")->setText(server);
			byPlaceholder(d, "Paste your stream key")->setText(key + "  "); // stray spaces get trimmed
			d->grab().save(QDir(outDir).filePath("dest_" + platform + ".png"));
			clickOk(d);
		});
		addDest->click(); // returns once the dialog closed
		wait(100);
	}
	check(table->rowCount() == rowsBefore + 3, QString("3 destinations added (rows %1 -> %2)").arg(rowsBefore).arg(table->rowCount()));
	{
		QSettings s;
		const int n = s.beginReadArray("destinations");
		bool keysOk = n == table->rowCount();
		for (int i = 0; i < n; ++i) {
			s.setArrayIndex(i);
			if (s.value("name").toString() == "TikTok")
				keysOk &= Secrets::unprotect(s.value("key").toString()) == "tiktok_test_key" &&
					  s.value("server").toString() == "rtmp://push.example.invalid/game";
		}
		s.endArray();
		check(keysOk, "destinations + trimmed keys saved immediately (no exit needed)");
	}
	}

	// 2. Media source: Add source -> Media Source, untick Local File, the URL/stream field must appear.
	if (parts & 2) {
	QAction *addSource = nullptr;
	for (QAction *a : w.findChildren<QAction *>())
		if (a->text() == "+ Source")
			addSource = a;
	bool urlFieldAppeared = false, fileFieldHidden = false;
	const QString url = "rtmp://127.0.0.1:1/live/test";
	onNextModal([&](QDialog *picker) {
		auto *types = picker->findChild<QListWidget *>("typeList");
		for (int i = 0; types && i < types->count(); ++i)
			if (types->item(i)->data(Qt::UserRole).toByteArray() == "ffmpeg_source")
				types->setCurrentRow(i);
		onNextModal(
			[&](QDialog *props) {
				const bool inputBefore = fieldFor(props, "Input") != nullptr;
				for (QCheckBox *c : props->findChildren<QCheckBox *>())
					if (c->text() == "Local File" && c->isChecked())
						c->setChecked(false);
				QTimer::singleShot(400, props, [&, props, inputBefore] {
					auto *input = qobject_cast<QLineEdit *>(fieldFor(props, "Input"));
					urlFieldAppeared = !inputBefore && input;
					props->grab().save(QDir(outDir).filePath("media_properties.png"));
					fileFieldHidden = fieldFor(props, "Local File") == nullptr && !fieldFor(props, "Local file");
					if (input) {
						input->setText(url);
						emit input->textEdited(url);
						emit input->editingFinished();
					}
					QTimer::singleShot(200, props, [props] { clickOk(props); });
				});
			},
			picker);
		clickOk(picker);
	});
	if (addSource)
		addSource->trigger();
	check(urlFieldAppeared, "Media Source: unticking Local File shows the Input (URL / stream) field");
	say(QString("      (file picker hidden afterwards: %1)").arg(fileFieldHidden ? "yes" : "no"));
	obs_source_t *media = obs_get_source_by_name("Media");
	check(media != nullptr, "media source exists under a unique name");
	if (media) {
		obs_data_t *st = obs_source_get_settings(media);
		check(!obs_data_get_bool(st, "is_local_file") && QString::fromUtf8(obs_data_get_string(st, "input")) == url,
		      "media source settings hold the stream URL");
		obs_data_release(st);
		obs_source_release(media);
	}

	// 3. Autosave without exiting: the new source is on disk within a moment.
	wait(1500);
	const QString saved = savedScenes();
	check(saved.contains("\"Media\"") && saved.contains(url), "scenes.json has the new source + URL without closing");
	check(saved.contains("scene_order"), "scene order saved");
	}

	// 4. Every source type's and filter type's properties dialog builds and closes (groups, buttons,
	//    lists, sliders...). Sources are created outside any scene, so nothing is captured or opened.
	if (parts & 4) {
	const char *types[] = {"monitor_capture", "window_capture", "game_capture", "ffmpeg_source", "image_source",
			       "slideshow", "text_gdiplus", "color_source_v3", "wasapi_process_output_capture"};
	for (const char *t : types) {
		obs_source_t *src = obs_source_create(t, QString("selftest %1").arg(t).toUtf8().constData(), nullptr, nullptr);
		if (!src) {
			say(QString("skip  %1 (not available)").arg(t));
			continue;
		}
		{
			PropertiesDialog d(src, &w);
			d.show();
			wait(150);
			d.reject();
		}
		obs_source_remove(src);
		obs_source_release(src);
		say(QString("ok    properties: %1").arg(t));
	}
	obs_source_t *host = obs_source_create("ffmpeg_source", "selftest filter host", nullptr, nullptr);
	const char *fid = nullptr;
	for (size_t i = 0; host && obs_enum_filter_types(i, &fid); ++i) {
		const QString id = QString::fromUtf8(fid);
		obs_source_t *f = obs_source_create(fid, ("selftest " + id).toUtf8().constData(), nullptr, nullptr);
		if (!f)
			continue;
		obs_source_filter_add(host, f);
		{
			PropertiesDialog d(f, &w);
			d.show();
			wait(80);
			d.reject();
		}
		obs_source_filter_remove(host, f);
		obs_source_release(f);
		say("ok    filter properties: " + id);
	}
	if (host) {
		obs_source_remove(host);
		obs_source_release(host);
	}
	check(true, "all property dialogs built and closed without crashing");
	}

	// 5. Edit menu (copy / paste as reference / duplicate), output screenshot, and pausing a recording.
	if (parts & 8) {
	auto action = [&](const QString &text) -> QAction * {
		for (QAction *a : w.findChildren<QAction *>())
			if (a->text().remove('&') == text)
				return a;
		return nullptr;
	};
	ObsCore &core = ObsCore::instance();
	obs_scene_t *home = core.currentScene();
	if (obs_source_t *c = core.addSource("color_source_v3", "selftest color"))
		obs_source_release(c);
	// The source list only re-reads the scene when the scene changes: bounce the scene selection.
	auto *scenes = w.findChild<QDockWidget *>("scenes")->findChild<QListWidget *>();
	auto *sources = w.findChild<QDockWidget *>("sources")->findChild<QListWidget *>();
	const int homeRow = scenes->currentRow();
	scenes->setCurrentRow(-1);
	scenes->setCurrentRow(homeRow);
	const auto found = sources->findItems("selftest color", Qt::MatchExactly);
	check(!found.isEmpty(), "edit: new source listed");
	if (!found.isEmpty())
		sources->setCurrentItem(found.first());
	wait(100);

	QAction *dup = action("Duplicate source"), *copy = action("Copy source"), *paste = action("Paste source");
	check(dup && copy && paste, "edit: Edit menu has Copy / Paste / Duplicate");
	if (dup && copy && paste) {
		dup->trigger();
		check(obs_scene_find_source(home, "selftest color 2") != nullptr, "edit: Duplicate adds an independent copy");
		sources->setCurrentItem(sources->findItems("selftest color", Qt::MatchExactly).value(0));
		copy->trigger();
		obs_scene_t *other = core.createScene("selftest other scene");
		core.setCurrentScene(other);
		paste->trigger();
		obs_sceneitem_t *ref = obs_scene_find_source(other, "selftest color");
		obs_sceneitem_t *orig = obs_scene_find_source(home, "selftest color");
		check(ref && orig && obs_sceneitem_get_source(ref) == obs_sceneitem_get_source(orig),
		      "edit: Paste into another scene adds the same source (reference)");
		paste->trigger();
		check(obs_scene_find_source(other, "selftest color 3") != nullptr,
		      "edit: Paste where the source already is adds a copy instead");
		core.setCurrentScene(home);
		core.removeScene(other);
		scenes->setCurrentRow(-1);
		scenes->setCurrentRow(homeRow);
	}

	// Screenshot: the recordings folder gets a PNG at the output size showing the (grey) colour source.
	const QString recDir = QDir(outDir).filePath("recordings");
	QDir(recDir).removeRecursively();
	QSettings().setValue("record/dir", recDir);
	if (QAction *shot = action("Take screenshot")) {
		shot->trigger();
		QStringList pngs;
		for (int i = 0; i < 50 && pngs.isEmpty(); ++i) {
			wait(100);
			pngs = QDir(recDir).entryList({"Screenshot_*.png"});
		}
		wait(300); // written on a worker thread: let it finish
		obs_video_info ovi = {};
		obs_get_video_info(&ovi);
		const QImage img(pngs.isEmpty() ? QString() : QDir(recDir).filePath(pngs.first()));
		const QColor mid = img.isNull() ? QColor() : img.pixelColor(img.width() / 2, img.height() / 2);
		check(!img.isNull() && img.width() == int(ovi.output_width) && img.height() == int(ovi.output_height) &&
			      mid.lightness() > 100,
		      QString("screenshot: %1x%2 PNG of the output (centre %3)").arg(img.width()).arg(img.height()).arg(mid.name()));
	} else {
		check(false, "screenshot: File > Take screenshot exists");
	}

	// Pause: only shown while recording; pausing freezes the REC clock; the file is written on stop.
	QPushButton *rec = nullptr, *pause = nullptr;
	for (QPushButton *b : w.findChildren<QPushButton *>()) {
		if (b->text() == "● Rec")
			rec = b;
		if (b->text() == "❚❚")
			pause = b;
	}
	check(rec && pause && !pause->isVisible(), "pause: hidden until you record");
	if (rec && pause) {
		rec->click();
		wait(1500);
		check(pause->isVisible() && pause->isEnabled(), "pause: offered while recording alone");
		pause->click();
		auto *chip = w.findChild<QWidget *>("recChip");
		auto *chipLabel = chip ? chip->findChild<QLabel *>() : nullptr;
		wait(1200);
		const QString pausedText = chipLabel ? chipLabel->text() : QString();
		wait(1200);
		check(pause->isChecked() && pausedText.startsWith("PAUSED") && chipLabel && chipLabel->text() == pausedText,
		      "pause: REC badge says PAUSED and its clock stops (" + pausedText + ")");
		pause->click();
		wait(1200);
		check(!pause->isChecked() && chipLabel && chipLabel->text().startsWith("REC"), "pause: resume continues recording");
		rec->click();
		check(!rec->isEnabled(), "record: button waits (Stopping...) while the file is finished");
		for (int i = 0; i < 100 && !rec->isEnabled(); ++i)
			wait(100);
		const QStringList files = QDir(recDir).entryList({"*.mkv", "*.mp4", "*.mov", "*.flv"});
		const qint64 size = files.isEmpty() ? 0 : QFileInfo(QDir(recDir).filePath(files.first())).size();
		check(size > 10000, QString("pause: recording with a pause in it saved (%1 KB)").arg(size / 1024));
		check(!pause->isVisible() && rec->text() == "● Rec", "pause: hidden again after stopping");
	}
	}

	w.grab().save(QDir(outDir).filePath("stability_main.png"));
	w.close();
	check(!QSettings().value("session/running").toBool(), "clean exit clears the running flag");
	say(failures ? QString("%1 FAILURE(S)").arg(failures) : QString("ALL PASSED"));
	return failures ? 1 : 0;
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
