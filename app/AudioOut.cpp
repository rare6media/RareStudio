#include "AudioOut.h"

#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>

#include <algorithm>

struct AudioOut::Impl {
	IAudioClient *client = nullptr;
	IAudioRenderClient *render = nullptr;
	UINT32 bufferFrames = 0;
	bool started = false;
};

AudioOut::AudioOut() : d(new Impl) {}

AudioOut::~AudioOut()
{
	if (d->client) {
		d->client->Stop();
		d->client->Release();
	}
	if (d->render)
		d->render->Release();
}

bool AudioOut::open()
{
	if (d->client)
		return true;
	IMMDeviceEnumerator *en = nullptr;
	IMMDevice *dev = nullptr;
	bool ok = false;
	if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&en))) &&
	    SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) &&
	    SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&d->client)))) {
		WAVEFORMATEX fmt = {};
		fmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		fmt.nChannels = 2;
		fmt.nSamplesPerSec = 48000;
		fmt.wBitsPerSample = 32;
		fmt.nBlockAlign = 8;
		fmt.nAvgBytesPerSec = 48000 * 8;
		// Shared mode converts to whatever the device really runs at.
		if (SUCCEEDED(d->client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
						    2000000, 0, &fmt, nullptr)) &&
		    SUCCEEDED(d->client->GetBufferSize(&d->bufferFrames)) &&
		    SUCCEEDED(d->client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void **>(&d->render))))
			ok = true;
	}
	if (dev)
		dev->Release();
	if (en)
		en->Release();
	if (!ok && d->client) {
		d->client->Release();
		d->client = nullptr;
	}
	return ok;
}

bool AudioOut::isOpen() const
{
	return d->render != nullptr;
}

void AudioOut::start()
{
	if (d->client && !d->started)
		d->started = SUCCEEDED(d->client->Start());
}

void AudioOut::stop()
{
	if (!d->client)
		return;
	d->client->Stop();
	d->client->Reset();
	d->started = false;
}

int AudioOut::queued()
{
	UINT32 pad = 0;
	if (d->client && SUCCEEDED(d->client->GetCurrentPadding(&pad)))
		return int(pad);
	return 0;
}

int AudioOut::writable()
{
	return d->client ? int(d->bufferFrames) - queued() : 0;
}

void AudioOut::write(const float *samples, int frames)
{
	if (!d->render || frames <= 0)
		return;
	frames = std::min(frames, writable());
	BYTE *dst = nullptr;
	if (frames > 0 && SUCCEEDED(d->render->GetBuffer(UINT32(frames), &dst))) {
		memcpy(dst, samples, size_t(frames) * 2 * sizeof(float));
		d->render->ReleaseBuffer(UINT32(frames), 0);
	}
}
