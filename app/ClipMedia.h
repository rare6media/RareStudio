#pragma once

#include <QImage>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

// Reading and exporting clips with the FFmpeg libraries that ship with libobs. Nothing here touches
// the UI; the clip editor drives it.
namespace ClipMedia {

constexpr int kRate = 48000; // sample rate of decoded audio (interleaved stereo float)

struct Info {
	bool ok = false;
	QString error;
	double duration = 0; // seconds
	int width = 0, height = 0;
	double fps = 30;
	int audioTracks = 0;
};
Info probe(const QString &path);

// Decodes audio stream number `track` (0-based among the file's audio streams) to interleaved stereo float.
bool decodeAudio(const QString &path, int track, std::vector<float> *pcm, QString *error);

// Video frames for the preview, scaled to fit maxW x maxH. Cheap to call for the next frame; a jump
// seeks to the keyframe before it and catches up.
class VideoReader {
public:
	VideoReader();
	~VideoReader();
	bool open(const QString &path, int maxW, int maxH, QString *error);
	QImage frameAt(double seconds); // null image past the end or on error
	double duration() const;

private:
	struct Impl;
	std::unique_ptr<Impl> d;
};

struct ExportJob {
	QString input, output;
	double start = 0, end = 0;  // seconds; end <= 0 means to the end
	std::vector<float> gains;   // per audio track; 0 leaves the track out. All zero = a clip without sound
};
// Writes an MP4: the chosen range, the video re-encoded (or copied untouched when the whole clip is
// kept) and the audio tracks mixed down at their gains into one AAC track. `progress` gets 0..1 and
// returns false to cancel.
bool exportClip(const ExportJob &job, const std::function<bool(double)> &progress, QString *error);

} // namespace ClipMedia
