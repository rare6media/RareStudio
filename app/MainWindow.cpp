#include "MainWindow.h"

#include "Accounts.h"
#include "AudioTracks.h"
#include "Changelog.h"
#include "ClipEditor.h"
#include "Collections.h"
#include "Overlays.h"
#include "ClipsPanel.h"
#include "CrashReport.h"
#include "SplashScreen.h"
#include "FiltersDialog.h"
#include "HotkeyManager.h"
#include "MixerPanel.h"
#include "ObsCore.h"
#include "PreviewWidget.h"
#include "PropertiesDialog.h"
#include "SourcePicker.h"
#include "SettingsDialog.h"
#include "Secrets.h"
#include "TransformDialog.h"
#include "TransitionManager.h"
#include "TransitionsDialog.h"
#include "Updater.h"
#include "WebDock.h"

#include "Autosave.h"
#include "UiBits.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QActionGroup>
#include <QMenu>
#include <QTimer>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QProcess>
#include <QToolButton>
#include <QFrame>
#include <QAction>
#include <QUrl>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QSettings>
#include <QStyle>
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

#include <algorithm>
#include <climits>
#include <cmath>

#include <windows.h>
#include <psapi.h>

namespace {

// Bump when docks are added/removed so a stale saved layout does not hide or scramble them.
constexpr int kLayoutVersion = 5; // 5: vertical mixer strips

QString scenesFile()
{
	return Collections::activeFile(); // the active scene collection
}

// Streaming only ever needs the server + stream key each platform shows you; no API keys, no sign-in.
struct Preset {
	const char *name;
	const char *server;
	const char *keyPage; // where the platform shows your stream key
	const char *help;
};
const Preset kPresets[] = {
	{"Twitch", "rtmp://live.twitch.tv/app", "https://dashboard.twitch.tv/settings/stream",
	 "Creator Dashboard > Settings > Stream > Primary Stream key > Copy."},
	{"YouTube", "rtmp://a.rtmp.youtube.com/live2", "https://www.youtube.com/live_dashboard",
	 "YouTube Studio > Go live > Stream > Stream key > Copy."},
	{"Kick", "rtmps://fa723fc1b171.global-contribute.live-video.net/app", "https://kick.com/dashboard/settings/stream",
	 "Dashboard > Settings > Stream URL & Key. If Kick shows a different Stream URL, paste that as the server."},
	{"TikTok", "", "https://livecenter.tiktok.com/producer",
	 "TikTok LIVE Producer shows a Server URL and Stream key together; paste both. TikTok only offers keys to "
	 "accounts it has approved for LIVE (usually 1,000+ followers); if the page shows no key, your account "
	 "isn't eligible yet and no app can get one for you."},
	{"Facebook", "rtmps://live-api-s.facebook.com:443/rtmp/", "https://www.facebook.com/live/producer",
	 "Live Producer > Streaming software > Stream key."},
	{"Custom RTMP", "", nullptr, "Any RTMP/RTMPS server: paste the server URL and stream key you were given."},
};

const Preset *presetFor(const QString &name)
{
	for (const Preset &p : kPresets)
		if (name.startsWith(QString::fromUtf8(p.name), Qt::CaseInsensitive))
			return &p;
	return nullptr;
}

// Add / edit one stream destination. Returns false on Cancel.
bool editDestinationDialog(QWidget *parent, Destination &d, bool isNew)
{
	QDialog dlg(parent);
	dlg.setWindowTitle(isNew ? "Add stream destination" : "Edit stream destination");
	dlg.resize(520, 0);

	auto *platform = new QComboBox;
	for (const Preset &p : kPresets)
		platform->addItem(QString::fromUtf8(p.name));
	auto *name = new QLineEdit(d.name);
	auto *server = new QLineEdit(d.server);
	server->setPlaceholderText("rtmp://...");
	auto *key = new QLineEdit(d.key);
	key->setEchoMode(QLineEdit::Password);
	key->setPlaceholderText("Paste your stream key");
	auto *show = new QPushButton("Show");
	show->setCheckable(true);
	auto *find = new QPushButton("Get my stream key ↗");
	find->setObjectName("accent");
	auto *help = new QLabel;
	help->setObjectName("hint");
	help->setWordWrap(true);

	auto *keyRow = new QHBoxLayout;
	keyRow->addWidget(key, 1);
	keyRow->addWidget(show);

	auto *form = new QFormLayout;
	form->setHorizontalSpacing(14);
	form->setVerticalSpacing(10);
	form->addRow("Platform", platform);
	form->addRow("Name", name);
	form->addRow("Server", server);
	form->addRow("Stream key", keyRow);
	form->addRow("", find);

	auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto *lay = new QVBoxLayout(&dlg);
	lay->setContentsMargins(18, 18, 18, 14);
	lay->setSpacing(14);
	lay->addLayout(form);
	// Outside the form: a wrapped label in a form row doesn't grow, and the TikTok note is four lines.
	help->setMinimumHeight(help->fontMetrics().lineSpacing() * 4);
	help->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	lay->addWidget(help);
	lay->addWidget(box);

	auto sync = [&](bool fillDefaults) {
		const Preset &p = kPresets[platform->currentIndex()];
		help->setText(QString::fromUtf8(p.help));
		find->setVisible(p.keyPage != nullptr);
		if (fillDefaults) {
			// Only replace what still looks like another preset's default, never something you typed.
			bool defaultName = name->text().isEmpty(), defaultServer = server->text().isEmpty();
			for (const Preset &q : kPresets) {
				defaultName |= name->text() == QString::fromUtf8(q.name);
				defaultServer |= server->text() == QString::fromUtf8(q.server);
			}
			if (defaultName)
				name->setText(QString::fromUtf8(p.name));
			if (defaultServer)
				server->setText(QString::fromUtf8(p.server));
		}
	};
	if (const Preset *p = presetFor(d.name))
		platform->setCurrentIndex(int(p - kPresets));
	else if (!isNew)
		platform->setCurrentIndex(int(std::size(kPresets)) - 1); // custom
	sync(isNew);
	QObject::connect(platform, &QComboBox::currentIndexChanged, &dlg, [&] { sync(true); });
	QObject::connect(show, &QPushButton::toggled, &dlg, [&](bool on) {
		key->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
		show->setText(on ? "Hide" : "Show");
	});
	QObject::connect(find, &QPushButton::clicked, &dlg, [&] {
		if (const char *url = kPresets[platform->currentIndex()].keyPage)
			QDesktopServices::openUrl(QUrl(QString::fromUtf8(url)));
	});
	QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
		if (server->text().trimmed().isEmpty()) {
			QMessageBox::warning(&dlg, "RareStudio", "Enter the server URL the platform gave you.");
			return;
		}
		dlg.accept();
	});
	if (dlg.exec() != QDialog::Accepted)
		return false;
	d.name = name->text().trimmed().isEmpty() ? platform->currentText() : name->text().trimmed();
	d.server = server->text().trimmed();
	d.key = key->text().trimmed(); // pasted keys often carry a stray space or newline
	return true;
}

constexpr int kPaneHeadHeight = 32;

// Every list in the UI finds sources by name, so names have to stay unique.
bool nameTaken(const QString &name)
{
	if (obs_source_t *s = obs_get_source_by_name(name.toUtf8().constData())) {
		obs_source_release(s);
		return true;
	}
	return false;
}

QString uniqueName(const QString &base)
{
	QString name = base;
	for (int n = 2; nameTaken(name); ++n)
		name = QString("%1 %2").arg(base).arg(n);
	return name;
}

} // namespace

MainWindow::MainWindow()
{
	setWindowTitle(QString("RareStudio - %1").arg(Collections::active()));
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
		auto *headBox = new QWidget;
		headBox->setFixedHeight(kPaneHeadHeight); // same height as the editor pane's header
		head->setContentsMargins(0, 0, 0, 0);
		headBox->setLayout(head);
		pl->addWidget(headBox);
		pl->addWidget(m_program, 1);
		connect(go, &QPushButton::clicked, this, [] { ObsCore::instance().commitToProgram(); });
	}
	m_programPane->hide();

	// The editor preview gets a matching header in Studio Mode so both canvases line up exactly.
	m_previewPane = new QWidget;
	{
		auto *pl = new QVBoxLayout(m_previewPane);
		pl->setContentsMargins(0, 0, 0, 0);
		pl->setSpacing(4);
		m_previewHead = new QWidget;
		m_previewHead->setFixedHeight(kPaneHeadHeight);
		auto *hl = new QHBoxLayout(m_previewHead);
		hl->setContentsMargins(0, 0, 0, 0);
		auto *label = new QLabel("PREVIEW (editing)");
		label->setStyleSheet("color: #60a5fa; font-weight: 700;");
		hl->addWidget(label);
		hl->addStretch();
		m_previewHead->hide();
		pl->addWidget(m_previewHead);
		pl->addWidget(m_preview, 1);
	}
	m_split = new QSplitter(Qt::Horizontal);
	m_split->addWidget(m_previewPane);
	m_split->addWidget(m_programPane);
	m_split->setChildrenCollapsible(false);
	m_split->setHandleWidth(12); // breathing room between the two canvases
	setCentralWidget(m_split);

	m_statusLabel = new QLabel("Offline");
	statusBar()->addPermanentWidget(m_statusLabel);

	ObsCore &core = ObsCore::instance();

	// Restore saved scenes, or start with one empty scene. A file cut short by a crash mid-write falls
	// back to the previous good copy (scenes.json.bak).
	QStringList sceneOrder;
	QString lastScene;
	if (obs_data_t *saved = obs_data_create_from_json_file_safe(scenesFile().toUtf8().constData(), "bak")) {
		if (obs_data_array_t *arr = obs_data_get_array(saved, "sources")) {
			obs_load_sources(arr, nullptr, nullptr);
			obs_data_array_release(arr);
		}
		if (obs_data_array_t *order = obs_data_get_array(saved, "scene_order")) {
			for (size_t i = 0, n = obs_data_array_count(order); i < n; ++i) {
				obs_data_t *it = obs_data_array_item(order, i);
				sceneOrder << QString::fromUtf8(obs_data_get_string(it, "name"));
				obs_data_release(it);
			}
			obs_data_array_release(order);
		}
		lastScene = QString::fromUtf8(obs_data_get_string(saved, "current_scene"));
		obs_data_release(saved);
	}
	const bool uncleanExit = QSettings().value("session/running", false).toBool();
	QSettings().setValue("session/running", true); // cleared again on a normal exit

	// Menus first so their order is File, Edit, View.
	QMenu *file = menuBar()->addMenu("&File");
	QMenu *edit = menuBar()->addMenu("&Edit");
	m_viewMenu = menuBar()->addMenu("&View");

	QMenu *help = menuBar()->addMenu("&Help");
	help->addAction("&What's new", this, [this] { Changelog::show(this); });
	help->addAction("Check for &updates", this, &MainWindow::checkForUpdates);
	help->addSeparator();
	help->addAction("Open &logs folder", this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(CrashReport::logsDir())); });
	help->addAction(QString("&About RareStudio %1").arg(RARESTUDIO_VERSION), this, [this] {
		QMessageBox::about(this, "About RareStudio",
				   QString("<b>RareStudio %1</b><br>Build %2<br><br>A lighter OBS-style streaming studio.").arg(RARESTUDIO_VERSION, RARESTUDIO_BUILD));
	});

	// Each step is slow enough to freeze the startup sequence; pump() keeps it moving between them.
	AudioTracks::routeAll(); // scenes are loaded: sort their audio into the clip tracks
	buildToolbar();
	SplashScreen::pump();
	buildScenesDock(sceneOrder);
	buildSourcesDock();
	SplashScreen::pump();
	buildMixerDock();
	SplashScreen::pump();
	buildStreamDock();
	buildClipsDock();
	SplashScreen::pump();
	buildWebDocks();
	SplashScreen::pump();

	if (m_scenes->count() == 0)
		addScene();
	const auto last = m_scenes->findItems(lastScene, Qt::MatchExactly);
	m_scenes->setCurrentRow(last.isEmpty() ? 0 : m_scenes->row(last.first()));

	// Autosave: every change is written moments after you make it (Autosave::touch from the UI, plus
	// libobs' own source signals), and a slow timer backs that up. A crash (driver, capture hook...)
	// must never cost you a scene, a source, a filter or a stream key.
	Autosave *autosave = Autosave::instance();
	autosave->setSaver([this] { persistAll(); });
	autosave->hookObs();
	m_autosave.setInterval(10000);
	connect(&m_autosave, &QTimer::timeout, this, [this] { persistAll(); });
	m_autosave.start();
	connect(m_dests, &QTableWidget::itemChanged, this, [] { Autosave::touch(); }); // the On checkbox
	connect(m_preview, &PreviewWidget::itemEdited, this, [] { Autosave::touch(); }); // drags, nudges
	if (uncleanExit)
		QTimer::singleShot(800, this, [this] {
			statusBar()->showMessage("RareStudio didn't close normally last time. Your scenes, sources and stream "
						 "destinations were restored from the autosave.", 15000);
		});

	// Every dock can be toggled from View; Reset layout recovers from a messy arrangement.
	m_defaultState = saveState(kLayoutVersion);
	for (QDockWidget *d : findChildren<QDockWidget *>())
		m_viewMenu->addAction(d->toggleViewAction());
	m_viewMenu->addSeparator();
	m_viewMenu->addAction("Reset layout", this, [this] {
		restoreState(m_defaultState, kLayoutVersion);
		for (QDockWidget *d : findChildren<QDockWidget *>())
			d->setVisible(d->objectName() != "dashboards");
		applyDefaultDockSizes();
	});

	// Preview <-> source list stay in sync; right-click and Delete work on the preview itself.
	connect(m_preview, &PreviewWidget::selectionChanged, this, &MainWindow::selectInList);
	connect(m_preview, &PreviewWidget::contextRequested, this, &MainWindow::showItemMenu);
	connect(m_preview, &PreviewWidget::deleteRequested, this, &MainWindow::removeSelectedSource);

	buildCollectionsMenu(file->addMenu("Scene &collection"));
	file->addAction("&Settings...", this, &MainWindow::openSettings);
	file->addAction("Check for &updates", this, &MainWindow::checkForUpdates);
	file->addSeparator();
	file->addAction("Take &screenshot", this, &MainWindow::takeScreenshot)->setToolTip("Save what viewers see as a PNG");
	file->addAction("Open recordings &folder", this, [] {
		const QString dir = QSettings().value("record/dir", Recorder::defaultDir()).toString();
		QDir().mkpath(dir);
		QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
	});
	file->addSeparator();
	file->addAction("E&xit", this, &QWidget::close);

	// Window-wide shortcuts; a focused text box still gets its own Ctrl+C / Ctrl+V first.
	edit->addAction("&Copy source", QKeySequence::Copy, this, &MainWindow::copySource);
	edit->addAction("&Paste source", QKeySequence::Paste, this, &MainWindow::pasteSource);
	edit->addAction("&Duplicate source", QKeySequence(Qt::CTRL | Qt::Key_D), this, &MainWindow::duplicateSource);
	edit->addSeparator();
	edit->addAction("Add &overlay scene...", this, &MainWindow::addOverlayScene)
		->setToolTip("Starting soon, Be right back, Stream ending or a Countdown");
	edit->addSeparator();
	edit->addAction("&Transform...", QKeySequence(Qt::CTRL | Qt::Key_E), this, &MainWindow::transformSelected);
	edit->addAction("&Fit to screen", QKeySequence(Qt::CTRL | Qt::Key_F), this, [this] {
		if (obs_sceneitem_t *item = m_preview->selectedItem()) {
			ObsCore::fitToCanvas(item);
			obs_sceneitem_release(item);
			Autosave::touch();
		}
	});
	connect(&m_rec, &Recorder::screenshotSaved, this, [this](const QString &path) {
		statusBar()->showMessage(path.isEmpty() ? QString("Could not save the screenshot.")
							: QString("Screenshot saved: %1").arg(QDir::toNativeSeparators(path)),
					 8000);
		if (!path.isEmpty())
			m_clips->add("Screenshot: " + QFileInfo(path).fileName(), path);
	});

	// Recording / clips / hotkeys
	connect(&m_rec, &Recorder::recordingStopped, this, [this](const QString &path, const QString &err) {
		m_recBtn->setText("● Rec");
		m_recBtn->setChecked(false);
		m_recBtn->setEnabled(true);
		syncPauseButton();
		statusBar()->showMessage(err.isEmpty() ? QString("Recording saved: %1").arg(QDir::toNativeSeparators(path))
						       : QString("Recording stopped: %1").arg(err), 10000);
	});
	connect(&m_rec, &Recorder::clipSaved, this, [this](const QString &path) {
		statusBar()->showMessage(QString("Clip saved: %1").arg(QDir::toNativeSeparators(path)), 10000);
		m_clips->add("Clip saved: " + QFileInfo(path).fileName(), path);
	});
	connect(&m_rec, &Recorder::replayStopped, this, [this] { setReplayUi(m_vertical.active()); });
	connect(&m_vertical, &VerticalClip::clipSaved, this, &MainWindow::onShortSaved);
	connect(m_clips, &ClipsPanel::editRequested, this, [this](const QString &path) { openClipEditor(path, true); });

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

	connect(&m_stream, &MultiStream::destinationState, this, [this](int row, const QString &state) {
		setDestinationStatus(row, state);
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
	// Older layouts are ignored; a first start (or an outdated layout) gets the default panel sizes.
	if (!restoreState(QSettings().value("ui/state").toByteArray(), kLayoutVersion))
		QTimer::singleShot(0, this, [this] { applyDefaultDockSizes(); });
}

MainWindow::~MainWindow()
{
	// After this the window (and what persistAll reads) is gone; libobs keeps running until main() shuts it down.
	if (Autosave *a = Autosave::instance()) {
		a->unhookObs();
		a->setSaver(nullptr);
	}
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

	// Related controls sit together in a rounded capsule so the bar reads as a few clusters, not a row of buttons.
	auto capsule = [bar](std::initializer_list<QWidget *> widgets, const char *name = "barGroup") {
		auto *f = new QFrame;
		f->setObjectName(name);
		auto *h = new QHBoxLayout(f);
		h->setContentsMargins(4, 4, 4, 4);
		h->setSpacing(3);
		for (QWidget *w : widgets)
			h->addWidget(w);
		bar->addWidget(f);
		return f;
	};
	auto actionButton = [this](const QString &text, auto slot, const QString &tip) {
		auto *act = new QAction(text, this);
		act->setToolTip(tip);
		connect(act, &QAction::triggered, this, slot);
		auto *b = new QToolButton;
		b->setObjectName("barBtn");
		b->setDefaultAction(act);
		b->setCursor(Qt::PointingHandCursor);
		return b;
	};

	auto *logo = new QLabel;
	logo->setPixmap(QIcon(":/logo").pixmap(30, 30));
	logo->setContentsMargins(4, 0, 10, 0);
	logo->setToolTip(QString("RareStudio %1").arg(RARESTUDIO_VERSION));
	bar->addWidget(logo);

	capsule({actionButton("+ Scene", &MainWindow::addScene, "Add a scene"),
		 actionButton("+ Source", &MainWindow::addSourceDialog, "Add a source to this scene")});

	m_studioBtn = new QPushButton("Studio Mode");
	m_studioBtn->setObjectName("barToggle");
	m_studioBtn->setCheckable(true);
	m_studioBtn->setCursor(Qt::PointingHandCursor);
	m_studioBtn->setToolTip("Edit one scene while another is live, then press Transition to send it on air");
	bar->addWidget(m_studioBtn);
	connect(m_studioBtn, &QPushButton::toggled, this, &MainWindow::setStudio);

	// Scene transition: how the picture changes when you switch scenes. The list comes from the
	// transition manager (built-ins, RareStudio's own, and any stingers you add).
	auto *transIcon = new QLabel(QString::fromUtf8("\xE2\x87\x84")); // ⇄
	transIcon->setObjectName("barCaption");
	transIcon->setToolTip("Scene transition");
	auto *trans = new QComboBox;
	trans->setObjectName("barCombo");
	trans->setMinimumWidth(150);
	trans->setToolTip("Scene transition");
	auto *ms = new QSpinBox;
	ms->setObjectName("barSpin");
	ms->setRange(0, 5000);
	ms->setSingleStep(50);
	ms->setSuffix(" ms");
	ms->setFixedWidth(84);
	ms->setToolTip("Transition length");
	auto *cfg = new QToolButton;
	cfg->setObjectName("barBtn");
	cfg->setText("Edit…");
	cfg->setCursor(Qt::PointingHandCursor);
	cfg->setToolTip("Transitions & stingers: tune, add, set per-scene overrides");
	capsule({transIcon, trans, ms, cfg});

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
		Autosave::touch();
	});
	connect(ms, &QSpinBox::valueChanged, this, [trans](int v) {
		TransitionManager::instance().setMs(trans->currentData().toString(), v);
		Autosave::touch();
	});
	connect(cfg, &QToolButton::clicked, this, [this, syncTransitionUi] {
		TransitionsDialog dlg(this);
		connect(&dlg, &TransitionsDialog::changed, this, syncTransitionUi);
		dlg.exec();
		syncTransitionUi();
		Autosave::touch();
	});

	// Recording and instant replay ("clip") controls.
	m_recBtn = new QPushButton("● Rec");
	m_recBtn->setObjectName("recBtn");
	m_recBtn->setCheckable(true);
	m_recBtn->setToolTip("Record to a file");
	m_pauseBtn = new QPushButton("❚❚");
	m_pauseBtn->setObjectName("capBtn");
	m_pauseBtn->setCheckable(true);
	m_pauseBtn->setVisible(false); // shown only while recording
	m_replayBtn = new QPushButton("Replay");
	m_replayBtn->setObjectName("capBtn");
	m_replayBtn->setCheckable(true);
	m_replayBtn->setToolTip("Keep the last seconds in memory so you can save a clip after something happens");
	m_clipBtn = new QPushButton("Clip!");
	m_clipBtn->setObjectName("capBtn");
	m_clipBtn->setEnabled(false);
	m_clipBtn->setToolTip("Save the last seconds as a clip");
	for (auto *b : {m_recBtn, m_pauseBtn, m_replayBtn, m_clipBtn})
		b->setCursor(Qt::PointingHandCursor);
	capsule({m_recBtn, m_pauseBtn, m_replayBtn, m_clipBtn});
	connect(m_recBtn, &QPushButton::clicked, this, &MainWindow::toggleRecord);
	connect(m_pauseBtn, &QPushButton::clicked, this, &MainWindow::togglePause);
	connect(m_replayBtn, &QPushButton::clicked, this, &MainWindow::toggleReplay);
	connect(m_clipBtn, &QPushButton::clicked, this, &MainWindow::saveClip);

	auto *spacer = new QWidget;
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	bar->addWidget(spacer);

	m_recChip = new StatusChip("recChip", QColor(0xf8, 0x71, 0x71));
	m_liveChip = new StatusChip("liveChip", QColor(0xff, 0x4d, 0x6d));
	bar->addWidget(m_recChip);
	bar->addWidget(m_liveChip);

	m_stats = new QLabel;
	m_stats->setObjectName("stats");
	m_stats->setTextFormat(Qt::RichText);
	capsule({m_stats}, "statsGroup");

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

	// A muted label in front of each bright value.
	auto stat = [](const QString &label, const QString &value, const char *colour = "#ececf4") {
		return QString("<span style='color:#6f6f86'>%1</span>&nbsp;<b style='color:%2'>%3</b>").arg(label, colour, value);
	};
	const QString gap = "&nbsp;&nbsp;&nbsp;";
	QString text = stat("CPU", QString("%1%").arg(cpu, 0, 'f', 1)) + gap + stat("RAM", QString("%1 MB").arg(ramMb, 0, 'f', 0)) +
		       gap + stat("FPS", QString::number(obs_get_active_fps(), 'f', 0));

	auto clock = [](qint64 t) {
		return QString("%1:%2:%3")
			.arg(t / 3600, 2, 10, QChar('0'))
			.arg(t / 60 % 60, 2, 10, QChar('0'))
			.arg(t % 60, 2, 10, QChar('0'));
	};
	m_recChip->setActive(m_rec.recording());
	if (m_rec.recording())
		m_recChip->setText((m_rec.paused() ? "PAUSED  " : "REC  ") + clock(m_rec.recordingSeconds()));
	syncPauseButton(); // going live or arming replay changes whether pausing is possible
	m_liveChip->setActive(m_stream.live());
	if (m_stream.live())
		m_liveChip->setText("LIVE  " + clock((QDateTime::currentMSecsSinceEpoch() - m_liveSinceMs) / 1000));

	if (m_stream.live()) {
		const MultiStream::Stats s = m_stream.stats();
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		if (m_lastBytesMs && now > m_lastBytesMs && s.bytes >= m_lastBytes)
			m_kbps = (s.bytes - m_lastBytes) * 8.0 / (now - m_lastBytesMs); // bytes*8/ms = kbit/s
		m_lastBytes = s.bytes;
		m_lastBytesMs = now;
		text += gap + stat("↑", QString("%1 kbps").arg(m_kbps, 0, 'f', 0)) + gap +
			stat("DROPPED", QString("%1/%2").arg(s.dropped).arg(s.total), s.dropped ? "#f87171" : "#ececf4");
	} else {
		m_lastBytesMs = 0;
	}
	m_stats->setText(text);
}

void MainWindow::openSettings()
{
	persistAll(); // settings changes touch the video/audio engine; never risk losing state to a crash there
	const double scaleBefore = UiBits::scaleFactor();
	if (SettingsDialog(this).exec() != QDialog::Accepted)
		return;
	m_hotkeys->reload();
	UiBits::installEffects(*qApp); // animations on/off
	persistAll();

	if (std::abs(UiBits::scaleFactor() - scaleBefore) < 0.01)
		return;
	if (m_stream.live() || m_rec.recording()) {
		statusBar()->showMessage("The new interface size applies the next time you start RareStudio.", 10000);
		return;
	}
	if (QMessageBox::question(this, "RareStudio", "Restart RareStudio now to apply the new interface size?") ==
	    QMessageBox::Yes) {
		m_restarting = true;
		close();
	}
}

// ---- Recording, clips, hotkeys ----------------------------------------------------------------

void MainWindow::toggleRecord()
{
	if (m_rec.recording()) {
		m_rec.stopRecording();
		// The muxer still has to finish the file; recordingStopped re-enables the button.
		m_recBtn->setText("Stopping…");
		m_recBtn->setEnabled(false);
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
	syncPauseButton();
}

void MainWindow::togglePause()
{
	if (!m_rec.recording())
		return;
	const bool want = !m_rec.paused();
	if (want && !m_pauseBtn->isEnabled()) {
		statusBar()->showMessage("Pause works when only recording: streaming and the replay buffer share its encoder.", 8000);
		return;
	}
	if (!m_rec.setPaused(want))
		statusBar()->showMessage("Could not pause the recording right now; try again in a moment.", 6000);
	syncPauseButton();
	updateStats();
}

void MainWindow::syncPauseButton()
{
	const bool rec = m_rec.recording();
	// Paused encoders are shared encoders: never leave them paused under a stream or the replay buffer.
	const bool shared = m_stream.live() || m_rec.replayActive();
	if (shared && m_rec.paused())
		m_rec.setPaused(false);
	m_pauseBtn->setVisible(rec);
	m_pauseBtn->setEnabled(!shared);
	QSignalBlocker b(m_pauseBtn);
	m_pauseBtn->setChecked(m_rec.paused());
	m_pauseBtn->setText(m_rec.paused() ? "▶ Resume" : "❚❚");
	m_pauseBtn->setToolTip(shared ? "Pause is only available while recording alone (not while live or with the replay buffer armed)"
				      : m_rec.paused() ? "Resume recording" : "Pause recording");
}

void MainWindow::takeScreenshot()
{
	m_rec.screenshot();
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
	m_rec.setPaused(false); // the replay buffer shares the recording's encoder
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
	if (QSettings().value("clips/review", true).toBool()) { // look it over (and trim, balance audio) before it goes anywhere
		openClipEditor(path, true);
		return;
	}
	if (!QSettings().value("clips/upload", false).toBool())
		return;
	if (!Accounts::instance().googleConnected()) {
		m_clips->update(id, "Short saved (not uploaded: connect Google in Settings > Accounts)", path);
		return;
	}
	uploadShort(id, path);
}

void MainWindow::openClipEditor(const QString &path, bool offerUpload)
{
	QSettings s;
	QString title = s.value("clips/title", "Clip {date} #Shorts").toString();
	const QDateTime now = QDateTime::currentDateTime();
	title.replace("{date}", now.toString("yyyy-MM-dd")).replace("{time}", now.toString("HH:mm"));
	auto *editor = new ClipEditor(path, title, s.value("clips/privacy", "private").toString(),
				      offerUpload && Accounts::instance().googleConnected(), this);
	if (!editor->valid()) {
		delete editor;
		return;
	}
	connect(editor, &ClipEditor::exported, this, [this](const QString &file, bool upload, const QString &t, const QString &priv) {
		const int id = m_clips->add("Edited clip saved: " + QFileInfo(file).fileName(), file);
		statusBar()->showMessage("Edited clip saved: " + QDir::toNativeSeparators(file), 10000);
		if (upload)
			uploadShort(id, file, t, priv);
	});
	editor->setWindowModality(Qt::NonModal); // keep streaming while you review
	editor->show();
}

void MainWindow::uploadShort(int panelId, const QString &path, const QString &titleOverride, const QString &privacyOverride)
{
	QSettings s;
	QString title = titleOverride.isEmpty() ? s.value("clips/title", "Clip {date} #Shorts").toString() : titleOverride;
	const QDateTime now = QDateTime::currentDateTime();
	title.replace("{date}", now.toString("yyyy-MM-dd")).replace("{time}", now.toString("HH:mm"));
	if (!title.contains("#Shorts", Qt::CaseInsensitive))
		title += " #Shorts";

	UploadJob *job = Accounts::instance().uploadShort(path, title, "Clipped live with RareStudio #Shorts",
							  privacyOverride.isEmpty() ? s.value("clips/privacy", "private").toString() : privacyOverride);
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
	if (m_stream.live())
		return; // rows must not change under a running stream; the next refresh catches up
	for (int r = 0; r < m_dests->rowCount(); ++r) {
		QTableWidgetItem *nm = m_dests->item(r, 1), *k = m_dests->item(r, 2);
		if (!nm || !k || nm->text() != name)
			continue;
		QSignalBlocker block(m_dests);
		nm->setData(Qt::UserRole, server);
		nm->setToolTip(server);
		k->setData(Qt::UserRole, key);
		refreshDestinationRow(r);
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
	case HotkeyManager::PauseRecord: togglePause(); break;
	case HotkeyManager::Screenshot: takeScreenshot(); break;
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
	m_autosave.stop();
	m_stream.stop();
	m_rec.shutdown();
	m_vertical.stop();
	persistAll();
	QSettings().setValue("session/running", false); // a clean exit: no "restored from autosave" next time
	if (m_restarting)
		qApp->setProperty("rsRestart", true); // main() relaunches once libobs has shut down
	QMainWindow::closeEvent(e);
}

// Everything that must survive a crash: stream destinations and keys, scenes/sources (with their
// filters, volumes and devices) and the window layout. Called on exit, periodically and before
// anything risky (settings changes).
void MainWindow::persistAll() const
{
	saveDestinations();

	obs_data_t *out = obs_data_create();
	obs_data_array_t *arr = obs_save_sources();
	obs_data_set_array(out, "sources", arr);
	// libobs keeps sources newest-first, so the scene list order is saved separately.
	obs_data_array_t *order = obs_data_array_create();
	for (int i = 0; i < m_scenes->count(); ++i) {
		obs_data_t *it = obs_data_create();
		obs_data_set_string(it, "name", m_scenes->item(i)->text().toUtf8().constData());
		obs_data_array_push_back(order, it);
		obs_data_release(it);
	}
	obs_data_set_array(out, "scene_order", order);
	if (const QListWidgetItem *cur = m_scenes->currentItem())
		obs_data_set_string(out, "current_scene", cur->text().toUtf8().constData());
	// Written to scenes.json.tmp then swapped in, keeping the previous file as .bak: never half a file.
	obs_data_save_json_safe(out, scenesFile().toUtf8().constData(), "tmp", "bak");
	obs_data_array_release(order);
	obs_data_array_release(arr);
	obs_data_release(out);

	QSettings s;
	s.setValue("ui/geometry", saveGeometry());
	s.setValue("ui/state", saveState(kLayoutVersion));
	s.sync();
}

// The preview is what you look at all day: keep the side panels to a sensible width and the mixer
// from eating half the window.
void MainWindow::applyDefaultDockSizes()
{
	auto *scenes = findChild<QDockWidget *>("scenes");
	auto *stream = findChild<QDockWidget *>("stream");
	auto *mixer = findChild<QDockWidget *>("mixer");
	if (scenes && stream)
		resizeDocks({scenes, stream}, {280, 380}, Qt::Horizontal);
	if (mixer)
		resizeDocks({mixer}, {300}, Qt::Vertical);
	if (stream)
		stream->raise();
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

void MainWindow::buildScenesDock(const QStringList &order)
{
	auto *dock = new QDockWidget("Scenes", this);
	dock->setObjectName("scenes");
	auto *w = new QWidget;
	auto *lay = new QVBoxLayout(w);
	lay->setContentsMargins(10, 10, 10, 10);
	lay->setSpacing(8);
	m_scenes = new QListWidget;
	lay->addWidget(m_scenes);
	auto *add = new QPushButton("+  Add Scene");
	lay->addWidget(add);
	dock->setWidget(w);
	addDockWidget(Qt::LeftDockWidgetArea, dock);

	QStringList names;
	obs_enum_scenes(
		[](void *p, obs_source_t *src) {
			static_cast<QStringList *>(p)->append(QString::fromUtf8(obs_source_get_name(src)));
			ObsCore::instance().holdScene(src);
			return true;
		},
		&names);
	// Saved order first; anything not in it (older save files) keeps libobs' order after them.
	std::stable_sort(names.begin(), names.end(), [&order](const QString &a, const QString &b) {
		const qsizetype ia = order.indexOf(a), ib = order.indexOf(b);
		return (ia < 0 ? INT_MAX : ia) < (ib < 0 ? INT_MAX : ib);
	});
	m_scenes->addItems(names);

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
		Autosave::touch(); // reopen on the scene you were on
	});
}

// Starting soon / BRB / Stream ending / Countdown: a scene holding one full-canvas browser source.
void MainWindow::addOverlayScene()
{
	Overlays::Spec spec;
	if (!Overlays::ask(this, [](const QString &n) { return nameTaken(n) || nameTaken(n + " overlay"); }, &spec))
		return;
	ObsCore &core = ObsCore::instance();
	if (!core.createScene(spec.sceneName))
		return;
	m_scenes->addItem(spec.sceneName);
	m_scenes->setCurrentRow(m_scenes->count() - 1); // makes it the scene sources are added to

	const QString sourceName = spec.sceneName + " overlay";
	if (obs_source_t *src = core.addSource("browser_source", sourceName)) {
		obs_data_t *s = obs_data_create();
		obs_data_set_string(s, "url", spec.url.toUtf8().constData());
		obs_data_set_int(s, "width", core.canvasWidth());
		obs_data_set_int(s, "height", core.canvasHeight());
		obs_data_set_int(s, "fps", 30);
		obs_data_set_string(s, "css", ""); // the page paints its own background
		obs_source_update(src, s);
		obs_data_release(s);
		if (obs_scene_t *scene = core.currentScene())
			if (obs_sceneitem_t *item = obs_scene_find_source(scene, sourceName.toUtf8().constData()))
				ObsCore::fitToCanvas(item);
		obs_source_release(src);
		refreshSources();
	}
	Autosave::touch();
}

void MainWindow::addScene()
{
	bool ok = true;
	QString name = m_scenes->count() == 0 ? "Scene" : QInputDialog::getText(this, "Add Scene", "Name:", QLineEdit::Normal,
									       QString("Scene %1").arg(m_scenes->count() + 1), &ok);
	if (!ok || name.isEmpty())
		return;
	if (nameTaken(name)) {
		QMessageBox::warning(this, "RareStudio", "Something with that name already exists.");
		return;
	}
	obs_scene_t *scene = ObsCore::instance().createScene(name);
	if (!scene)
		return;
	m_scenes->addItem(name);
	m_scenes->setCurrentRow(m_scenes->count() - 1);
	Autosave::touch();
}

// ---- Sources --------------------------------------------------------------------------------

void MainWindow::buildSourcesDock()
{
	auto *dock = new QDockWidget("Sources", this);
	dock->setObjectName("sources");
	auto *w = new QWidget;
	auto *lay = new QVBoxLayout(w);
	lay->setContentsMargins(10, 10, 10, 10);
	lay->setSpacing(8);
	m_sources = new QListWidget;
	lay->addWidget(m_sources, 1);
	m_sourcesEmpty = new QLabel("No sources in this scene yet.\nPress + to add a capture, camera, media or text.");
	m_sourcesEmpty->setObjectName("empty");
	m_sourcesEmpty->setAlignment(Qt::AlignCenter);
	m_sourcesEmpty->setWordWrap(true);
	lay->addWidget(m_sourcesEmpty);
	auto *row = new QHBoxLayout;
	row->setSpacing(4);
	auto tool = [](const QString &text, const QString &tip) {
		auto *b = new QToolButton;
		b->setObjectName("dockTool");
		b->setText(text);
		b->setToolTip(tip);
		b->setCursor(Qt::PointingHandCursor);
		return b;
	};
	auto *add = tool("+", "Add source");
	auto *rem = tool("−", "Remove source (Delete)");
	auto *up = tool("▲", "Bring forward");
	auto *down = tool("▼", "Send backward");
	auto *props = tool("Properties", "Edit the selected source (double-click works too)");
	auto *filt = tool("Filters", "Filters on the selected source");
	auto *xform = tool("Transform", "Position, size, crop");
	for (auto *b : {add, rem, up, down})
		row->addWidget(b);
	row->addStretch();
	lay->addLayout(row);
	auto *edits = new QHBoxLayout; // own row, so the labels never get squeezed into "..."
	edits->setSpacing(4);
	for (auto *b : {props, filt, xform}) {
		b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		edits->addWidget(b);
	}
	lay->addLayout(edits);
	dock->setWidget(w);
	addDockWidget(Qt::LeftDockWidgetArea, dock);
	connect(filt, &QToolButton::clicked, this, &MainWindow::filtersSelected);
	m_itemButtons = {rem, up, down, props, filt, xform};
	connect(m_preview, &PreviewWidget::selectionChanged, this, &MainWindow::updateItemButtons);

	m_sources->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_sources, &QListWidget::customContextMenuRequested, this,
		[this](const QPoint &p) { showItemMenu(m_sources->viewport()->mapToGlobal(p)); });
	connect(m_sources, &QListWidget::currentTextChanged, this, [this](const QString &name) {
		m_preview->selectByName(name);
		updateItemButtons();
	});
	connect(add, &QToolButton::clicked, this, &MainWindow::addSourceDialog);
	connect(props, &QToolButton::clicked, this, &MainWindow::editSelectedSource);
	connect(xform, &QToolButton::clicked, this, &MainWindow::transformSelected);
	connect(up, &QToolButton::clicked, this, [this] { moveSelected(OBS_ORDER_MOVE_UP); });
	connect(down, &QToolButton::clicked, this, [this] { moveSelected(OBS_ORDER_MOVE_DOWN); });
	connect(m_sources, &QListWidget::itemDoubleClicked, this, &MainWindow::editSelectedSource);
	connect(rem, &QToolButton::clicked, this, &MainWindow::removeSelectedSource);
	connect(m_sources, &QListWidget::itemChanged, this, [](QListWidgetItem *it) {
		obs_scene_t *scene = ObsCore::instance().currentScene();
		if (!scene)
			return;
		if (obs_sceneitem_t *si = obs_scene_find_source(scene, it->text().toUtf8().constData()))
			obs_sceneitem_set_visible(si, it->checkState() == Qt::Checked);
		Autosave::touch();
	});
}

void MainWindow::refreshSources()
{
	QSignalBlocker block(m_sources);
	m_sources->clear();
	obs_scene_t *scene = ObsCore::instance().currentScene();
	auto showEmpty = qScopeGuard([this] {
		m_sourcesEmpty->setVisible(m_sources->count() == 0);
		updateItemButtons();
	});
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

void MainWindow::updateItemButtons()
{
	const bool any = !m_preview->selectedName().isEmpty();
	for (QWidget *b : m_itemButtons)
		b->setEnabled(any);
}

void MainWindow::copySource()
{
	obs_sceneitem_t *item = m_preview->selectedItem();
	if (!item)
		return;
	m_copied.source = QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(item)));
	obs_sceneitem_get_info2(item, &m_copied.transform);
	obs_sceneitem_get_crop(item, &m_copied.crop);
	m_copied.visible = obs_sceneitem_visible(item);
	obs_sceneitem_release(item);
	statusBar()->showMessage(QString("Copied \"%1\". Paste it into any scene with Ctrl+V.").arg(m_copied.source), 5000);
}

void MainWindow::pasteSource()
{
	if (m_copied.source.isEmpty())
		return;
	obs_source_t *src = obs_get_source_by_name(m_copied.source.toUtf8().constData());
	if (!src) {
		statusBar()->showMessage(QString("\"%1\" no longer exists.").arg(m_copied.source), 5000);
		return;
	}
	obs_scene_t *scene = ObsCore::instance().currentScene();
	// The source list finds items by name, so a scene shows each source at most once: paste a copy then.
	placeSource(src, scene && obs_scene_find_source(scene, m_copied.source.toUtf8().constData()));
	obs_source_release(src);
}

void MainWindow::duplicateSource()
{
	obs_sceneitem_t *item = m_preview->selectedItem();
	if (!item)
		return;
	const CopiedItem keep = m_copied; // duplicating doesn't overwrite what you copied
	m_copied.source = QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(item)));
	obs_sceneitem_get_info2(item, &m_copied.transform);
	obs_sceneitem_get_crop(item, &m_copied.crop);
	m_copied.visible = obs_sceneitem_visible(item);
	placeSource(obs_sceneitem_get_source(item), true);
	obs_sceneitem_release(item);
	m_copied = keep;
}

void MainWindow::placeSource(obs_source_t *src, bool duplicate)
{
	obs_scene_t *scene = ObsCore::instance().currentScene();
	if (!scene || !src)
		return;
	// Scenes can't be pasted into themselves (libobs refuses, and a loop would never render).
	if (obs_scene_get_source(scene) == src)
		return;
	obs_source_t *put = src;
	if (duplicate) {
		// A full copy, filters included, under a fresh name.
		put = obs_source_duplicate(src, uniqueName(QString::fromUtf8(obs_source_get_name(src))).toUtf8().constData(), false);
		if (!put) {
			statusBar()->showMessage("That source can't be duplicated.", 5000);
			return;
		}
	}
	if (obs_sceneitem_t *item = obs_scene_add(scene, put)) {
		obs_sceneitem_set_info2(item, &m_copied.transform);
		obs_sceneitem_set_crop(item, &m_copied.crop);
		obs_sceneitem_set_visible(item, m_copied.visible);
		const QString name = QString::fromUtf8(obs_source_get_name(put));
		refreshSources();
		selectInList(name);
		m_preview->selectByName(name);
		Autosave::touch();
	}
	if (duplicate)
		obs_source_release(put);
}

void MainWindow::addSourceDialog()
{
	if (!ObsCore::instance().currentScene())
		return;

	QByteArray id;
	QString finalName;
	if (!pickSource(this, uniqueName, &id, &finalName))
		return;

	if (obs_source_t *src = ObsCore::instance().addSource(id.constData(), finalName)) {
		refreshSources();
		selectInList(finalName);
		m_preview->selectByName(finalName);
		Autosave::touch(); // it exists now, so it is saved now, whatever happens in the dialog
		PropertiesDialog dlg2(src, this);
		dlg2.exec();
		obs_source_release(src);
		Autosave::touch();
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
		Autosave::touch();
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
	Autosave::touch();
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
		Autosave::touch();
	}
}

void MainWindow::transformSelected()
{
	if (obs_sceneitem_t *item = m_preview->selectedItem()) {
		TransformDialog(item, this).exec();
		obs_sceneitem_release(item);
		Autosave::touch();
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
	Autosave::touch();
}

void MainWindow::showItemMenu(const QPoint &globalPos)
{
	obs_sceneitem_t *item = m_preview->selectedItem();
	QMenu menu(this);
	auto addPaste = [&] {
		QAction *paste = menu.addAction("Paste", QKeySequence::Paste, this, &MainWindow::pasteSource);
		paste->setEnabled(!m_copied.source.isEmpty());
	};
	if (!item) {
		menu.addAction("Add source...", this, &MainWindow::addSourceDialog);
		addPaste();
		menu.exec(globalPos);
		return;
	}

	menu.addAction("Properties...", this, &MainWindow::editSelectedSource);
	menu.addAction("Filters...", this, &MainWindow::filtersSelected);
	menu.addAction("Rename...", this, &MainWindow::renameSource);
	menu.addSeparator();
	menu.addAction("Copy", QKeySequence::Copy, this, &MainWindow::copySource);
	addPaste();
	menu.addAction("Duplicate", QKeySequence(Qt::CTRL | Qt::Key_D), this, &MainWindow::duplicateSource);
	menu.addSeparator();
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
	Autosave::touch(); // fit, center, reset...
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
	// Named after the source: with a GoXLR they become "System" and "Chat Mic".
	for (obs_source_t *src : {core.desktopAudio(), core.micAudio()})
		if (src)
			m_mixer->addStrip(QString::fromUtf8(obs_source_get_name(src)), src);
	connect(m_mixer, &MixerPanel::filtersRequested, this, [this](obs_source_t *src) {
		FiltersDialog(src, this).exec();
		Autosave::touch();
	});
	connect(m_mixer, &MixerPanel::propertiesRequested, this, [this](obs_source_t *src) {
		PropertiesDialog(src, this).exec();
		Autosave::touch();
	});
}

// ---- Streaming ------------------------------------------------------------------------------

void MainWindow::buildStreamDock()
{
	auto *dock = new QDockWidget("Stream", this);
	dock->setObjectName("stream");
	auto *w = new QWidget;
	auto *lay = new QVBoxLayout(w);
	lay->setContentsMargins(10, 10, 10, 10);
	lay->setSpacing(8);

	// Rows are read-only: the On box is ticked in place, everything else opens the destination dialog
	// (no half-typed key sitting in a cell editor when something crashes).
	m_dests = new QTableWidget(0, 4);
	m_dests->setHorizontalHeaderLabels({"On", "Platform", "Key", "Status"});
	QHeaderView *hh = m_dests->horizontalHeader();
	hh->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	hh->setSectionResizeMode(1, QHeaderView::Stretch);
	hh->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	hh->setSectionResizeMode(3, QHeaderView::Stretch);
	hh->setHighlightSections(false);
	m_dests->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_dests->setWordWrap(false);
	m_dests->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_dests->setSelectionMode(QAbstractItemView::SingleSelection);
	m_dests->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_dests->setShowGrid(false);
	m_dests->verticalHeader()->hide();
	m_dests->verticalHeader()->setDefaultSectionSize(34);
	m_dests->setToolTip("Double-click a destination to edit its server or stream key");
	lay->addWidget(m_dests, 1);
	m_destsEmpty = new QLabel("No destinations yet.\nAdd Twitch, YouTube, Kick, TikTok or any RTMP server:\n"
				  "they all go live together from one encode.");
	m_destsEmpty->setObjectName("empty");
	m_destsEmpty->setAlignment(Qt::AlignCenter);
	m_destsEmpty->setWordWrap(true);
	(new QVBoxLayout(m_dests->viewport()))->addWidget(m_destsEmpty);
	auto syncEmpty = [this] { m_destsEmpty->setVisible(m_dests->rowCount() == 0); };
	connect(m_dests->model(), &QAbstractItemModel::rowsInserted, this, syncEmpty);
	connect(m_dests->model(), &QAbstractItemModel::rowsRemoved, this, syncEmpty);

	auto *row = new QHBoxLayout;
	row->setSpacing(6);
	auto *add = new QPushButton("+  Add destination");
	add->setObjectName("accent");
	auto *edit = new QPushButton("Edit");
	m_destRemove = new QPushButton("Remove");
	row->addWidget(add);
	row->addStretch();
	row->addWidget(edit);
	row->addWidget(m_destRemove);
	lay->addLayout(row);

	auto *settingsHint = new QLabel("Streaming only needs each platform's stream key: no sign-in or API keys. "
					"Encoder and bitrate: File > Settings > Output.");
	settingsHint->setObjectName("hint");
	settingsHint->setWordWrap(true);
	lay->addWidget(settingsHint);

	dock->setWidget(w);
	addDockWidget(Qt::RightDockWidgetArea, dock);

	connect(add, &QPushButton::clicked, this, [this] {
		Destination d;
		if (!editDestinationDialog(this, d, true))
			return;
		addDestination(d.name, d.server, d.key, true);
		m_dests->selectRow(m_dests->rowCount() - 1);
		saveDestinations();
	});
	connect(edit, &QPushButton::clicked, this, [this] { editDestination(m_dests->currentRow()); });
	auto syncButtons = [this, edit] {
		const bool picked = !m_dests->selectionModel()->selectedRows().isEmpty();
		edit->setEnabled(picked);
		m_destRemove->setEnabled(picked && !m_stream.live());
	};
	connect(m_dests, &QTableWidget::itemSelectionChanged, this, syncButtons);
	syncButtons();
	connect(m_dests, &QTableWidget::cellDoubleClicked, this, [this](int r, int col) {
		if (col != 0)
			editDestination(r);
	});
	connect(m_destRemove, &QPushButton::clicked, this, [this] {
		const int r = m_dests->currentRow();
		if (r < 0 || m_stream.live())
			return;
		const QTableWidgetItem *nameItem = m_dests->item(r, 1);
		if (QMessageBox::question(this, "RareStudio",
					  QString("Remove the destination \"%1\"?").arg(nameItem ? nameItem->text() : QString())) !=
		    QMessageBox::Yes)
			return;
		m_dests->removeRow(r);
		saveDestinations();
	});
	loadDestinations();
}

void MainWindow::addDestination(const QString &name, const QString &server, const QString &key, bool enabled)
{
	// Fill the whole row before anyone hears about it: itemChanged on the first cell used to save a
	// row whose other cells didn't exist yet (null item -> crash when adding a destination).
	QSignalBlocker block(m_dests);
	const int r = m_dests->rowCount();
	m_dests->insertRow(r);
	auto *on = new QTableWidgetItem;
	on->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
	on->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
	m_dests->setItem(r, 0, on);
	auto *nm = new QTableWidgetItem(name);
	nm->setData(Qt::UserRole, server);
	nm->setToolTip(server);
	m_dests->setItem(r, 1, nm);
	auto *k = new QTableWidgetItem;
	k->setData(Qt::UserRole, key); // the real key lives in item data and is never displayed
	m_dests->setItem(r, 2, k);
	m_dests->setItem(r, 3, new QTableWidgetItem("offline"));
	refreshDestinationRow(r);
}

void MainWindow::refreshDestinationRow(int r)
{
	QTableWidgetItem *k = m_dests->item(r, 2);
	if (!k)
		return;
	const bool hasKey = !k->data(Qt::UserRole).toString().isEmpty();
	k->setText(hasKey ? QString(8, QChar(0x2022)) : "missing");
	k->setForeground(hasKey ? QColor(0x9a, 0x9a, 0xb0) : QColor(0xf5, 0x9e, 0x0b));
}

void MainWindow::setDestinationStatus(int r, const QString &state)
{
	QTableWidgetItem *st = m_dests->item(r, 3);
	if (!st)
		return;
	st->setText(state);
	st->setToolTip(state);
	const QColor c = state == "live"                                         ? QColor(0x4a, 0xde, 0x80)
			 : state.startsWith("error") || state.startsWith("could") ? QColor(0xf8, 0x71, 0x71)
			 : state == "offline"                                     ? QColor(0x8a, 0x8a, 0xa0)
										  : QColor(0xfb, 0xbf, 0x24);
	st->setForeground(c);
}

void MainWindow::editDestination(int r)
{
	if (r < 0 || r >= m_dests->rowCount() || !m_dests->item(r, 1) || !m_dests->item(r, 2))
		return;
	if (m_stream.live()) {
		statusBar()->showMessage("Stop streaming to change a destination.", 6000);
		return;
	}
	Destination d = destinations().value(r);
	if (!editDestinationDialog(this, d, false))
		return;
	{
		QSignalBlocker block(m_dests);
		m_dests->item(r, 1)->setText(d.name);
		m_dests->item(r, 1)->setData(Qt::UserRole, d.server);
		m_dests->item(r, 1)->setToolTip(d.server);
		m_dests->item(r, 2)->setData(Qt::UserRole, d.key);
		refreshDestinationRow(r);
	}
	saveDestinations();
}

QList<Destination> MainWindow::destinations() const
{
	QList<Destination> out;
	for (int r = 0; r < m_dests->rowCount(); ++r) {
		const QTableWidgetItem *on = m_dests->item(r, 0), *nm = m_dests->item(r, 1), *k = m_dests->item(r, 2);
		if (!on || !nm || !k)
			continue; // never trust a row to be complete
		Destination d;
		d.enabled = on->checkState() == Qt::Checked;
		d.name = nm->text();
		d.server = nm->data(Qt::UserRole).toString();
		d.key = k->data(Qt::UserRole).toString();
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
	auto setLiveLook = [this](bool live) {
		m_goLive->setText(live ? "Stop Streaming" : "Go Live");
		m_goLive->setProperty("live", live);
		m_goLive->style()->unpolish(m_goLive);
		m_goLive->style()->polish(m_goLive);
		m_destRemove->setEnabled(!live && !m_dests->selectionModel()->selectedRows().isEmpty());
	};
	if (m_stream.live()) {
		m_stream.stop();
		setLiveLook(false);
		m_statusLabel->setText("Offline");
		for (int r = 0; r < m_dests->rowCount(); ++r)
			setDestinationStatus(r, "offline");
		updateStats();
		return;
	}

	const QList<Destination> dests = destinations();
	for (const Destination &d : dests)
		if (d.enabled && d.key.isEmpty() &&
		    QMessageBox::question(this, "RareStudio",
					  QString("\"%1\" has no stream key, so that platform will refuse the stream. Go live anyway?")
						  .arg(d.name)) != QMessageBox::Yes)
			return;

	m_rec.setPaused(false); // the stream rides the recording's encoder, which must not stay paused
	QString err;
	if (!m_stream.start(dests, &err)) {
		QMessageBox::warning(this, "RareStudio", err);
		return;
	}
	m_liveSinceMs = QDateTime::currentMSecsSinceEpoch();
	setLiveLook(true);
	m_statusLabel->setText("Live (" + m_stream.encoderName() + ")");
	updateStats();
}

// ---- Studio Mode, scene and source management ---------------------------------------------------

void MainWindow::setStudio(bool on)
{
	ObsCore &core = ObsCore::instance();
	core.setStudioMode(on);
	m_preview->setShowsEditScene(on);
	m_programPane->setVisible(on);
	m_previewHead->setVisible(on);
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
		Autosave::touch();
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
		Autosave::touch();
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
	Autosave::touch();
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
	Autosave::touch();
	refreshSources();
	selectInList(name);
	m_preview->selectByName(name);
}

void MainWindow::toggleLockSelected()
{
	if (obs_sceneitem_t *item = m_preview->selectedItem()) {
		obs_sceneitem_set_locked(item, !obs_sceneitem_locked(item));
		obs_sceneitem_release(item);
		Autosave::touch();
	}
}

// Scene collections: pick one to switch (RareStudio saves and restarts onto it), or manage them.
void MainWindow::buildCollectionsMenu(QMenu *menu)
{
	connect(menu, &QMenu::aboutToShow, this, [this, menu] {
		menu->clear();
		const QString current = Collections::active();
		auto *group = new QActionGroup(menu);
		for (const QString &name : Collections::list()) {
			QAction *a = menu->addAction(name);
			a->setCheckable(true);
			a->setChecked(name.compare(current, Qt::CaseInsensitive) == 0);
			group->addAction(a);
			connect(a, &QAction::triggered, this, [this, name] { switchCollection(name); });
		}
		menu->addSeparator();
		auto ask = [this](const QString &title, const QString &initial = {}) {
			bool ok = false;
			const QString n = QInputDialog::getText(this, title, "Name:", QLineEdit::Normal, initial, &ok).trimmed();
			return ok ? n : QString();
		};
		auto fail = [this](const QString &msg) {
			if (!msg.isEmpty())
				QMessageBox::warning(this, "Scene collection", msg);
			return !msg.isEmpty();
		};
		menu->addAction("New collection...", this, [=] {
			const QString n = ask("New scene collection");
			if (n.isEmpty())
				return;
			persistAll();
			if (!fail(Collections::create(n)))
				switchCollection(n);
		});
		menu->addAction("Duplicate this collection...", this, [=] {
			const QString n = ask("Duplicate scene collection", Collections::active() + " copy");
			if (n.isEmpty())
				return;
			persistAll(); // the copy is of what is on disk
			if (!fail(Collections::duplicate(Collections::active(), n)))
				switchCollection(n);
		});
		menu->addAction("Rename this collection...", this, [=] {
			const QString n = ask("Rename scene collection", Collections::active());
			if (n.isEmpty() || n == Collections::active())
				return;
			persistAll();
			if (!fail(Collections::rename(Collections::active(), n)))
				setWindowTitle(QString("RareStudio - %1").arg(Collections::active()));
		});
		QMenu *del = menu->addMenu("Delete collection");
		for (const QString &name : Collections::list()) {
			if (name.compare(current, Qt::CaseInsensitive) == 0)
				continue;
			del->addAction(name, this, [=] {
				if (QMessageBox::question(this, "Delete collection", QString("Delete \"%1\" and all its scenes? This cannot be undone.").arg(name)) == QMessageBox::Yes)
					fail(Collections::remove(name));
			});
		}
		del->setEnabled(!del->isEmpty());
		menu->addAction("Open collections folder", this, [] {
			QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(Collections::fileFor("x")).absolutePath()));
		});
	});
}

void MainWindow::switchCollection(const QString &name)
{
	if (name.compare(Collections::active(), Qt::CaseInsensitive) == 0)
		return;
	persistAll(); // closeEvent saves again, after asking first if you are live
	const QString previous = Collections::active();
	Collections::setActive(name);
	m_restarting = true;
	if (!close()) { // cancelled (still live): stay where you were
		Collections::setActive(previous);
		m_restarting = false;
	}
}

void MainWindow::checkForUpdates()
{
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
	if (auto *stream = findChild<QDockWidget *>("stream")) {
		tabifyDockWidget(stream, dash);
		stream->raise(); // Stream is the tab you want first, not Clips
	}
	setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
	// Choosing it from the View menu should bring its tab to the front.
	connect(dash->toggleViewAction(), &QAction::toggled, dash, [dash](bool on) {
		if (on) // after the dock has actually been shown
			QTimer::singleShot(0, dash, [dash] { dash->raise(); });
	});
	dash->hide(); // hidden = no browser running
}
