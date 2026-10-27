#pragma once

#include <obs.h>

#include <QTimer>
#include <QWidget>

#include <atomic>

class QProgressBar;
class QSlider;
class QCheckBox;
class QVBoxLayout;

// One strip per audio source: level meter, volume fader, mute.
class MixerStrip : public QWidget {
	Q_OBJECT
public:
	MixerStrip(const QString &label, obs_source_t *source, QWidget *parent = nullptr);
	~MixerStrip() override;

	void refreshMeter();

signals:
	void filtersRequested(obs_source_t *source);

private:
	static void onLevels(void *param, const float magnitude[MAX_AUDIO_CHANNELS],
			     const float peak[MAX_AUDIO_CHANNELS], const float inputPeak[MAX_AUDIO_CHANNELS]);

	obs_source_t *m_source;
	obs_volmeter_t *m_meter = nullptr;
	std::atomic<float> m_level{-60.0f};
	QProgressBar *m_bar;
	QSlider *m_fader;
	QCheckBox *m_mute;
};

class MixerPanel : public QWidget {
	Q_OBJECT
public:
	explicit MixerPanel(QWidget *parent = nullptr);
	void addStrip(const QString &label, obs_source_t *source);

signals:
	void filtersRequested(obs_source_t *source); // noise suppression, gain, compressor, ...

protected:
	void showEvent(QShowEvent *) override { m_timer.start(); }
	void hideEvent(QHideEvent *) override { m_timer.stop(); }

private:
	QVBoxLayout *m_layout;
	QList<MixerStrip *> m_strips;
	QTimer m_timer; // polled only while the panel is visible
};
