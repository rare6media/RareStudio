#pragma once

#include <memory>

// Plays 48 kHz stereo float audio on the default output device (Windows WASAPI, shared mode), pulled in
// small pieces by whoever owns it. The clip editor's preview uses it as the clock.
class AudioOut {
public:
	AudioOut();
	~AudioOut();

	bool open();     // false if there is no usable output device
	bool isOpen() const;
	void start();
	void stop();     // stops and discards what was queued (for seeking)
	int writable();  // frames that fit right now
	int queued();    // frames written but not yet played
	void write(const float *interleavedStereo, int frames);

private:
	struct Impl;
	std::unique_ptr<Impl> d;
};
