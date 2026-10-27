#include "MainWindow.h"

#include "Accounts.h"
#include "ClipsPanel.h"
#include "FiltersDialog.h"
#include "HotkeyManager.h"
#include "MixerPanel.h"
#include "ObsCore.h"
#include "PreviewWidget.h"
#include "PropertiesDialog.h"
#include "SettingsDialog.h"
#include "Secrets.h"
#include "TransformDialog.h"
#include "TransitionManager.h"
#include "TransitionsDialog.h"
#include "Updater.h"
#include "WebDock.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QMenu>
#include <QTimer>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QLineEdit>
#include <QSpinBox>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QToolBar>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>

#include <QDateTime>
#include <QIcon>

#include <windows.h>
#include <psapi.h>

namespace {

// Bump when docks are added/removed so a stale saved layout does not hide or scramble them.
constexpr int kLayoutVersion = 4;

QString scenesFile()
{
	const QString dir = qEnvironmentVariable("APPDATA") + "/RareStudio";
	QDir().mkpath(dir);
	return dir + "/scenes.json";
}

struct Preset {
	const char *name;
	const char *server;
};
const Preset kPresets[] = {
	{"Twitch", "rtmp://live.twitch.tv/app"},
	{"YouTube", "rtmp://a.rtmp.youtube.com/live2"},
	{"Kick", "rtmps://fa723fc1b171.global-contribute.live-video.net/app"},
	{"Facebook", "rtmps://live-api-s.facebook.com:443/rtmp/"},
	{"Custom RTMP", ""},
};

const struct {
	const char *id;
} kSourceTypes[] = {
	{"monitor_capture"},
	{"window_capture"},
	{"game_capture"},
	{"dshow_input"},
	{"ffmpeg_source"}, // media file / URL
	{"image_source"},
	{"slideshow"},
	{"text_gdiplus"},
	{"color_source_v3"},
	{"wasapi_input_capture"},
	{"wasapi_output_capture"},
	{"wasapi_process_output_capture"}, // audio from a single app
};

} // namespace

MainWindow::MainWindow()
{
	setWindowTitle("RareStudio");
	setDockNestingEnabled(true);

	// Centre: the editor preview, plus (in Studio Mode) a read-only view of what is on air.
	m_preview = new PreviewWidget;
	m_program = new PreviewWidget;
	m_program->setProgramOnly(true);
	m_programPane = new QWidget;
	{
		auto *pl = new QVBoxLayout(m_programPane);
		pl->setContentsMargins(0, 0, 0, 0);
		pl->setSpacing(4);
		auto *head = new QHBoxLayout;
		auto *label = new QLabel("PROGRAM (live)");
		label->setStyleSheet("color: #ef4444; font-weight: 700;");
		auto *go = new QPushButton("Transition  \u25B6");
		go->setObjectName("goLive");
		go->setToolTip("Send the scene on the left on air using the selected transition");
		head->addWidget(label);
		head->addStretch();
		head->addWidget(go);
		pl->addLayout(head);
		pl->addWidget(m_program, 1);
		connect(go, &QPushButton::clicked, this, [] { ObsCore::instance().commitToProgram(); });
	}
	m_programPane->hide();
	m_split = new QSplitter(Qt::Horizontal);
	m_split->addWidget(m_preview);
	m_split->addWidget(m_programPane);
	m_split->setChildrenCollapsible(false);
	setCentralWidget(m_split);

	m_statusLabel = new QLabel("Offline");
	statusBar()->addPermanentWidget(m_statusLabel);

	ObsCore &core = ObsCore::instance();

	// Restore saved scenes, or start with one empty scene.
	if (obs_data_t *saved = obs_data_create_from_json_file(scenesFile().toUtf8().constData())) {
		if (obs_data_array_t *arr = obs_data_get_array(saved, "sources")) {
			obs_load_sources(arr, nullptr, nullptr);
			obs_data_array_release(arr);
		}
		obs_data_release(saved);
	}

	// Menus first so their order is File, View.
	QMenu *file = menuBar()->addMenu("&File");
	m_viewMenu = menuBar()->addMenu("&View");

	buildToolbar();
	buildScenesDock();
	buildSourcesDock();
	buildMixerDock();
	buildStreamDock();
	buildClipsDock();
	buildWebDocks();

	if (m_scenes->count() == 0)
		addScene();
	m_scenes->setCurrentRow(0);

	// Every dock can be toggled from View; Reset layout recovers from a messy arrangement.
	m_defaultState = saveState(kLayoutVersion);
	for (QDockWidget *d : findChildren<QDockWidget *>())
		m_viewMenu->addAction(d->toggleViewAction());
	m_viewMenu->addSeparator();
	m_viewMenu->addAction("Reset layout", this, [this] {
		restoreState(m_defaultState, kLayoutVersion);
		for (QDockWidget *d : findChildren<QDockWidget *>())
			d->setVisible(d->objectName() != "dashboards");
	});

	// Preview <-> source list stay in sync; right-click and Delete work on the preview itself.
	connect(m_preview, &PreviewWidget::selectionChanged, this, &MainWindow::selectInList);
	connect(m_preview, &PreviewWidget::contextRequested, this, &MainWindow::showItemMenu);
	connect(m_preview, &PreviewWidget::deleteRequested, this, &MainWindow::removeSelectedSource);

	file->addAction("&Settings...", this, &MainWindow::openSettings);
	file->addAction("Check for &updates", this, &MainWindow::checkForUpdates);
	file->addSeparator();
	file->addAction("E&xit", this, &QWidget::close);

	// Recording / clips / hotkeys
	connect(&m_rec, &Recorder::recordingStopped, this, [this](const QString &path, const QString &err) {
		m_recBtn->setText("● Rec");
		m_recBtn->setChecked(false);
		statusBar()->showMessage(err.isEmpty() ? QString("Recording saved: %1").arg(QDir::toNativeSeparators(path))
						       : QString("Recording stopped: %1").arg(err), 10000);
	});
	connect(&m_rec, &Recorder::clipSaved, this, [this](const QString &path) {
		statusBar()->showMessage(QString("Clip saved: %1").arg(QDir::toNativeSeparators(path)), 10000);
		m_clips->add("Clip saved: " + QFileInfo(path).fileName(), path);
	});
	connect(&m_rec, &Recorder::replayStopped, this, [this] { setReplayUi(m_vertical.active()); });
	connect(&m_vertical, &VerticalClip::clipSaved, this, &MainWindow::onShortSaved);

	// The vertical Shorts view follows whichever scene is on air.
	ObsCore::instance().setProgramHook([this](obs_scene_t *scene) { m_vertical.programChanged(scene); });
	m_vertical.programChanged(ObsCore::instance().programScene());

	// Signing in (or reconnecting) fills your stream keys without any copy/paste.
	file->addAction("Refresh stream &keys", this, [this] { refreshStreamKeys(true); });
	connect(&Accounts::instance(), &Accounts::changed, this, [this] {
		if (QSettings().value("accounts/autokeys", true).toBool())
			refreshStreamKeys(true);
	});
	QTimer::singleShot(4000, this, [this] {
		if (QSettings().value("accounts/autokeys", true).toBool())
			refreshStreamKeys(false);
	});

	m_hotkeys = new HotkeyManager(this);
	connect(m_hotkeys, &HotkeyManager::triggered, this, &MainWindow::onHotkey);
	m_hotkeys->reload();

	m_cpu = os_cpu_usage_info_start();
	connect(&m_statsTimer, &QTimer::timeout, this, &MainWindow::updateStats);
	m_statsTimer.start(1000);

	connect(&m_stream, &MultiStream::destinationState, this, [this](const QString &name, const QString &state) {
		for (int r = 0; r < m_dests->rowCount(); ++r)
			if (m_dests->item(r, 1)->text() == name)
				m_dests->item(r, 4)->setText(state);
	});

	// Updates: quietly check shortly after launch, applies on next start.
	m_updater = new Updater(this);
	connect(m_updater, &Updater::updateReady, this, [this](const QString &v) {
		statusBar()->showMessage(QString("RareStudio %1 downloaded. Restart to apply.").arg(v));
	});
	connect(m_updater, &Updater::status, this, [this](const QString &msg) { statusBar()->showMessage(msg, 8000); });
	m_updater->cleanOldVersions();
	if (QSettings().value("update/auto", true).toBool())
		QTimer::singleShot(15000, this, [this] { m_updater->checkNow(false); });

	if (!restoreGeometry(QSettings().value("ui/geometry").toByteArray()))
		resize(1360, 820);
	restoreState(QSettings().value("ui/state").toByteArray(), kLayoutVersion); // older layouts are ignored
}

MainWindow::~MainWindow()
{
	if (m_cpu)
		os_cpu_usage_info_destroy(m_cpu);
}

// ---- Toolbar & stats --------------------------------------------------------------------------

void MainWindow::buildToolbar()
{
	auto *bar = addToolBar("Main");
	bar->setObjectName("toolbar");
	bar->setMovable(false);
	bar->setIconSize(QSize(22, 22));

	auto *logo = new QLabel;
	logo->setPixmap(QIcon(":/logo").pixmap(22, 22));
	logo->setContentsMargins(6, 0, 6, 0);
	bar->addWidget(logo);
	bar->addAction("+ Scene", this, &MainWindow::addScene);
	bar->addAction("+ Source", this, &MainWindow::addSourceDialog);
	m_studioBtn = new QPushButton("Studio Mode");
	m_studioBtn->setCheckable(true);
	m_studioBtn->setToolTip("Edit one scene while another is live, then press Transition to send it on air");
	bar->addWidget(m_studioBtn);
	connect(m_studioBtn, &QPushButton::toggled, this, &MainWindow::setStudio);
	bar->addSeparator();

	// Scene transition: how the picture changes when you switch scenes. The list comes from the
	// transition manager (built-ins, RareStudio's own, and any stingers you add).
	auto &tm = TransitionManager::instance();
	auto *trans = new QComboBox;
	trans->setMinimumWidth(190);
	trans->setToolTip("Scene transition");
	auto *ms = new QSpinBox;
	ms->setRange(0, 5000);
	ms->setSingleStep(50);
	ms->setSuffix(" ms");
	ms->setToolTip("Transition length");
	auto *cfg = new QPushButton("\u2699");
	cfg->setFixedWidth(34);
	cfg->setToolTip("Transitions & stingers: tune, add, set per-scene overrides");
	bar->addWidget(trans);
	bar->addWidget(ms);
	bar->addWidget(cfg);

	// Re-reads the manager into the widgets without triggering their change handlers.
	auto syncTransitionUi = [trans, ms] {
		auto &t = TransitionManager::instance();
		QSignalBlocker b1(trans), b2(ms);
		trans->clear();
		for (const QString &n : t.names())
			trans->addItem(n, n);
		trans->setCurrentIndex(std::max(0, trans->findData(t.current())));
		if (const TransitionDef *d = t.find(t.current()))
			ms->setValue(d->ms);
	};
	syncTransitionUi();
	connect(trans, &QComboBox::currentIndexChanged, this, [trans, ms, syncTransitionUi] {
		TransitionManager::instance().setCurrent(trans->currentData().toString());
		syncTransitionUi();
	});
	connect(ms, &QSpinBox::valueChanged, this, [trans](int v) {
		TransitionManager::instance().setMs(trans->currentData().toString(), v);
	});
	connect(cfg, &QPushButton::clicked, this, [this, syncTransitionUi] {
		TransitionsDialog dlg(this);
		connect(&dlg, &TransitionsDialog::changed, this, syncTransitionUi);
		dlg.exec();
		syncTransitionUi();
	});	bar->addSeparator();

	// Recording and instant replay ("clip") controls.
	m_recBtn = new QPushButton("● Rec");
	m_recBtn->setCheckable(true);
	m_recBtn->setToolTip("Record to a file");
	m_replayBtn = new QPushButton("Replay");
	m_replayBtn->setCheckable(true);
	m_replayBtn->setToolTip("Keep the last seconds in memory so you can save a clip after something happens");
	m_clipBtn = new QPushButton("Clip!");
	m_clipBtn->setEnabled(false);
	m_clipBtn->setToolTip("Save the last seconds as a clip");
	for (auto *b : {m_recBtn, m_replayBtn, m_clipBtn})
		bar->addWidget(b);
	connect(m_recBtn, &QPushButton::clicked, this, &MainWindow::toggleRecord);
	connect(m_replayBtn, &QPushButton::clicked, this, &MainWindow::toggleReplay);
	connect(m_clipBtn, &QPushButton::clicked, this, &MainWindow::saveClip);

	auto *spacer = new QWidget;
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	bar->addWidget(spacer);

	m_stats = new QLabel;
	m_stats->setObjectName("stats");
	bar->addWidget(m_stats);

	m_goLive = new QPushButton("Go Live");
	m_goLive->setObjectName("goLive");
	m_goLive->setCursor(Qt::PointingHandCursor);
	bar->addWidget(m_goLive);
	connect(m_goLive, &QPushButton::clicked, this, &MainWindow::toggleStream);
}

void MainWindow::updateStats()
{
	// RAM = private bytes of this process (the browser docks run in their own process, not counted).
	PROCESS_MEMORY_COUNTERS_EX pmc{};
	GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc));
	const double ramMb = pmc.PrivateUsage / (1024.0 * 1024.0);
	const double cpu = m_cpu ? os_cpu_usage_info_query(m_cpu) : 0.0;

	QString text = QString("CPU %1%   RAM %2 MB   %3 fps")
			       .arg(cpu, 0, 'f', 1)
			       .arg(ramMb, 0, 'f', 0)
			       .arg(obs_get_active_fps(), 0, 'f', 1);

	if (m_rec.recording()) {
		const qint64 t = m_rec.recordingSeconds();
		text = QString("● REC %1:%2:%3   ")
			       .arg(t / 3600, 2, 10, QChar('0'))
			       .arg(t / 60 % 60, 2, 10, QChar('0'))
			       .arg(t % 60, 2, 10, QChar('0')) + text;
	}
	if (m_stream.live()) {
		const MultiStream::Stats s = m_stream.stats();
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		if (m_lastBytesMs && now > m_lastBytesMs && s.bytes >= m_lastBytes)
			m_kbps = (s.bytes - m_lastBytes) * 8.0 / (now - m_lastBytesMs); // bytes*8/ms = kbit/s
		m_lastBytes = s.bytes;
		m_lastBytesMs = now;
		text += QString("   ↑ %1 kbps   dropped %2/%3").arg(m_kbps, 0, 'f', 0).arg(s.dropped).arg(s.total);
	} else {
		m_lastBytesMs = 0;
	}
	m_stats->setText(text);
}

void MainWindow::openSettings()
{
	if (SettingsDialog(this).exec() == QDialog::Accepted)
		m_hotkeys->reload();
}

// ---- Recording, clips, hotkeys ----------------------------------------------------------------

void MainWindow::toggleRecord()
{
	if (m_rec.recording()) {
		m_rec.stopRecording();
		return;
	}
	QString err;
	if (!m_rec.startRecording(&err)) {
		m_recBtn->setChecked(false);
		QMessageBox::warning(this, "RareStudio", "Could not start recording:\n" + err);
		return;
	}
	m_recBtn->setChecked(true);
	m_recBtn->setText("■ Stop");
}

void MainWindow::setReplayUi(bool armed)
{
	QSignalBlocker b(m_replayBtn);
	m_replayBtn->setChecked(armed);
	m_clipBtn->setEnabled(armed);
}

// Arms the replay buffers that Settings > Clips asks for: the full-size local one, the vertical
// 9:16 Shorts one, or both.
void MainWindow::toggleReplay()
{
	if (m_rec.replayActive() || m_vertical.active()) {
		m_rec.stopReplay();
		m_vertical.stop();
		setReplayUi(m_rec.replayActive());
		return;
	}

	QSettings s;
	const bool wantShort = s.value("clips/short", false).toBool();
	const bool wantLocal = s.value("clips/local", true).toBool() || !wantShort; // never arm nothing
	QString err;
	if (wantLocal && !m_rec.startReplay(&err)) {
		setReplayUi(false);
		QMessageBox::warning(this, "RareStudio", "Could not start the replay buffer:\n" + err);
		return;
	}
	if (wantShort && !m_vertical.start(&err))
		QMessageBox::warning(this, "RareStudio", "Could not start the Shorts buffer:\n" + err);

	const bool armed = m_rec.replayActive() || m_vertical.active();
	setReplayUi(armed);
	if (armed)
		statusBar()->showMessage("Replay armed: press Clip! (or its hotkey) to save the last moments.", 8000);
}

void MainWindow::saveClip()
{
	QSettings s;
	bool any = false;
	if (m_rec.replayActive())
		any |= m_rec.saveClip();
	if (m_vertical.active())
		any |= m_vertical.saveClip();
	if (s.value("clips/twitch", false).toBool()) {
		createTwitchClip();
		any = true;
	}
	if (!any)
		statusBar()->showMessage("Arm the replay buffer first (the Replay button).", 5000);
}

// ---- Twitch clips, Shorts, stream keys --------------------------------------------------------

void MainWindow::createTwitchClip()
{
	auto &acc = Accounts::instance();
	const int id = m_clips->add("Twitch clip: requesting...");
	if (!acc.twitchConnected()) {
		m_clips->update(id, "Twitch clip skipped: connect Twitch in Settings > Accounts");
		return;
	}
	const int secs = QSettings().value("clips/twitch_seconds", 30).toInt();
	acc.twitchClip(secs, [this, id](const QString &url, const QString &err) {
		m_clips->update(id, err.isEmpty() ? "Twitch clip created: " + url : err, url);
	});
}

void MainWindow::onShortSaved(const QString &path)
{
	const int id = m_clips->add("Short saved: " + QFileInfo(path).fileName(), path);
	statusBar()->showMessage("Short saved: " + QDir::toNativeSeparators(path), 10000);
	if (!QSettings().value("clips/upload", false).toBool())
		return;
	if (!Accounts::instance().googleConnected()) {
		m_clips->update(id, "Short saved (not uploaded: connect Google in Settings > Accounts)", path);
		return;
	}
	uploadShort(id, path);
}

void MainWindow::uploadShort(int panelId, const QString &path)
{
	QSettings s;
	QString title = s.value("clips/title", "Clip {date} #Shorts").toString();
	const QDateTime now = QDateTime::currentDateTime();
	title.replace("{date}", now.toString("yyyy-MM-dd")).replace("{time}", now.toString("HH:mm"));
	if (!title.contains("#Shorts", Qt::CaseInsensitive))
		title += " #Shorts";

	UploadJob *job = Accounts::instance().uploadShort(path, title, "Clipped live with RareStudio #Shorts",
							  s.value("clips/privacy", "private").toString());
	m_clips->update(panelId, "Uploading Short... 0%", path);
	connect(job, &UploadJob::progress, this, [this, panelId, path](qint64 sent, qint64 total) {
		m_clips->update(panelId, QString("Uploading Short... %1%").arg(total > 0 ? int(sent * 100 / total) : 0), path);
	});
	connect(job, &UploadJob::finished, this, [this, panelId, path, job](const QString &url, const QString &err) {
		m_clips->update(panelId, err.isEmpty() ? "Short uploaded: " + url : "Upload failed: " + err, err.isEmpty() ? url : path);
		job->deleteLater();
	});
}

void MainWindow::upsertDestination(const QString &name, const QString &server, const QString &key)
{
	for (int r = 0; r < m_dests->rowCount(); ++r) {
		if (m_dests->item(r, 1)->text() != name)
			continue;
		m_dests->item(r, 2)->setText(server);
		m_dests->item(r, 3)->setData(Qt::UserRole, key);
		m_dests->item(r, 3)->setText(QString(key.size(), QChar(0x2022)));
		saveDestinations();
		return;
	}
	addDestination(name, server, key, true);
	saveDestinations();
}

// Pulls your stream keys from Twitch / YouTube with the accounts you connected.
void MainWindow::refreshStreamKeys(bool announce)
{
	auto &acc = Accounts::instance();
	auto say = [this, announce](const QString &msg) {
		if (announce)
			statusBar()->showMessage(msg, 8000);
	};
	if (acc.twitchConnected())
		acc.twitchStreamKey([this, say](const QString &key, const QString &err) {
			if (!err.isEmpty())
				return say("Twitch key: " + err);
			upsertDestination("Twitch (auto)", "rtmp://live.twitch.tv/app", key);
			say("Twitch stream key updated.");
		});
	if (acc.googleConnected())
		acc.youtubeIngest([this, say](const QString &server, const QString &key, const QString &err) {
			if (!err.isEmpty())
				return say("YouTube key: " + err);
			upsertDestination("YouTube (auto)", server, key);
			say("YouTube stream key updated.");
		});
}

void MainWindow::onHotkey(int action)
{
	switch (action) {
	case HotkeyManager::ToggleStream: toggleStream(); break;
	case HotkeyManager::ToggleRecord: toggleRecord(); break;
	case HotkeyManager::ToggleReplay: toggleReplay(); break;
	case HotkeyManager::SaveClip: saveClip(); break;
	case HotkeyManager::MuteMic:
		if (obs_source_t *mic = ObsCore::instance().micAudio())
			obs_source_set_muted(mic, !obs_source_muted(mic));
		break;
	case HotkeyManager::NextScene:
	case HotkeyManager::PrevScene: {
		const int n = m_scenes->count();
		if (n > 0) {
			const int step = action == HotkeyManager::NextScene ? 1 : -1;
			m_scenes->setCurrentRow((m_scenes->currentRow() + step + n) % n);
		}
		break;
	}
	default:
		if (action > HotkeyManager::SceneBase && action - HotkeyManager::SceneBase <= m_scenes->count())
			m_scenes->setCurrentRow(action - HotkeyManager::SceneBase - 1);
	}
}

void MainWindow::closeEvent(QCloseEvent *e)
{
	if (m_stream.live() &&
	    QMessageBox::question(this, "RareStudio", "You are live. Stop streaming and exit?") != QMessageBox::Yes) {
		e->ignore();
		return;
	}
	m_stream.stop();
	m_rec.shutdown();
	m_vertical.stop();
	m_dests->setCurrentItem(nullptr); // commits a key that is still being typed
	saveDestinations();

	obs_data_t *out = obs_data_create();
	obs_data_array_t *arr = obs_save_sources();
	obs_data_set_array(out, "sources", arr);
	obs_data_save_json_safe(out, scenesFile().toUtf8().constData(), "tmp", "bak");
	obs_data_array_release(arr);
	obs_data_release(out);

	QSettings().setValue("ui/geometry", saveGeometry());
	QSettings().setValue("ui/state", saveState(kLayoutVersion));
	QMainWindow::closeEvent(e);
}

void MainWindow::changeEvent(QEvent *e)
{
	// A minimized window has nothing to show, so stop drawing the preview.
	if (e->type() == QEvent::WindowStateChange && m_preview)
	{
		m_preview->setRendering(!isMinimized());
		m_program->setRendering(!isMinimized());
	}
	QMainWindow::changeEvent(e);
}

// ---- Scenes ---------------------------------------------------------------------------------

void MainWindow::buildScenesDock()
{
	auto *dock = new QDockWidget("Scenes", this);
	dock->setObjectName("scenes");
	auto *w = new QWidget;
	auto *lay = new QVBoxLayout(w);
	m_scenes = new QListWidget;
	lay->addWidget(m_scenes);
	auto *add = new QPushButton("Add Scene");
	lay->addWidget(add);
	dock->setWidget(w);
	addDockWidget(Qt::LeftDockWidgetArea, dock);

	obs_enum_scenes(
		[](void *p, obs_source_t *src) {
			auto *list = static_cast<QListWidget *>(p);
			list->addItem(QString::fromUtf8(obs_source_get_name(src)));
			ObsCore::instance().holdScene(src);
			return true;
		},
		m_scenes);

	m_scenes->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_scenes, &QListWidget::customContextMenuRequested, this,
		[this](const QPoint &p) { showSceneMenu(m_scenes->viewport()->mapToGlobal(p)); });
	connect(add, &QPushButton::clicked, this, &MainWindow::addScene);
	connect(m_scenes, &QListWidget::currentTextChanged, this, [this](const QString &name) {
		if (name.isEmpty())
			return;
		obs_source_t *src = obs_get_source_by_name(name.toUtf8().constData());
		if (!src)
			return;
		m_preview->clearSelection(); // selected item belongs to the scene we're leaving
		ObsCore::instance().setCurrentScene(obs_scene_from_source(src));
		obs_source_release(src); // output channel 0 holds its own reference
		refreshSources();
	});
}

void MainWindow::addScene()
{
	bool ok = true;
	QString name = m_scenes->count() == 0 ? "Scene" : QInputDialog::getText(this, "Add Scene", "Name:", QLineEdit::Normal,
									       QString("Scene %1").arg(m_scenes->count() + 1), &ok);
	if (!ok || name.isEmpty())
		return;
	obs_scene_t *scene = ObsCore::instance().createScene(name);
	if (!scene)
		return;
	m_scenes->addItem(name);
	m_scenes->setCurrentRow(m_scenes->count() - 1);
}

// ---- Sources --------------------------------------------------------------------------------

void MainWindow::buildSourcesDock()
{
	auto *dock = new QDockWidget("Sources", this);
	dock->setObjectName("sources");
	auto *w = new QWidget;
	auto *lay = new QVBoxLayout(w);
	m_sources = new QListWidget;
	lay->addWidget(m_sources);
	auto *row = new QHBoxLayout;
	auto *add = new QPushButton("+");
	auto *props = new QPushButton("Properties");
	auto *xform = new QPushButton("Transform");
	auto *up = new QPushButton("▲");
	auto *down = new QPushButton("▼");
	auto *rem = new QPushButton("✕");
	add->setToolTip("Add source");
	up->setToolTip("Bring forward");
	down->setToolTip("Send backward");
	rem->setToolTip("Remove source");
	for (auto *b : {add, up, down, rem})
		b->setFixedWidth(34);
	for (auto *b : {add, props, xform, up, down, rem})
		row->addWidget(b);
	lay->addLayout(row);
	dock->setWidget(w);
	addDockWidget(Qt::LeftDockWidgetArea, dock);

	m_sources->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_sources, &QListWidget::customContextMenuRequested, this,
		[this](const QPoint &p) { showItemMenu(m_sources->viewport()->mapToGlobal(p)); });
	connect(m_sources, &QListWidget::currentTextChanged, this,
		[this](const QString &name) { m_preview->selectByName(name); });
	connect(add, &QPushButton::clicked, this, &MainWindow::addSourceDialog);
	connect(props, &QPushButton::clicked, this, &MainWindow::editSelectedSource);
	connect(xform, &QPushButton::clicked, this, &MainWindow::transformSelected);
	connect(up, &QPushButton::clicked, this, [this] { moveSelected(OBS_ORDER_MOVE_UP); });
	connect(down, &QPushButton::clicked, this, [this] { moveSelected(OBS_ORDER_MOVE_DOWN); });
	connect(m_sources, &QListWidget::itemDoubleClicked, this, &MainWindow::editSelectedSource);
	connect(rem, &QPushButton::clicked, this, &MainWindow::removeSelectedSource);
	connect(m_sources, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
		obs_scene_t *scene = ObsCore::instance().currentScene();
		if (!scene)
			return;
		if (obs_sceneitem_t *si = obs_scene_find_source(scene, it->text().toUtf8().constData()))
			obs_sceneitem_set_visible(si, it->checkState() == Qt::Checked);
	});
}

void MainWindow::refreshSources()
{
	QSignalBlocker block(m_sources);
	m_sources->clear();
	obs_scene_t *scene = ObsCore::instance().currentScene();
	if (!scene)
		return;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *p) {
			auto *list = static_cast<QListWidget *>(p);
			auto *li = new QListWidgetItem(QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(item))));
			li->setFlags(li->flags() | Qt::ItemIsUserCheckable);
			li->setCheckState(obs_sceneitem_visible(item) ? Qt::Checked : Qt::Unchecked);
			list->insertItem(0, li); // top of the list = front of the scene
			return true;
		},
		m_sources);
}

void MainWindow::addSourceDialog()
{
	QStringList labels;
	QList<const char *> ids;
	for (const auto &t : kSourceTypes) {
		if (const char *dn = obs_source_get_display_name(t.id)) {
			labels << QString::fromUtf8(dn);
			ids << t.id;
		}
	}
	bool ok = false;
	const QString label = QInputDialog::getItem(this, "Add Source", "Type:", labels, 0, false, &ok);
	if (!ok)
		return;
	const int idx = labels.indexOf(label);

	const QString name = QInputDialog::getText(this, "Add Source", "Name:", QLineEdit::Normal, label, &ok);
	if (!ok || name.isEmpty())
		return;

	if (obs_source_t *src = ObsCore::instance().addSource(ids[idx], name)) {
		refreshSources();
		selectInList(name);
		m_preview->selectByName(name);
		PropertiesDialog dlg(src, this);
		dlg.exec();
		obs_source_release(src);
	} else {
		QMessageBox::warning(this, "RareStudio", "Could not create that source.");
	}
}

void MainWindow::editSelectedSource()
{
	QListWidgetItem *it = m_sources->currentItem();
	if (!it)
		return;
	if (obs_source_t *src = obs_get_source_by_name(it->text().toUtf8().constData())) {
		PropertiesDialog dlg(src, this);
		dlg.exec();
		obs_source_release(src);
	}
}

void MainWindow::removeSelectedSource()
{
	QListWidgetItem *it = m_sources->currentItem();
	obs_scene_t *scene = ObsCore::instance().currentScene();
	if (!it || !scene)
		return;
	if (obs_sceneitem_t *si = obs_scene_find_source(scene, it->text().toUtf8().constData())) {
		m_preview->clearSelection(); // drop the preview's reference before the item goes away
		obs_sceneitem_remove(si);
	}
	refreshSources();
}

void MainWindow::selectInList(const QString &name)
{
	QSignalBlocker block(m_sources); // avoid echoing the selection back to the preview
	if (name.isEmpty()) {
		m_sources->clearSelection();
		m_sources->setCurrentRow(-1);
		return;
	}
	const auto found = m_sources->findItems(name, Qt::MatchExactly);
	if (!found.isEmpty())
		m_sources->setCurrentItem(found.first());
}

void MainWindow::filtersSelected()
{
	if (obs_sceneitem_t *item = m_preview->selectedItem()) {
		FiltersDialog(obs_sceneitem_get_source(item), this).exec();
		obs_sceneitem_release(item);
	}
}

void MainWindow::transformSelected()
{
	if (obs_sceneitem_t *item = m_preview->selectedItem()) {
		TransformDialog(item, this).exec();
		obs_sceneitem_release(item);
	}
}

void MainWindow::moveSelected(obs_order_movement dir)
{
	obs_sceneitem_t *item = m_preview->selectedItem();
	if (!item)
		return;
	const QString name = m_preview->selectedName();
	obs_sceneitem_set_order(item, dir);
	obs_sceneitem_release(item);
	refreshSources();
	selectInList(name);
}

void MainWindow::showItemMenu(const QPoint &globalPos)
{
	obs_sceneitem_t *item = m_preview->selectedItem();
	QMenu menu(this);
	if (!item) {
		menu.addAction("Add source...", this, &MainWindow::addSourceDialog);
		menu.exec(globalPos);
		return;
	}

	menu.addAction("Properties...", this, &MainWindow::editSelectedSource);
	menu.addAction("Filters...", this, &MainWindow::filtersSelected);
	menu.addAction("Rename...", this, &MainWindow::renameSource);
	menu.addAction(obs_sceneitem_locked(item) ? "Unlock" : "Lock position", this, &MainWindow::toggleLockSelected);
	menu.addAction("Transform...", this, &MainWindow::transformSelected);
	menu.addSeparator();
	menu.addAction("Fit to screen", [item] { ObsCore::fitToCanvas(item); });
	menu.addAction("Stretch to screen", [item] { ObsCore::stretchToCanvas(item); });
	menu.addAction("Center horizontally", [item] { ObsCore::centerOnCanvas(item, true, false); });
	menu.addAction("Center vertically", [item] { ObsCore::centerOnCanvas(item, false, true); });
	menu.addAction("Reset transform", [item] {
		obs_sceneitem_crop c = {};
		obs_sceneitem_set_crop(item, &c);
		ObsCore::fitToCanvas(item);
	});
	menu.addSeparator();
	menu.addAction("Bring to front", [this] { moveSelected(OBS_ORDER_MOVE_TOP); });
	menu.addAction("Bring forward", [this] { moveSelected(OBS_ORDER_MOVE_UP); });
	menu.addAction("Send backward", [this] { moveSelected(OBS_ORDER_MOVE_DOWN); });
	menu.addAction("Send to back", [this] { moveSelected(OBS_ORDER_MOVE_BOTTOM); });
	menu.addSeparator();
	menu.addAction("Remove", this, &MainWindow::removeSelectedSource);
	menu.exec(globalPos);
	obs_sceneitem_release(item);
}

// ---- Audio ----------------------------------------------------------------------------------

void MainWindow::buildMixerDock()
{
	auto *dock = new QDockWidget("Audio Mixer", this);
	dock->setObjectName("mixer");
	m_mixer = new MixerPanel;
	dock->setWidget(m_mixer);
	addDockWidget(Qt::BottomDockWidgetArea, dock);

	ObsCore &core = ObsCore::instance();
	core.setupDefaultAudio();
	m_mixer->addStrip("Desktop Audio", core.desktopAudio());
	m_mixer->addStrip("Mic", core.micAudio());
	connect(m_mixer, &MixerPanel::filtersRequested, this, [this](obs_source_t *src) { FiltersDialog(src, this).exec(); });
}

// ---- Streaming ------------------------------------------------------------------------------

void MainWindow::buildStreamDock()
{
	auto *dock = new QDockWidget("Stream", this);
	dock->setObjectName("stream");
	auto *w = new QWidget;
	auto *lay = new QVBoxLayout(w);

	m_dests = new QTableWidget(0, 5);
	m_dests->setHorizontalHeaderLabels({"On", "Platform", "Server", "Stream key", "Status"});
	m_dests->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_dests->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_dests->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_dests->setWordWrap(false);
	m_dests->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_dests->verticalHeader()->hide();
	lay->addWidget(m_dests);

	auto *row = new QHBoxLayout;
	auto *preset = new QComboBox;
	for (const Preset &p : kPresets)
		preset->addItem(p.name);
	auto *add = new QPushButton("Add destination");
	auto *rem = new QPushButton("Remove");
	row->addWidget(preset);
	row->addWidget(add);
	row->addWidget(rem);
	lay->addLayout(row);

	auto *settingsHint = new QLabel("Encoder, bitrate and recording options: File > Settings > Output");
	settingsHint->setWordWrap(true);
	settingsHint->setStyleSheet("color: #9a9ab0;");
	lay->addWidget(settingsHint);

	// Stream keys are typed masked and committed as soon as editing finishes.
	struct KeyDelegate : QStyledItemDelegate {
		using QStyledItemDelegate::QStyledItemDelegate;
		QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override
		{
			auto *e = new QLineEdit(parent);
			e->setEchoMode(QLineEdit::Password);
			return e;
		}
		void setEditorData(QWidget *, const QModelIndex &) const override {} // never show the stored key
		void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
		{
			const QString typed = static_cast<QLineEdit *>(editor)->text();
			if (typed.isEmpty())
				return; // keep the existing key
			model->setData(index, typed, Qt::UserRole);
			model->setData(index, QString(typed.size(), QChar(0x2022)), Qt::DisplayRole);
		}
	};
	m_dests->setItemDelegateForColumn(3, new KeyDelegate(m_dests));

	dock->setWidget(w);
	addDockWidget(Qt::RightDockWidgetArea, dock);

	connect(add, &QPushButton::clicked, this, [this, preset] {
		const Preset &p = kPresets[preset->currentIndex()];
		addDestination(p.name, p.server, "", true);
	});
	connect(rem, &QPushButton::clicked, this, [this] {
		if (m_dests->currentRow() >= 0)
			m_dests->removeRow(m_dests->currentRow());
	});
	loadDestinations();
}

void MainWindow::addDestination(const QString &name, const QString &server, const QString &key, bool enabled)
{
	const int r = m_dests->rowCount();
	m_dests->insertRow(r);
	auto *on = new QTableWidgetItem;
	on->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
	on->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
	m_dests->setItem(r, 0, on);
	m_dests->setItem(r, 1, new QTableWidgetItem(name));
	m_dests->setItem(r, 2, new QTableWidgetItem(server));
	auto *k = new QTableWidgetItem(QString(key.size(), QChar(0x2022)));
	k->setData(Qt::UserRole, key); // real key lives in item data; the cell only shows dots
	k->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
	m_dests->setItem(r, 3, k);
	auto *st = new QTableWidgetItem("offline");
	st->setFlags(Qt::ItemIsEnabled);
	m_dests->setItem(r, 4, st);
	if (key.isEmpty())
		m_dests->editItem(m_dests->item(r, 3));
}

QList<Destination> MainWindow::destinations() const
{
	QList<Destination> out;
	for (int r = 0; r < m_dests->rowCount(); ++r) {
		Destination d;
		d.enabled = m_dests->item(r, 0)->checkState() == Qt::Checked;
		d.name = m_dests->item(r, 1)->text();
		d.server = m_dests->item(r, 2)->text();
		d.key = m_dests->item(r, 3)->data(Qt::UserRole).toString();
		out.append(d);
	}
	return out;
}

void MainWindow::saveDestinations() const
{
	QSettings s;
	s.beginWriteArray("destinations");
	const auto list = destinations();
	for (int i = 0; i < list.size(); ++i) {
		s.setArrayIndex(i);
		s.setValue("name", list[i].name);
		s.setValue("server", list[i].server);
		s.setValue("key", Secrets::protect(list[i].key));
		s.setValue("enabled", list[i].enabled);
	}
	s.endArray();
}

void MainWindow::loadDestinations()
{
	QSettings s;
	const int n = s.beginReadArray("destinations");
	for (int i = 0; i < n; ++i) {
		s.setArrayIndex(i);
		addDestination(s.value("name").toString(), s.value("server").toString(),
			       Secrets::unprotect(s.value("key").toString()), s.value("enabled", true).toBool());
	}
	s.endArray();
}

void MainWindow::toggleStream()
{
	if (m_stream.live()) {
		m_stream.stop();
		m_goLive->setText("Go Live");
		m_statusLabel->setText("Offline");
		for (int r = 0; r < m_dests->rowCount(); ++r)
			m_dests->item(r, 4)->setText("offline");
		return;
	}

	QString err;
	if (!m_stream.start(destinations(), &err)) {
		QMessageBox::warning(this, "RareStudio", err);
		return;
	}
	m_goLive->setText("Stop Streaming");
	m_statusLabel->setText("Live (" + m_stream.encoderName() + ")");
}

// ---- Studio Mode, scene and source management ---------------------------------------------------

void MainWindow::setStudio(bool on)
{
	ObsCore &core = ObsCore::instance();
	core.setStudioMode(on);
	m_preview->setShowsEditScene(on);
	m_programPane->setVisible(on);
	if (on) {
		m_split->setSizes({1, 1});
		return;
	}
	// Back to a single view: the editor shows (and edits) whatever is on air.
	if (obs_scene_t *live = core.programScene()) {
		const auto found = m_scenes->findItems(QString::fromUtf8(obs_source_get_name(obs_scene_get_source(live))),
						       Qt::MatchExactly);
		if (!found.isEmpty())
			m_scenes->setCurrentItem(found.first());
	}
}

void MainWindow::showSceneMenu(const QPoint &globalPos)
{
	QMenu menu(this);
	menu.addAction("Add scene", this, &MainWindow::addScene);
	if (m_scenes->currentItem()) {
		menu.addSeparator();
		menu.addAction("Rename...", this, &MainWindow::renameScene);
		menu.addAction("Duplicate", this, &MainWindow::duplicateScene);
		QAction *rm = menu.addAction("Remove", this, &MainWindow::removeScene);
		rm->setEnabled(m_scenes->count() > 1);
	}
	menu.exec(globalPos);
}

static bool nameTaken(const QString &name)
{
	if (obs_source_t *s = obs_get_source_by_name(name.toUtf8().constData())) {
		obs_source_release(s);
		return true;
	}
	return false;
}

void MainWindow::renameScene()
{
	QListWidgetItem *it = m_scenes->currentItem();
	if (!it)
		return;
	bool ok = false;
	const QString name = QInputDialog::getText(this, "Rename scene", "Name:", QLineEdit::Normal, it->text(), &ok);
	if (!ok || name.isEmpty() || name == it->text())
		return;
	if (nameTaken(name)) {
		QMessageBox::warning(this, "RareStudio", "Something with that name already exists.");
		return;
	}
	if (obs_source_t *src = obs_get_source_by_name(it->text().toUtf8().constData())) {
		obs_source_set_name(src, name.toUtf8().constData());
		obs_source_release(src);
		QSignalBlocker block(m_scenes); // same scene, new label: not a scene change
		it->setText(name);
	}
}

void MainWindow::duplicateScene()
{
	QListWidgetItem *it = m_scenes->currentItem();
	obs_scene_t *scene = ObsCore::instance().currentScene();
	if (!it || !scene)
		return;
	QString name = it->text() + " copy";
	for (int n = 2; nameTaken(name); ++n)
		name = QString("%1 copy %2").arg(it->text()).arg(n);
	if (ObsCore::instance().duplicateScene(scene, name)) {
		m_scenes->addItem(name);
		m_scenes->setCurrentRow(m_scenes->count() - 1);
	}
}

void MainWindow::removeScene()
{
	QListWidgetItem *it = m_scenes->currentItem();
	if (!it || m_scenes->count() < 2)
		return;
	if (QMessageBox::question(this, "RareStudio", QString("Remove scene \"%1\"?").arg(it->text())) != QMessageBox::Yes)
		return;
	const QString name = it->text();
	obs_source_t *src = obs_get_source_by_name(name.toUtf8().constData());
	const int row = m_scenes->currentRow();
	m_scenes->setCurrentRow(row == 0 ? 1 : row - 1); // move off it first
	if (ObsCore::instance().programScene() == (src ? obs_scene_from_source(src) : nullptr))
		ObsCore::instance().commitToProgram(); // studio mode: it was on air, so replace it
	if (src) {
		ObsCore::instance().removeScene(obs_scene_from_source(src));
		obs_source_release(src);
	}
	delete m_scenes->takeItem(m_scenes->row(m_scenes->findItems(name, Qt::MatchExactly).value(0)));
}

void MainWindow::renameSource()
{
	QListWidgetItem *it = m_sources->currentItem();
	if (!it)
		return;
	bool ok = false;
	const QString name = QInputDialog::getText(this, "Rename source", "Name:", QLineEdit::Normal, it->text(), &ok);
	if (!ok || name.isEmpty() || name == it->text())
		return;
	if (nameTaken(name)) {
		QMessageBox::warning(this, "RareStudio", "Something with that name already exists.");
		return;
	}
	if (obs_source_t *src = obs_get_source_by_name(it->text().toUtf8().constData())) {
		obs_source_set_name(src, name.toUtf8().constData());
		obs_source_release(src);
	}
	refreshSources();
	selectInList(name);
	m_preview->selectByName(name);
}

void MainWindow::toggleLockSelected()
{
	if (obs_sceneitem_t *item = m_preview->selectedItem()) {
		obs_sceneitem_set_locked(item, !obs_sceneitem_locked(item));
		obs_sceneitem_release(item);
	}
}

void MainWindow::checkForUpdates()
{
	if (QSettings().value("update/url").toString().isEmpty()) {
		QMessageBox::information(this, "RareStudio",
					 "No update feed is set yet.\n\nPut its address in File > Settings > Updates.");
		return;
	}
	statusBar()->showMessage("Checking for updates...", 4000);
	m_updater->checkNow(true);
}
// ---- Web docks ------------------------------------------------------------------------------

void MainWindow::buildClipsDock()
{
	auto *dock = new QDockWidget("Clips", this);
	dock->setObjectName("clips");
	m_clips = new ClipsPanel;
	dock->setWidget(m_clips);
	addDockWidget(Qt::RightDockWidgetArea, dock);
	if (auto *stream = findChild<QDockWidget *>("stream"))
		tabifyDockWidget(stream, dock);
}

void MainWindow::buildWebDocks()
{
	auto *dash = new WebDock("Dashboards",
				 {{"YouTube Studio", QUrl("https://studio.youtube.com")},
				  {"Twitch", QUrl("https://dashboard.twitch.tv/stream-manager")}},
				 this);
	addDockWidget(Qt::RightDockWidgetArea, dash);
	// Share the Stream panel's space as a tab instead of squeezing in under it.
	if (auto *stream = findChild<QDockWidget *>("stream"))
		tabifyDockWidget(stream, dash);
	setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
	// Choosing it from the View menu should bring its tab to the front.
	connect(dash->toggleViewAction(), &QAction::toggled, dash, [dash](bool on) {
		if (on) // after the dock has actually been shown
			QTimer::singleShot(0, dash, [dash] { dash->raise(); });
	});
	dash->hide(); // hidden = no browser running
}
