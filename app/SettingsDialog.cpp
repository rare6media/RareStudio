#include "SettingsDialog.h"

#include "Accounts.h"
#include "Changelog.h"
#include "AudioPresets.h"
#include "EncoderPool.h"
#include "HotkeyManager.h"
#include "ObsCore.h"
#include "PropertiesDialog.h"
#include "Recorder.h"
#include "UiBits.h"
#include "Updater.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

struct Res {
	int w, h;
};
const Res kBase[] = {{3840, 2160}, {2560, 1440}, {1920, 1080}, {1600, 900}, {1280, 720}};
const Res kOut[] = {{1920, 1080}, {1600, 900}, {1280, 720}, {1024, 576}, {852, 480}};
const int kFps[] = {24, 30, 48, 60};

QString label(const Res &r)
{
	return QString("%1 x %2").arg(r.w).arg(r.h);
}

template <size_t N> void fill(QComboBox *c, const Res (&list)[N], int w, int h)
{
	for (const Res &r : list) {
		c->addItem(label(r), QPoint(r.w, r.h));
		if (r.w == w && r.h == h)
			c->setCurrentIndex(c->count() - 1);
	}
}

QLabel *hint(const QString &text)
{
	auto *l = new QLabel(text);
	l->setWordWrap(true);
	l->setObjectName("hint");
	return l;
}

} // namespace

SettingsDialog::Video SettingsDialog::load()
{
	QSettings s;
	return {s.value("video/base_w", 1920).toInt(), s.value("video/base_h", 1080).toInt(),
		s.value("video/out_w", 1920).toInt(), s.value("video/out_h", 1080).toInt(),
		s.value("video/fps", 30).toInt()};
}

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("Settings");
	resize(680, 560);

	auto *tabs = new QTabWidget;
	tabs->addTab(buildInterfaceTab(), "Interface");
	tabs->addTab(buildVideoTab(), "Video");
	tabs->addTab(buildAudioTab(), "Audio");
	tabs->addTab(buildOutputTab(), "Output");
	tabs->addTab(buildAccountsTab(), "Accounts");
	tabs->addTab(buildClipsTab(), "Clips");
	tabs->addTab(buildHotkeysTab(), "Hotkeys");
	tabs->addTab(buildUpdatesTab(), "Updates");
	for (int i = 0; i < tabs->count(); ++i) { // room to breathe on every page
		QLayout *l = tabs->widget(i)->layout();
		l->setContentsMargins(18, 18, 18, 18);
		if (auto *f = qobject_cast<QFormLayout *>(l)) {
			f->setHorizontalSpacing(16);
			f->setVerticalSpacing(10);
		}
	}

	auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(14, 14, 14, 12);
	lay->setSpacing(12);
	lay->addWidget(tabs);
	lay->addWidget(box);
	connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(box, &QDialogButtonBox::accepted, this, [this] {
		if (apply())
			accept();
	});
}

QWidget *SettingsDialog::buildInterfaceTab()
{
	m_scale = new QComboBox;
	const struct {
		const char *label;
		double scale;
	} sizes[] = {{"Compact (90%)", 0.9},   {"Normal (100%)", 1.0},  {"Comfortable (115%)", 1.15},
		     {"Large (130%)", 1.3},    {"Extra large (150%)", 1.5}, {"Huge (175%)", 1.75}};
	const double cur = UiBits::scaleFactor();
	int match = 1; // Normal unless the saved size is one of the presets
	for (const auto &sz : sizes) {
		m_scale->addItem(sz.label, sz.scale);
		if (std::abs(sz.scale - cur) < 0.01)
			match = m_scale->count() - 1;
	}
	m_scale->setCurrentIndex(match);
	m_animations = new QCheckBox("Light animations (fading dialogs and menus, pulsing LIVE / REC badges)");
	m_animations->setChecked(UiBits::animationsEnabled());

	auto *w = new QWidget;
	auto *form = new QFormLayout(w);
	form->addRow("Interface size", m_scale);
	form->addRow(hint("Scales every panel, button and text on top of Windows' own display scaling. "
			  "Pick a bigger size if things feel cramped. Takes effect after a restart."));
	form->addRow(m_animations);
	form->addRow(hint("Animations are tiny and never touch the video preview, so they cost next to nothing. "
			  "Turn them off if you prefer everything instant."));
	form->addRow(hint("Tip: drag the panels by their title bars to rearrange them, and use View > Reset layout "
			  "to get the default arrangement back."));
	return w;
}

QWidget *SettingsDialog::buildVideoTab()
{
	const Video v = load();
	m_base = new QComboBox;
	fill(m_base, kBase, v.baseW, v.baseH);
	m_out = new QComboBox;
	fill(m_out, kOut, v.outW, v.outH);
	m_fps = new QComboBox;
	for (int f : kFps) {
		m_fps->addItem(QString("%1 fps").arg(f), f);
		if (f == v.fps)
			m_fps->setCurrentIndex(m_fps->count() - 1);
	}

	auto *w = new QWidget;
	auto *form = new QFormLayout(w);
	form->addRow("Canvas (what you compose)", m_base);
	form->addRow("Stream output (what viewers get)", m_out);
	form->addRow("Frame rate", m_fps);
	form->addRow(hint("Lower output resolution and fps reduce encoder load and upload bandwidth. "
			  "Video changes can't be applied while live or recording."));
	return w;
}

QWidget *SettingsDialog::buildOutputTab()
{
	QSettings s;
	m_encoder = new QComboBox;
	m_encoder->addItem("Automatic (best available)", "auto");
	for (const auto &e : EncoderPool::availableVideoEncoders())
		m_encoder->addItem(e.second, e.first);
	m_encoder->setCurrentIndex(std::max(0, m_encoder->findData(s.value("output/encoder", "auto"))));

	m_videoKbps = new QSpinBox;
	m_videoKbps->setRange(500, 50000);
	m_videoKbps->setSuffix(" kbps");
	m_videoKbps->setValue(s.value("stream/video_kbps", 6000).toInt());
	m_dir = new QLineEdit(s.value("record/dir", Recorder::defaultDir()).toString());
	auto *browse = new QPushButton("Browse...");
	auto *dirRow = new QHBoxLayout;
	dirRow->addWidget(m_dir);
	dirRow->addWidget(browse);
	connect(browse, &QPushButton::clicked, this, [this] {
		const QString d = QFileDialog::getExistingDirectory(this, "Recording folder", m_dir->text());
		if (!d.isEmpty())
			m_dir->setText(d);
	});

	m_format = new QComboBox;
	for (const char *f : {"mkv", "mp4", "mov", "flv"})
		m_format->addItem(QString(f).toUpper(), f);
	m_format->setCurrentIndex(std::max(0, m_format->findData(s.value("record/format", "mkv"))));

	m_replaySecs = new QSpinBox;
	m_replaySecs->setRange(5, 300);
	m_replaySecs->setSuffix(" seconds");
	m_replaySecs->setValue(s.value("replay/seconds", 30).toInt());

	auto *w = new QWidget;
	auto *form = new QFormLayout(w);
	form->addRow("Video encoder", m_encoder);
	form->addRow("Video bitrate", m_videoKbps);
	form->addRow(hint("Streams, recordings and clips share one encode, so using all three at once "
			  "costs the same CPU/GPU as one. Hardware encoders (NVENC, AMD, Quick Sync) are best."));
	form->addRow("Recording folder", dirRow);
	form->addRow("Recording format", m_format);
	form->addRow("Clip length (replay buffer)", m_replaySecs);
	form->addRow(hint("MKV is safest: a crash won't corrupt the file. Encoder and bitrate changes apply the "
			  "next time you start streaming or recording."));
	return w;
}

QWidget *SettingsDialog::buildAudioTab()
{
	QSettings s;
	ObsCore &core = ObsCore::instance();

	m_audioKbps = new QSpinBox;
	m_audioKbps->setRange(64, 320);
	m_audioKbps->setSingleStep(32);
	m_audioKbps->setSuffix(" kbps");
	m_audioKbps->setValue(s.value("stream/audio_kbps", 160).toInt());

	m_monitor = new QComboBox;
	m_monitor->addItem("System default", "default");
	m_monitor->setItemData(0, "Default", Qt::UserRole + 1);
	if (obs_audio_monitoring_available()) {
		obs_enum_audio_monitoring_devices(
			[](void *p, const char *name, const char *id) {
				auto *c = static_cast<QComboBox *>(p);
				if (strcmp(id, "default") != 0) {
					c->addItem(QString::fromUtf8(name), QString::fromUtf8(id));
					c->setItemData(c->count() - 1, QString::fromUtf8(name), Qt::UserRole + 1);
				}
				return true;
			},
			m_monitor);
		m_monitor->setCurrentIndex(std::max(0, m_monitor->findData(s.value("audio/monitor_id", "default"))));
	} else {
		m_monitor->setEnabled(false);
	}

	// Device pickers and quick setups act on the live sources right away (they are not part of OK/Cancel).
	auto *micDev = new QPushButton("Microphone device...");
	auto *deskDev = new QPushButton("Desktop audio device...");
	micDev->setEnabled(core.micAudio());
	deskDev->setEnabled(core.desktopAudio());
	connect(micDev, &QPushButton::clicked, this, [this, &core] { PropertiesDialog(core.micAudio(), this).exec(); });
	connect(deskDev, &QPushButton::clicked, this,
		[this, &core] { PropertiesDialog(core.desktopAudio(), this).exec(); });
	auto *devRow = new QHBoxLayout;
	devRow->addWidget(micDev);
	devRow->addWidget(deskDev);

	auto *voice = new QPushButton("Add voice chain to Mic");
	voice->setToolTip("Noise suppression, noise gate, compressor and limiter. Filters you already have are kept.");
	auto *desk = new QPushButton("Add limiter to Desktop Audio");
	auto *levels = new QPushButton("Reset levels");
	levels->setToolTip("Mic 0 dB, desktop -8 dB, both unmuted, no sync offset.");
	auto *status = hint("");
	connect(voice, &QPushButton::clicked, this, [&core, status] {
		AudioPresets::applyVoiceChain(core.micAudio());
		status->setText("Mic now has the voice chain (open its Filters from the mixer to fine-tune).");
	});
	connect(desk, &QPushButton::clicked, this, [&core, status] {
		AudioPresets::applyDesktopChain(core.desktopAudio());
		status->setText("Desktop Audio now has a limiter.");
	});
	connect(levels, &QPushButton::clicked, this, [&core, status] {
		AudioPresets::resetLevels(core.micAudio(), core.desktopAudio());
		status->setText("Levels reset.");
	});
	auto *quickRow = new QHBoxLayout;
	quickRow->addWidget(voice);
	quickRow->addWidget(desk);
	quickRow->addWidget(levels);

	auto *w = new QWidget;
	auto *form = new QFormLayout(w);
	form->addRow(new QLabel("<b>Devices</b>"));
	form->addRow(devRow);
	form->addRow("Monitoring device (headphones)", m_monitor);
	form->addRow(hint("Per-source monitoring is switched on from the ⋯ menu on each mixer strip."));
	form->addRow(new QLabel("<b>Encoding</b>"));
	form->addRow("Audio bitrate", m_audioKbps);
	form->addRow(hint("48 kHz stereo AAC, shared by streams, recordings and clips. 160 kbps suits most "
			  "platforms; use 192-256 for music. Applies the next time you start streaming or recording."));
	form->addRow(new QLabel("<b>Quick setup</b>"));
	form->addRow(quickRow);
	form->addRow(status);
	form->addRow(hint("Per-source filters, volume, mute and sync offset are saved with your scenes."));
	return w;
}

QWidget *SettingsDialog::buildHotkeysTab()
{
	QSettings s;
	auto *w = new QWidget;
	auto *form = new QFormLayout(w);
	for (HotkeyManager::Action a : HotkeyManager::configurable()) {
		auto *edit = new QKeySequenceEdit(QKeySequence(s.value(HotkeyManager::settingsKey(a)).toString()));
		edit->setMaximumSequenceLength(1);
		m_keys.insert(a, edit);
		form->addRow(HotkeyManager::label(a), edit);
	}
	form->addRow(hint("Hotkeys work even when a game has focus. Ctrl+Alt+1 to 9 always jump to scene 1 to 9."));
	return w;
}

QWidget *SettingsDialog::buildUpdatesTab()
{
	QSettings s;
	// Share the main window's updater so a download started here survives closing this dialog.
	Updater *updater = parent() ? parent()->findChild<Updater *>() : nullptr;
	if (!updater)
		updater = new Updater(this);

	auto *installed = new QLabel(QString("RareStudio %1  -  build %2").arg(RARESTUDIO_VERSION, RARESTUDIO_BUILD));
	installed->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_latest = new QLabel("Not checked yet");
	m_latest->setWordWrap(true);
	auto *check = new QPushButton("Check now");
	auto *notes = new QPushButton("What's new");
	auto *latestRow = new QHBoxLayout;
	latestRow->addWidget(m_latest, 1);
	latestRow->addWidget(check);
	latestRow->addWidget(notes);
	connect(notes, &QPushButton::clicked, this, [this] { Changelog::show(this); });

	const auto setLatest = [this](const QString &text) { m_latest->setText(text); };
	connect(check, &QPushButton::clicked, this, [=] {
		check->setEnabled(false);
		setLatest("Checking...");
		updater->checkNow(true);
	});
	connect(updater, &Updater::checked, this, [=](const QString &latest, const QString &, bool newer) {
		check->setEnabled(true);
		setLatest(newer ? QString("RareStudio %1 is available").arg(latest)
				: Updater::isNewer(latest, RARESTUDIO_VERSION) ? QString("RareStudio %1 is available but skipped").arg(latest)
									       : QString("%1 is the latest release. You are up to date.").arg(latest));
	});
	connect(updater, &Updater::checkFailed, this, [=](const QString &why) {
		check->setEnabled(true);
		setLatest("Could not check: " + why);
	});
	connect(updater, &Updater::updateReady, this, [=](const QString &v) { setLatest(QString("RareStudio %1 is downloaded. Restart to apply it.").arg(v)); });
	connect(updater, &Updater::status, this, [=](const QString &msg) {
		if (msg.startsWith("Downloading") || msg.contains("rejected") || msg.contains("failed") || msg.startsWith("Could not")) {
			setLatest(msg);
			check->setEnabled(true);
		}
	});

	m_autoUpdate = new QCheckBox("Check for updates when RareStudio starts");
	m_autoUpdate->setChecked(s.value("update/auto", true).toBool());

	// Releases you chose not to take (also set automatically when you roll back).
	auto *skipRow = new QWidget;
	auto *skipLay = new QHBoxLayout(skipRow);
	skipLay->setContentsMargins(0, 0, 0, 0);
	auto *skipLabel = new QLabel;
	auto *unskip = new QPushButton("Stop skipping");
	skipLay->addWidget(skipLabel, 1);
	skipLay->addWidget(unskip);
	const auto refreshSkip = [skipRow, skipLabel] {
		skipRow->setVisible(!Updater::skipped().isEmpty());
		skipLabel->setText(QString("Skipping %1: it will not be downloaded automatically.").arg(Updater::skipped()));
	};
	refreshSkip();
	connect(unskip, &QPushButton::clicked, this, [=] {
		Updater::setSkipped({});
		refreshSkip();
	});

	m_keep = new QSpinBox;
	m_keep->setRange(0, 5);
	m_keep->setValue(Updater::keepCount());
	m_keep->setSuffix(" older versions");

	m_rollback = new QComboBox;
	const QStringList targets = Updater::rollbackTargets();
	m_rollback->addItems(targets);
	auto *rollBtn = new QPushButton("Roll back");
	rollBtn->setEnabled(!targets.isEmpty());
	m_rollback->setEnabled(!targets.isEmpty());
	if (targets.isEmpty())
		m_rollback->addItem("No older versions kept");
	auto *rollRow = new QHBoxLayout;
	rollRow->addWidget(m_rollback, 1);
	rollRow->addWidget(rollBtn);
	connect(rollBtn, &QPushButton::clicked, this, [this] {
		const QString v = m_rollback->currentText();
		if (QMessageBox::question(this, "Roll back",
					  QString("Go back to RareStudio %1?\n\nRareStudio will close and restart on that version. Your scenes, sources "
						  "and settings are kept. The release you leave will be skipped until you stop skipping it.")
						  .arg(v)) != QMessageBox::Yes)
			return;
		QString err;
		if (!Updater::rollbackTo(v, &err)) {
			QMessageBox::warning(this, "Roll back", err);
			return;
		}
		accept();
		if (QWidget *w = parentWidget() ? parentWidget()->window() : nullptr)
			w->close();
	});

	m_changelogMode = new QComboBox;
	m_changelogMode->addItem("After each update", "update");
	m_changelogMode->addItem("Every time RareStudio starts", "always");
	m_changelogMode->addItem("Never", "never");
	m_changelogMode->setCurrentIndex(std::max(0, m_changelogMode->findData(s.value("ui/changelog", "update").toString())));

	m_feed = new QLineEdit(s.value("update/url").toString());
	m_feed->setPlaceholderText(Updater::defaultFeed());

	auto *w = new QWidget;
	auto *form = new QFormLayout(w);
	form->addRow("Installed", installed);
	form->addRow("Latest release", latestRow);
	form->addRow(m_autoUpdate);
	form->addRow(skipRow);
	form->addRow("Keep", m_keep);
	form->addRow("Roll back to", rollRow);
	form->addRow("Show what's new", m_changelogMode);
	form->addRow("Update feed (optional)", m_feed);
	form->addRow(hint("Updates download in the background and apply the next time you start RareStudio. Your desktop shortcut "
			  "never changes. Kept versions let you roll back if a release misbehaves.\n\n"
			  "Leave the feed address empty to use the built-in one."));
	return w;
}

QWidget *SettingsDialog::buildAccountsTab()
{
	auto &acc = Accounts::instance();
	QSettings s;
	m_twitchId = new QLineEdit(acc.twitchClientId());
	m_twitchId->setPlaceholderText("Twitch app Client ID");
	m_googleId = new QLineEdit(acc.googleClientId());
	m_googleId->setPlaceholderText("Google OAuth Client ID");
	m_googleSecret = new QLineEdit;
	m_googleSecret->setEchoMode(QLineEdit::Password);
	m_googleSecret->setPlaceholderText(acc.googleClientSecret().isEmpty() ? "Google OAuth Client secret" : "(saved; type to replace)");
	m_twitchStatus = new QLabel;
	m_googleStatus = new QLabel;
	m_autoKeys = new QCheckBox("Fill in my stream keys automatically (at startup and when I connect)");
	m_autoKeys->setChecked(s.value("accounts/autokeys", true).toBool());

	auto *twitchConnect = new QPushButton("Connect Twitch");
	auto *twitchOff = new QPushButton("Disconnect");
	auto *googleConnect = new QPushButton("Connect Google");
	auto *googleOff = new QPushButton("Disconnect");
	auto *help = new QPushButton("How do I get these?");

	auto row = [](QWidget *a, QWidget *b, QWidget *c = nullptr) {
		auto *h = new QHBoxLayout;
		h->addWidget(a);
		h->addWidget(b);
		if (c)
			h->addWidget(c);
		return h;
	};

	auto *w = new QWidget;
	auto *form = new QFormLayout(w);
	form->addRow(hint("<b>Optional, and not needed to stream.</b> Going live only needs the stream key each platform "
			  "shows you, pasted into the Stream panel (works for Twitch, YouTube, Kick, TikTok and any RTMP server).<br><br>"
			  "Connecting accounts here only adds extras: stream keys filled in automatically, Twitch clips, and "
			  "uploading 9:16 Shorts to YouTube. Those need your own developer Client IDs, which Twitch and Google require "
			  "for any app. Tokens are stored encrypted for your Windows user."));
	form->addRow("Twitch Client ID", m_twitchId);
	form->addRow("", row(twitchConnect, twitchOff, m_twitchStatus));
	form->addRow("Google Client ID", m_googleId);
	form->addRow("Google Client secret", m_googleSecret);
	form->addRow("", row(googleConnect, googleOff, m_googleStatus));
	form->addRow(m_autoKeys);
	form->addRow(help);

	connect(twitchConnect, &QPushButton::clicked, this, [this] {
		Accounts::instance().setTwitchClientId(m_twitchId->text());
		Accounts::instance().connectTwitch();
	});
	connect(twitchOff, &QPushButton::clicked, this, [] { Accounts::instance().disconnectTwitch(); });
	connect(googleConnect, &QPushButton::clicked, this, [this] {
		Accounts::instance().setGoogleCredentials(m_googleId->text(), m_googleSecret->text());
		Accounts::instance().connectGoogle();
		m_googleStatus->setText("Finish signing in in your browser...");
	});
	connect(googleOff, &QPushButton::clicked, this, [] { Accounts::instance().disconnectGoogle(); });
	connect(help, &QPushButton::clicked, this, [this] {
		QMessageBox box(this);
		box.setWindowTitle("Getting your API credentials");
		box.setTextFormat(Qt::RichText);
		box.setText(
			"<b>Twitch</b> (about 2 minutes)<ol>"
			"<li>Open <a href='https://dev.twitch.tv/console/apps/create'>dev.twitch.tv/console/apps/create</a> and sign in.</li>"
			"<li>Name: anything unique (e.g. RareStudio-YourName). OAuth Redirect URL: <code>http://localhost</code>. "
			"Category: Application Integration. <b>Client Type: Public</b>.</li>"
			"<li>Create it, then copy the <b>Client ID</b> into RareStudio and press Connect Twitch.</li></ol>"
			"<b>Google / YouTube</b> (about 10 minutes, once)<ol>"
			"<li>Go to <a href='https://console.cloud.google.com/projectcreate'>console.cloud.google.com</a> and create a project.</li>"
			"<li>APIs &amp; Services &gt; Library: enable <b>YouTube Data API v3</b>.</li>"
			"<li>OAuth consent screen: User type External, app name RareStudio, add your own Google account as a test user. "
			"Then press <b>Publish app</b>. This stops sign-in expiring every 7 days. You will see an 'unverified app' warning "
			"once when signing in; that is normal for your own app.</li>"
			"<li>Credentials &gt; Create credentials &gt; OAuth client ID &gt; <b>Desktop app</b>. Copy the Client ID and secret here.</li></ol>"
			"<b>Good to know:</b> YouTube keeps videos uploaded by unverified API projects <b>private</b> until the project passes "
			"Google's audit. You can switch each Short to public in YouTube Studio. The default quota allows roughly 6 uploads a day.");
		box.setStandardButtons(QMessageBox::Ok);
		box.exec();
	});

	// Twitch shows a short code to type into twitch.tv/activate (or a link that already has it).
	connect(&acc, &Accounts::twitchDeviceCode, this, [this](const QString &code, const QUrl &url) {
		QDesktopServices::openUrl(url);
		m_twitchStatus->setText(QString("Enter code <b>%1</b> in the Twitch page that just opened").arg(code));
	});
	connect(&acc, &Accounts::changed, this, [this] { refreshAccountStatus(); });
	connect(&acc, &Accounts::failed, this, [this](const QString &msg) {
		refreshAccountStatus();
		QMessageBox::warning(this, "RareStudio", msg);
	});
	refreshAccountStatus();
	return w;
}

void SettingsDialog::refreshAccountStatus()
{
	auto &acc = Accounts::instance();
	m_twitchStatus->setText(acc.twitchConnected() ? QString("✔ Connected as <b>%1</b>").arg(acc.twitchLogin()) : "Not connected");
	m_googleStatus->setText(acc.googleConnected() ? QString("✔ Connected: <b>%1</b>").arg(acc.googleChannel()) : "Not connected");
}

QWidget *SettingsDialog::buildClipsTab()
{
	QSettings s;
	m_localClip = new QCheckBox("Save a local clip (full-size, from the replay buffer)");
	m_localClip->setChecked(s.value("clips/local", true).toBool());
	m_twitchClip = new QCheckBox("Also create a Twitch clip (on Twitch's servers, while you are live)");
	m_twitchClip->setChecked(s.value("clips/twitch", false).toBool());
	m_twitchClipSecs = new QSpinBox;
	m_twitchClipSecs->setRange(5, 60);
	m_twitchClipSecs->setSuffix(" seconds");
	m_twitchClipSecs->setValue(s.value("clips/twitch_seconds", 30).toInt());

	m_makeShort = new QCheckBox("Also make a 9:16 YouTube Short (vertical 1080x1920, ready instantly)");
	m_makeShort->setChecked(s.value("clips/short", false).toBool());
	m_shortSecs = new QSpinBox;
	m_shortSecs->setRange(10, 180);
	m_shortSecs->setSuffix(" seconds");
	m_shortSecs->setValue(s.value("shorts/seconds", 30).toInt());
	m_shortStyle = new QComboBox;
	m_shortStyle->addItem("Blurred background (fit picture in the middle)", "blurred");
	m_shortStyle->addItem("Fill the frame (crops the sides)", "fill");
	m_shortStyle->addItem("Fit with black bars", "fit");
	m_shortStyle->setCurrentIndex(std::max(0, m_shortStyle->findData(s.value("shorts/style", "blurred"))));

	m_reviewShort = new QCheckBox("Review each Short in the editor first (trim it, balance its audio tracks, then upload from there)");
	m_reviewShort->setChecked(s.value("clips/review", true).toBool());
	m_autoUpload = new QCheckBox("Upload each Short to YouTube as soon as it is saved (when review is off)");
	m_autoUpload->setChecked(s.value("clips/upload", false).toBool());
	m_privacy = new QComboBox;
	m_privacy->addItem("Private", "private");
	m_privacy->addItem("Unlisted", "unlisted");
	m_privacy->addItem("Public", "public");
	m_privacy->setCurrentIndex(std::max(0, m_privacy->findData(s.value("clips/privacy", "private"))));
	m_shortTitle = new QLineEdit(s.value("clips/title", "Clip {date} #Shorts").toString());

	auto *w = new QWidget;
	auto *form = new QFormLayout(w);
	form->addRow(hint("Pressing Clip! (or its hotkey) does everything ticked here at once. The Replay button must be "
			  "armed first for local clips and Shorts, because they come from the replay buffers."));
	form->addRow(m_localClip);
	form->addRow(m_twitchClip);
	form->addRow("Twitch clip length", m_twitchClipSecs);
	form->addRow(m_makeShort);
	form->addRow("Short length", m_shortSecs);
	form->addRow("Short layout", m_shortStyle);
	form->addRow(m_reviewShort);
	form->addRow(m_autoUpload);
	form->addRow("Upload visibility", m_privacy);
	form->addRow("Short title", m_shortTitle);
	form->addRow(hint("Title tokens: {date}, {time}. \"#Shorts\" is added if missing. Uploads need Google connected under Accounts, "
			  "and YouTube keeps API uploads private until Google approves your API project (see the help on the Accounts tab)."));
	return w;
}

bool SettingsDialog::apply()
{
	const QPoint b = m_base->currentData().toPoint(), o = m_out->currentData().toPoint();
	const int fps = m_fps->currentData().toInt();
	const Video cur = load();
	const bool videoChanged = b.x() != cur.baseW || b.y() != cur.baseH || o.x() != cur.outW || o.y() != cur.outH ||
				  fps != cur.fps;
	if (videoChanged) {
		if (!ObsCore::instance().applyVideo(b.x(), b.y(), o.x(), o.y(), fps)) {
			QMessageBox::warning(this, "RareStudio",
					     "Could not apply video settings (stop streaming/recording first):\n" +
						     ObsCore::instance().error());
			return false;
		}
	}

	QSettings s;
	s.setValue("video/base_w", b.x());
	s.setValue("video/base_h", b.y());
	s.setValue("video/out_w", o.x());
	s.setValue("video/out_h", o.y());
	s.setValue("video/fps", fps);
	s.setValue("output/encoder", m_encoder->currentData());
	s.setValue("stream/video_kbps", m_videoKbps->value());
	s.setValue("stream/audio_kbps", m_audioKbps->value());
	// Switching the monitoring device rebuilds every monitor stream, so only touch it when it changed.
	if (s.value("audio/monitor_id", "default") != m_monitor->currentData()) {
		s.setValue("audio/monitor_id", m_monitor->currentData());
		s.setValue("audio/monitor_name", m_monitor->currentData(Qt::UserRole + 1));
		ObsCore::applyMonitoringDevice();
	}
	s.setValue("record/dir", m_dir->text());
	s.setValue("record/format", m_format->currentData());
	s.setValue("replay/seconds", m_replaySecs->value());
	s.setValue("clips/local", m_localClip->isChecked());
	s.setValue("clips/twitch", m_twitchClip->isChecked());
	s.setValue("clips/twitch_seconds", m_twitchClipSecs->value());
	s.setValue("clips/short", m_makeShort->isChecked());
	s.setValue("shorts/seconds", m_shortSecs->value());
	s.setValue("shorts/style", m_shortStyle->currentData());
	s.setValue("clips/upload", m_autoUpload->isChecked());
	s.setValue("clips/review", m_reviewShort->isChecked());
	s.setValue("clips/privacy", m_privacy->currentData());
	s.setValue("clips/title", m_shortTitle->text());
	s.setValue("accounts/autokeys", m_autoKeys->isChecked());
	Accounts::instance().setTwitchClientId(m_twitchId->text());
	Accounts::instance().setGoogleCredentials(m_googleId->text(), m_googleSecret->text());
	s.setValue("update/url", m_feed->text().trimmed());
	s.setValue("update/auto", m_autoUpdate->isChecked());
	s.setValue("update/keep", m_keep->value());
	s.setValue("ui/changelog", m_changelogMode->currentData());
	s.setValue("ui/scale", m_scale->currentData());
	s.setValue("ui/animations", m_animations->isChecked());
	for (auto it = m_keys.cbegin(); it != m_keys.cend(); ++it)
		s.setValue(HotkeyManager::settingsKey(HotkeyManager::Action(it.key())), it.value()->keySequence().toString());
	return true;
}
