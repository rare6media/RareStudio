#pragma once

#include <obs.h>

#include <QList>
#include <QPair>
#include <QString>

// One video encoder and one audio encoder shared by every output (each stream destination,
// the recording and the replay buffer). Encoding once and fanning out is what keeps CPU/GPU
// use flat no matter how many things are running.
class EncoderPool {
public:
	static EncoderPool &instance();

	// Reference counted: the first acquire creates the encoders, the last release frees them.
	bool acquire(QString *error);
	void release();

	obs_encoder_t *video() const { return m_video; }
	obs_encoder_t *audio() const { return m_audio; }
	QString videoName() const { return m_videoName; }

	// The encoder id to use: your Settings choice, else the best available. Empty if none.
	static QString preferredVideoEncoder();
	// (id, display name) for every H.264 encoder libobs has registered.
	static QList<QPair<QString, QString>> availableVideoEncoders();

private:
	EncoderPool() = default;

	int m_refs = 0;
	obs_encoder_t *m_video = nullptr;
	obs_encoder_t *m_audio = nullptr;
	QString m_videoName;
};
