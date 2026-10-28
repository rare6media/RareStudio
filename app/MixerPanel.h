#pragma once

#include <obs.h>

#include <QElapsedTimer>
#include <QSlider>
#include <QTimer>
#include <QWidget>

#include <atomic>

class QDoubleSpinBox;
class QMenu;
class QPushButton;
class QToolButton;
class QHBoxLayout;
class QVBoxLayout;
class QLabel;

// Vertical stereo dB meter: two RMS-style bars with held peak markers, scale ticks and a clip latch
// on top (click the meter to clear it).
class LevelMeter : public QWidget {
	Q_OBJECT
public:
	explicit LevelMeter(QWidget *parent = nullptr);

	// Levels in dBFS, as delivered by libobs (-inf = silence).
	void setLevels(const float mag[2], const float peak[2]);
	void advance(); // decay + hold timing; call once per UI tick
	void setMuted(bool muted);

	static constexpr float kFloor = -60.0f;

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;

private:
	float m_shown[2] = {kFloor, kFloor};
	float m_hold[2] = {kFloor, kFloor};
	qint64 m_holdSince[2] = {0, 0};
	float m_in[2] = {kFloor, kFloor};
	float m_inPeak[2] = {kFloor, kFloor};
	bool m_clip = false;
	bool m_muted = false;
	QElapsedTimer m_clock;
	qint64 m_last = 0;
};

// Console-style vertical fader: a lit track, dB scale beside it and a ridged cap. Double click jumps to 0 dB.
class Fader : public QSlider {
	Q_OBJECT
public:
	explicit Fader(QWidget *parent = nullptr) : QSlider(Qt::Vertical, parent) { setFocusPolicy(Qt::WheelFocus); }
	QSize sizeHint() const override { return {48, 160}; }
	QSize minimumSizeHint() const override { return {48, 90}; }
signals:
	void resetRequested();

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override { emit resetRequested(); }

private:
	void moveTo(int y);
	int trackTop() const { return 12; }
	int trackBottom() const { return height() - 12; }
	bool m_drag = false;
};

// One channel strip per audio source, like a mixing desk: name, mute and menu on top, meter beside a
// vertical fader, dB entry underneath (menu: filters, device properties, monitoring, sync offset).
class MixerStrip : public QWidget {
	Q_OBJECT
public:
	MixerStrip(const QString &label, obs_source_t *source, QWidget *parent = nullptr);
	~MixerStrip() override;

	void refresh();                        // UI tick: meter and any change made elsewhere (hotkeys, scripts)
	bool alive() const;                    // source still exists and has not been removed
	obs_source_t *identity() const { return m_ptr; } // compare only, never dereference

signals:
	void filtersRequested(obs_source_t *source);
	void propertiesRequested(obs_source_t *source);

private:
	static void onLevels(void *param, const float magnitude[MAX_AUDIO_CHANNELS],
			     const float peak[MAX_AUDIO_CHANNELS], const float inputPeak[MAX_AUDIO_CHANNELS]);

	void setDb(double db, bool fromUser);
	void syncFromSource();
	void buildMenu();

	obs_source_t *m_ptr;
	obs_weak_source_t *m_weak;
	obs_volmeter_t *m_meter = nullptr;
	std::atomic<float> m_mag[2] = {-60.0f, -60.0f};
	std::atomic<float> m_peak[2] = {-60.0f, -60.0f};
	float m_lastMul = -1.0f;
	bool m_lastMuted = false;

	QLabel *m_name;
	LevelMeter *m_levels;
	Fader *m_fader;
	QDoubleSpinBox *m_db;
	QPushButton *m_mute;
	QToolButton *m_menuBtn;
	QMenu *m_menu;
	QAction *m_filtersAct, *m_syncAct;
};

class MixerPanel : public QWidget {
	Q_OBJECT
public:
	explicit MixerPanel(QWidget *parent = nullptr);
	void addStrip(const QString &label, obs_source_t *source);

	QSize sizeHint() const override { return {420, 290}; }        // a few strips visible by default
	QSize minimumSizeHint() const override { return {120, 230}; } // never smaller than one full strip

signals:
	void filtersRequested(obs_source_t *source);
	void propertiesRequested(obs_source_t *source);

protected:
	void showEvent(QShowEvent *) override;
	void hideEvent(QHideEvent *) override { m_timer.stop(); }

private:
	void syncSources(); // picks up app / media / browser audio added to scenes, drops removed ones

	QHBoxLayout *m_layout;
	QList<MixerStrip *> m_strips;
	QTimer m_timer; // polled only while the panel is visible
	int m_ticks = 0;
};
