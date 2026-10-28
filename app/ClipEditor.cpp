#include "ClipEditor.h"

#include "AudioTracks.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kRulerH = 34, kLaneH = 58, kPad = 10;
constexpr int kRate = ClipMedia::kRate;

QString clock(double t)
{
	t = std::max(0.0, t);
	const int m = int(t) / 60;
	return QString("%1:%2").arg(m).arg(t - m * 60, 4, 'f', 1, QChar('0'));
}

QColor trackColor(int t)
{
	static const QColor c[] = {QColor(167, 139, 250), QColor(52, 211, 153), QColor(34, 211, 238), QColor(245, 158, 11)};
	return t < 4 ? c[t] : QColor(232, 121, 249);
}

float softLimit(float x)
{
	const float a = std::fabs(x);
	return a > 0.9f ? std::copysign(0.9f + 0.1f * std::tanh((a - 0.9f) / 0.1f), x) : x;
}

} // namespace

// The wide area to the right of the track controls: time ruler, one waveform lane per audio track, the
// trim handles and the playhead. Lane heights match the control rows beside it.
class TrackTimeline : public QWidget {
	Q_OBJECT
public:
	explicit TrackTimeline(QWidget *parent = nullptr) : QWidget(parent)
	{
		setMouseTracking(true);
		setMinimumWidth(520);
	}
	void setup(double duration, int tracks)
	{
		m_dur = std::max(0.1, duration);
		m_peaks.assign(size_t(tracks), {});
		m_gain.assign(size_t(tracks), 1.0f);
		m_in = 0;
		m_out = m_dur;
		setFixedHeight(kRulerH + tracks * kLaneH + kPad);
	}
	void setPeaks(int track, std::vector<float> peaks)
	{
		m_peaks[size_t(track)] = std::move(peaks);
		update();
	}
	void setGains(const std::vector<float> &g)
	{
		m_gain = g;
		update();
	}
	void setRange(double in, double out)
	{
		m_in = in;
		m_out = out;
		update();
	}
	void setPlayhead(double t)
	{
		m_pos = t;
		update();
	}
	double inPoint() const { return m_in; }
	double outPoint() const { return m_out; }

signals:
	void seekRequested(double seconds);
	void rangeEdited(double in, double out);

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const int n = int(m_peaks.size());
		p.fillRect(rect(), QColor(16, 16, 23));
		for (int t = 0; t < n; ++t)
			p.fillRect(QRect(0, kRulerH + t * kLaneH, width(), kLaneH), t % 2 ? QColor(20, 20, 29) : QColor(23, 23, 33));

		// Ruler.
		p.setPen(QColor(150, 150, 185));
		QFont f = font();
		f.setPixelSize(10);
		p.setFont(f);
		double step = 1;
		for (double s : {1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0})
			if (s * x2px(1) >= 56) {
				step = s;
				break;
			} else
				step = s;
		for (double t = 0; t <= m_dur + 0.001; t += step) {
			const int x = int(t2x(t));
			p.drawLine(x, kRulerH - 8, x, kRulerH - 2);
			p.drawText(QRect(x + 3, 4, 60, 14), Qt::AlignLeft | Qt::AlignVCenter, clock(t));
		}

		// Waveforms: height scaled by each track's gain, so you can see what the balance does.
		for (int t = 0; t < n; ++t) {
			const auto &pk = m_peaks[size_t(t)];
			const int mid = kRulerH + t * kLaneH + kLaneH / 2;
			const bool live = m_gain[size_t(t)] > 0.001f;
			QColor c = live ? trackColor(t) : QColor(110, 110, 130);
			c.setAlpha(live ? 230 : 110);
			p.setPen(c);
			if (!pk.empty()) {
				const int w = width() - 2 * kPad;
				for (int x = 0; x < w; ++x) {
					const float v = std::min(1.0f, pk[size_t(x) * pk.size() / size_t(w)] * (live ? m_gain[size_t(t)] : 1.0f));
					const int h = int(v * (kLaneH * 0.44));
					p.drawLine(kPad + x, mid - h, kPad + x, mid + h);
				}
			}
		}

		// Outside the kept range.
		p.fillRect(QRect(0, 0, int(t2x(m_in)), height()), QColor(0, 0, 0, 150));
		p.fillRect(QRect(int(t2x(m_out)), 0, width() - int(t2x(m_out)), height()), QColor(0, 0, 0, 150));
		// Handles.
		for (const double t : {m_in, m_out}) {
			const int x = int(t2x(t));
			p.setPen(QPen(QColor(124, 58, 237), 3));
			p.drawLine(x, 0, x, height());
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(124, 58, 237));
			p.drawRoundedRect(QRect(t == m_in ? x - 1 : x - 9, 2, 10, 18), 3, 3);
		}
		// Playhead.
		const int hx = int(t2x(m_pos));
		p.setPen(QPen(Qt::white, 2));
		p.drawLine(hx, 0, hx, height());
	}
	void mousePressEvent(QMouseEvent *e) override
	{
		const double x = e->position().x();
		if (std::abs(x - t2x(m_in)) <= 8)
			m_drag = In;
		else if (std::abs(x - t2x(m_out)) <= 8)
			m_drag = Out;
		else {
			m_drag = Seek;
			emit seekRequested(x2t(x));
		}
	}
	void mouseMoveEvent(QMouseEvent *e) override
	{
		const double x = e->position().x();
		if (m_drag == None) {
			setCursor(std::abs(x - t2x(m_in)) <= 8 || std::abs(x - t2x(m_out)) <= 8 ? Qt::SizeHorCursor : Qt::ArrowCursor);
			return;
		}
		const double t = std::clamp(x2t(x), 0.0, m_dur);
		if (m_drag == Seek)
			emit seekRequested(t);
		else if (m_drag == In)
			emit rangeEdited(std::min(t, m_out - 0.2), m_out);
		else
			emit rangeEdited(m_in, std::max(t, m_in + 0.2));
	}
	void mouseReleaseEvent(QMouseEvent *) override { m_drag = None; }

private:
	enum Drag { None, Seek, In, Out };
	double t2x(double t) const { return kPad + t / m_dur * (width() - 2 * kPad); }
	double x2t(double x) const { return std::clamp((x - kPad) / std::max(1, width() - 2 * kPad) * m_dur, 0.0, m_dur); }
	double x2px(double seconds) const { return seconds / m_dur * (width() - 2 * kPad); } // pixels per `seconds`

	double m_dur = 1, m_in = 0, m_out = 1, m_pos = 0;
	std::vector<std::vector<float>> m_peaks;
	std::vector<float> m_gain;
	Drag m_drag = None;
};

// ---------------------------------------------------------------------------------------------------

ClipEditor::ClipEditor(const QString &path, const QString &title, const QString &privacy, bool canUpload, QWidget *parent)
	: QDialog(parent), m_path(path)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle("Review clip - " + QFileInfo(path).fileName());
	m_info = ClipMedia::probe(path);
	if (!m_info.ok) {
		QMessageBox::warning(parent, "Review clip", m_info.error);
		return; // the caller checks valid() and deletes us
	}
	QString err;
	m_reader.open(path, 560, 600, &err);
	m_tracks = m_info.audioTracks;
	m_in = 0;
	m_outPoint = m_info.duration;
	buildUi(canUpload, title, privacy);
	m_out.open();
	showFrame(0);
	loadAudio();

	m_timer.setInterval(12);
	connect(&m_timer, &QTimer::timeout, this, &ClipEditor::tick);
}

ClipEditor::~ClipEditor()
{
	m_cancelExport = true;
	if (m_worker) {
		m_worker->wait();
		delete m_worker;
	}
}

void ClipEditor::buildUi(bool canUpload, const QString &title, const QString &privacy)
{
	// ---- Preview + details
	m_video = new QLabel;
	m_video->setAlignment(Qt::AlignCenter);
	m_video->setStyleSheet("background: #000; border-radius: 10px;");
	const double aspect = double(m_info.width) / std::max(1, m_info.height);
	const int vh = aspect < 1 ? 560 : int(560 / aspect), vw = aspect < 1 ? int(560 * aspect) : 560;
	m_video->setFixedSize(std::max(vw, 200), std::max(vh, 200));

	m_title = new QLineEdit(title);
	m_privacy = new QComboBox;
	m_privacy->addItem("Private", "private");
	m_privacy->addItem("Unlisted", "unlisted");
	m_privacy->addItem("Public", "public");
	m_privacy->setCurrentIndex(std::max(0, m_privacy->findData(privacy)));

	m_time = new QLabel("0:00.0");
	m_range = new QLabel;
	m_range->setObjectName("hint");
	m_play = new QPushButton("Play");
	m_play->setEnabled(true);
	m_loop = new QCheckBox("Loop the kept part");
	m_loop->setChecked(true);
	auto *setIn = new QPushButton("Start here  (I)");
	auto *setOut = new QPushButton("End here  (O)");
	auto *reset = new QPushButton("Keep all");
	connect(m_play, &QPushButton::clicked, this, &ClipEditor::togglePlay);
	connect(setIn, &QPushButton::clicked, this, [this] { rangeChanged(std::min(m_pos, m_outPoint - 0.2), m_outPoint); });
	connect(setOut, &QPushButton::clicked, this, [this] { rangeChanged(m_in, std::max(m_pos, m_in + 0.2)); });
	connect(reset, &QPushButton::clicked, this, [this] { rangeChanged(0, m_info.duration); });

	auto *hint = new QLabel("Drag the purple handles on the timeline, or play to the right spot and use Start here / End here.");
	hint->setObjectName("hint");
	hint->setWordWrap(true);
	auto *transport = new QHBoxLayout;
	transport->addWidget(m_play);
	transport->addWidget(m_time);
	transport->addStretch();
	transport->addWidget(m_loop);
	auto *trim = new QHBoxLayout;
	trim->addWidget(setIn);
	trim->addWidget(setOut);
	trim->addWidget(reset);

	auto *side = new QVBoxLayout;
	if (canUpload) {
		side->addWidget(new QLabel("Title"));
		side->addWidget(m_title);
		side->addWidget(new QLabel("Who can watch"));
		side->addWidget(m_privacy);
	}
	side->addSpacing(8);
	side->addLayout(transport);
	side->addLayout(trim);
	side->addWidget(m_range);
	side->addWidget(hint);
	side->addStretch();

	auto *top = new QHBoxLayout;
	top->addWidget(m_video);
	top->addLayout(side, 1);

	// ---- Tracks: controls on the left, waveforms on the right, rows lined up with the lanes.
	m_timeline = new TrackTimeline;
	m_timeline->setup(m_info.duration, m_tracks);
	connect(m_timeline, &TrackTimeline::seekRequested, this, &ClipEditor::seek);
	connect(m_timeline, &TrackTimeline::rangeEdited, this, &ClipEditor::rangeChanged);

	auto *grid = new QGridLayout;
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setSpacing(0);
	grid->setColumnMinimumWidth(0, 250);
	auto *head = new QLabel(m_tracks > 0 ? "AUDIO TRACKS" : "This clip has no audio");
	head->setObjectName("hint");
	head->setFixedHeight(kRulerH);
	head->setContentsMargins(6, 0, 0, 0);
	grid->addWidget(head, 0, 0);
	grid->addWidget(m_timeline, 0, 1, m_tracks + 1, 1);
	const bool singleMix = m_tracks == AudioTracks::kCount;
	for (int t = 0; t < m_tracks; ++t) {
		auto *row = new QWidget;
		row->setFixedHeight(kLaneH);
		auto *h = new QHBoxLayout(row);
		h->setContentsMargins(6, 0, 10, 0);
		h->setSpacing(6);
		auto *mute = new QToolButton;
		mute->setText("M");
		mute->setCheckable(true);
		mute->setToolTip("Mute this track");
		auto *solo = new QToolButton;
		solo->setText("S");
		solo->setCheckable(true);
		solo->setToolTip("Listen to this track alone");
		auto *name = new QLabel(m_tracks == 1 ? QString("Audio") : singleMix ? AudioTracks::name(t) : QString("Track %1").arg(t + 1));
		name->setStyleSheet(QString("color: %1; font-weight: 600;").arg(trackColor(t).name()));
		name->setMinimumWidth(78);
		auto *vol = new QSlider(Qt::Horizontal);
		vol->setRange(0, 200);
		vol->setValue(100);
		vol->setToolTip("Volume of this track in the saved clip");
		auto *pct = new QLabel("100%");
		pct->setMinimumWidth(34);
		pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		h->addWidget(mute);
		h->addWidget(solo);
		h->addWidget(name);
		h->addWidget(vol, 1);
		h->addWidget(pct);
		grid->addWidget(row, t + 1, 0);
		m_mute.push_back(mute);
		m_solo.push_back(solo);
		m_vol.push_back(vol);
		m_volLabel.push_back(pct);
		auto refresh = [this, pct, vol] {
			pct->setText(QString("%1%").arg(vol->value()));
			updateEffectiveGains();
		};
		connect(vol, &QSlider::valueChanged, this, refresh);
		connect(mute, &QToolButton::toggled, this, refresh);
		connect(solo, &QToolButton::toggled, this, refresh);
	}
	// The Mix already contains everything, so by default only it plays; the other tracks are there to
	// rebalance (switch the Mix off, bring in the tracks you want).
	if (m_tracks > 1)
		for (int t = 1; t < m_tracks; ++t)
			m_mute[size_t(t)]->setChecked(true);
	grid->setRowStretch(m_tracks + 1, 1);

	auto *mixHint = new QLabel(m_tracks > 1 ? "Mix already has everything in it. To rebalance, mute Mix and bring in the other tracks, then set each one's volume."
						: QString());
	mixHint->setObjectName("hint");
	mixHint->setWordWrap(true);

	// ---- Footer
	m_status = new QLabel;
	m_status->setObjectName("hint");
	m_progress = new QProgressBar;
	m_progress->setRange(0, 100);
	m_progress->setFixedWidth(160);
	m_progress->hide();
	m_save = new QPushButton("Save edited clip");
	m_saveUpload = new QPushButton("Save and upload to YouTube");
	m_saveUpload->setVisible(canUpload);
	m_saveUpload->setDefault(true);
	m_cancel = new QPushButton("Close");
	connect(m_save, &QPushButton::clicked, this, [this] { startExport(false); });
	connect(m_saveUpload, &QPushButton::clicked, this, [this] { startExport(true); });
	connect(m_cancel, &QPushButton::clicked, this, [this] {
		if (m_exporting)
			m_cancelExport = true;
		else
			close();
	});
	auto *foot = new QHBoxLayout;
	foot->addWidget(m_status, 1);
	foot->addWidget(m_progress);
	foot->addWidget(m_save);
	foot->addWidget(m_saveUpload);
	foot->addWidget(m_cancel);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(16, 14, 16, 12);
	root->setSpacing(10);
	root->addLayout(top);
	root->addLayout(grid);
	root->addWidget(mixHint);
	root->addLayout(foot);

	updateRangeLabel();
	updateEffectiveGains();
}

// ---- Audio loading ----------------------------------------------------------------------------

void ClipEditor::loadAudio()
{
	if (m_tracks == 0)
		return;
	m_status->setText("Loading audio...");
	const QString path = m_path;
	const int tracks = m_tracks;
	QThread *th = QThread::create([this, path, tracks] {
		auto pcm = std::make_shared<std::vector<std::vector<float>>>(size_t(tracks));
		auto peaks = std::make_shared<std::vector<std::vector<float>>>(size_t(tracks));
		for (int t = 0; t < tracks; ++t) {
			QString err;
			ClipMedia::decodeAudio(path, t, &(*pcm)[size_t(t)], &err);
			const auto &p = (*pcm)[size_t(t)];
			std::vector<float> &pk = (*peaks)[size_t(t)];
			constexpr size_t bins = 1600;
			pk.assign(bins, 0.0f);
			const size_t frames = p.size() / 2;
			for (size_t i = 0; i < frames; ++i) {
				float &b = pk[std::min(bins - 1, i * bins / std::max<size_t>(1, frames))];
				b = std::max(b, std::max(std::fabs(p[i * 2]), std::fabs(p[i * 2 + 1])));
			}
		}
		QMetaObject::invokeMethod(this, [this, pcm, peaks] {
			m_pcm = std::move(*pcm);
			for (int t = 0; t < int(peaks->size()); ++t)
				m_timeline->setPeaks(t, std::move((*peaks)[size_t(t)]));
			m_status->clear();
		}, Qt::QueuedConnection);
	});
	connect(th, &QThread::finished, th, &QObject::deleteLater);
	th->start();
}

// ---- Playback ----------------------------------------------------------------------------------

void ClipEditor::updateEffectiveGains()
{
	bool anySolo = false;
	for (auto *s : m_solo)
		anySolo |= s->isChecked();
	m_eff.assign(size_t(m_tracks), 0.0f);
	for (int t = 0; t < m_tracks; ++t) {
		const bool audible = anySolo ? m_solo[size_t(t)]->isChecked() : !m_mute[size_t(t)]->isChecked();
		m_eff[size_t(t)] = audible ? float(m_vol[size_t(t)]->value()) / 100.0f : 0.0f;
	}
	if (m_timeline)
		m_timeline->setGains(m_eff);
}

void ClipEditor::togglePlay()
{
	if (m_playing) {
		m_playing = false;
		m_timer.stop();
		m_out.stop();
		m_play->setText("Play");
		return;
	}
	if (m_pos < m_in || m_pos >= m_outPoint - 0.05)
		m_pos = m_in;
	m_playing = true;
	m_writeSample = qint64(m_pos * kRate);
	m_clockStart = m_pos;
	m_clock.start();
	m_out.stop();
	feedAudio(); // prime the device before it starts, so playback begins cleanly
	m_out.start();
	m_timer.start();
	m_play->setText("Pause");
}

void ClipEditor::seek(double seconds)
{
	m_pos = std::clamp(seconds, 0.0, m_info.duration);
	m_timeline->setPlayhead(m_pos);
	m_time->setText(clock(m_pos));
	showFrame(m_pos);
	if (m_playing) {
		m_writeSample = qint64(m_pos * kRate);
		m_clockStart = m_pos;
		m_clock.start();
		m_out.stop();
		feedAudio();
		m_out.start();
	}
}

void ClipEditor::feedAudio()
{
	const qint64 inS = qint64(m_in * kRate), outS = qint64(m_outPoint * kRate);
	int n = m_out.isOpen() ? std::min(m_out.writable(), 4096) : 0;
	if (n <= 0)
		return;
	std::vector<float> buf(size_t(n) * 2);
	int filled = 0;
	for (; filled < n; ++filled) {
		if (m_writeSample >= outS) {
			if (!m_loop->isChecked())
				break;
			m_writeSample = inS;
		}
		float l = 0, r = 0;
		for (int t = 0; t < int(m_pcm.size()) && t < m_tracks; ++t) {
			const float g = m_eff[size_t(t)];
			const size_t i = size_t(m_writeSample) * 2;
			if (g > 0 && i + 1 < m_pcm[size_t(t)].size()) {
				l += m_pcm[size_t(t)][i] * g;
				r += m_pcm[size_t(t)][i + 1] * g;
			}
		}
		buf[size_t(filled) * 2] = softLimit(l);
		buf[size_t(filled) * 2 + 1] = softLimit(r);
		++m_writeSample;
	}
	m_out.write(buf.data(), filled);
}

void ClipEditor::tick()
{
	if (!m_playing)
		return;
	feedAudio();
	double pos;
	if (m_out.isOpen()) {
		pos = double(m_writeSample - m_out.queued()) / kRate;
		if (pos < m_in) // just wrapped around the loop point
			pos += m_outPoint - m_in;
	} else {
		pos = m_clockStart + m_clock.elapsed() / 1000.0;
	}
	if (pos >= m_outPoint - 0.001) {
		if (m_loop->isChecked() && !m_out.isOpen()) {
			seek(m_in);
			return;
		}
		if (!m_loop->isChecked()) {
			m_pos = m_outPoint;
			togglePlay();
			showFrame(m_pos);
			m_timeline->setPlayhead(m_pos);
			m_time->setText(clock(m_pos));
			return;
		}
	}
	m_pos = pos;
	m_timeline->setPlayhead(pos);
	m_time->setText(clock(pos));
	showFrame(pos);
}

void ClipEditor::showFrame(double seconds)
{
	const QImage img = m_reader.frameAt(seconds);
	if (!img.isNull())
		m_video->setPixmap(QPixmap::fromImage(img).scaled(m_video->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

// ---- Trim ---------------------------------------------------------------------------------------

void ClipEditor::rangeChanged(double in, double out)
{
	m_in = std::clamp(in, 0.0, m_info.duration);
	m_outPoint = std::clamp(out, m_in + 0.1, m_info.duration);
	m_timeline->setRange(m_in, m_outPoint);
	updateRangeLabel();
}

void ClipEditor::updateRangeLabel()
{
	m_range->setText(QString("Keeping %1 to %2  (%3 of %4)").arg(clock(m_in), clock(m_outPoint), clock(m_outPoint - m_in), clock(m_info.duration)));
}

// ---- Export -------------------------------------------------------------------------------------

QString ClipEditor::outputPath() const
{
	const QFileInfo fi(m_path);
	QString base = fi.completeBaseName();
	if (base.endsWith("_edit"))
		base.chop(5);
	for (int n = 1;; ++n) {
		const QString p = fi.absolutePath() + "/" + base + (n == 1 ? "_edit" : QString("_edit%1").arg(n)) + ".mp4";
		if (!QFileInfo::exists(p))
			return p;
	}
}

void ClipEditor::startExport(bool upload)
{
	if (m_exporting)
		return;
	ClipMedia::ExportJob job;
	job.input = m_path;
	job.output = outputPath();
	job.start = m_in;
	job.end = m_outPoint;
	job.gains = m_eff;
	if (m_tracks > 0 && std::none_of(m_eff.begin(), m_eff.end(), [](float g) { return g > 0.001f; }) &&
	    QMessageBox::question(this, "Review clip", "Every audio track is muted, so the saved clip will be silent. Continue?") != QMessageBox::Yes)
		return;

	m_exporting = true;
	m_cancelExport = false;
	m_wantUpload = upload;
	m_exportPath = job.output;
	if (m_playing)
		togglePlay();
	for (auto *w : {static_cast<QWidget *>(m_save), static_cast<QWidget *>(m_saveUpload), static_cast<QWidget *>(m_play)})
		w->setEnabled(false);
	m_cancel->setText("Cancel");
	m_progress->setValue(0);
	m_progress->show();
	m_status->setText("Saving...");

	m_worker = QThread::create([this, job] {
		QString err;
		const bool ok = ClipMedia::exportClip(job,
						      [this](double p) {
							      QMetaObject::invokeMethod(m_progress, [this, p] { m_progress->setValue(int(p * 100)); }, Qt::QueuedConnection);
							      return !m_cancelExport;
						      },
						      &err);
		QMetaObject::invokeMethod(this, [this, ok, err] {
			m_worker->wait();
			delete m_worker;
			m_worker = nullptr;
			m_exporting = false;
			m_progress->hide();
			m_cancel->setText("Close");
			for (auto *w : {static_cast<QWidget *>(m_save), static_cast<QWidget *>(m_saveUpload), static_cast<QWidget *>(m_play)})
				w->setEnabled(true);
			if (!ok) {
				m_status->setText(m_cancelExport ? "Cancelled." : "Could not save: " + err);
				return;
			}
			emit exported(m_exportPath, m_wantUpload, m_title->text().trimmed(), m_privacy->currentData().toString());
			close();
		}, Qt::QueuedConnection);
	});
	m_worker->start();
}

// ---- Window -------------------------------------------------------------------------------------

void ClipEditor::keyPressEvent(QKeyEvent *e)
{
	if (m_title && m_title->hasFocus()) {
		QDialog::keyPressEvent(e);
		return;
	}
	switch (e->key()) {
	case Qt::Key_Space:
		if (!m_exporting)
			togglePlay();
		break;
	case Qt::Key_I: rangeChanged(std::min(m_pos, m_outPoint - 0.2), m_outPoint); break;
	case Qt::Key_O: rangeChanged(m_in, std::max(m_pos, m_in + 0.2)); break;
	case Qt::Key_Left: seek(m_pos - (e->modifiers() & Qt::ShiftModifier ? 5.0 : 1.0)); break;
	case Qt::Key_Right: seek(m_pos + (e->modifiers() & Qt::ShiftModifier ? 5.0 : 1.0)); break;
	default: QDialog::keyPressEvent(e);
	}
}

void ClipEditor::closeEvent(QCloseEvent *e)
{
	if (m_exporting) { // let the export finish or cancel first
		m_cancelExport = true;
		e->ignore();
		return;
	}
	m_timer.stop();
	m_out.stop();
	QDialog::closeEvent(e);
}

#include "ClipEditor.moc"
