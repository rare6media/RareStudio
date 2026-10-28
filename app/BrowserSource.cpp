#include "BrowserSource.h"

#include <obs.h>
#include <util/platform.h>

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QMetaObject>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <algorithm>
#include <memory>
#include <mutex>

namespace {

struct Config {
	QString url = "https://obsproject.com/browser-source";
	int width = 800;
	int height = 600;
	int fps = 30;
	QString css = "body { background-color: rgba(0, 0, 0, 0); margin: 0px auto; overflow: hidden; }";
	bool muted = false;
};

// Everything libobs threads touch lives here, behind one lock; the page itself only ever lives on the UI
// thread (BrowserHost). Frames are pushed while holding the lock so destroy() can never free the source
// underneath a push.
struct State {
	std::mutex lock;
	obs_source_t *source = nullptr;
	Config cfg;
};

class BrowserHost : public QObject {
public:
	explicit BrowserHost(std::shared_ptr<State> state) : m_state(std::move(state))
	{
		m_view = new QWebEngineView;
		m_view->setAttribute(Qt::WA_DontShowOnScreen); // rendered, never shown
		m_view->setAttribute(Qt::WA_TranslucentBackground);
		m_view->setWindowFlag(Qt::Tool);
		m_page = new QWebEnginePage(profile(), m_view);
		m_page->setBackgroundColor(Qt::transparent);
		m_page->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
		m_page->settings()->setAttribute(QWebEngineSettings::ShowScrollBars, false);
		m_view->setPage(m_page);
		connect(m_page, &QWebEnginePage::loadFinished, this, [this] { grabFrame(); });

		m_timer.setTimerType(Qt::PreciseTimer);
		connect(&m_timer, &QTimer::timeout, this, [this] { grabFrame(); });
		apply();
	}

	~BrowserHost() override { delete m_view; }

	void apply()
	{
		Config c;
		{
			std::lock_guard g(m_state->lock);
			c = m_state->cfg;
		}
		const bool resized = c.width != m_cfg.width || c.height != m_cfg.height;
		const bool reload = c.url != m_cfg.url || c.css != m_cfg.css || !m_started;
		m_cfg = c;
		m_started = true;

		m_view->resize(m_cfg.width, m_cfg.height);
		m_view->show();
		m_page->setAudioMuted(m_cfg.muted);
		m_timer.setInterval(std::max(1, 1000 / std::clamp(m_cfg.fps, 1, 60)));
		if (!m_timer.isActive())
			m_timer.start();

		if (reload) {
			m_page->scripts().clear();
			QWebEngineScript css;
			css.setName("rarestudio-css");
			css.setInjectionPoint(QWebEngineScript::DocumentReady);
			css.setWorldId(QWebEngineScript::ApplicationWorld);
			QString js = QString("(function(){var s=document.createElement('style');s.textContent=%1;"
					     "document.head.appendChild(s);})();")
					     .arg(quoted(m_cfg.css));
			css.setSourceCode(js);
			m_page->scripts().insert(css);
			m_page->load(QUrl::fromUserInput(m_cfg.url));
		} else if (resized) {
			m_page->triggerAction(QWebEnginePage::Reload);
		}
	}

	// The scene holding this page went on air: pages (countdowns, intros) can restart themselves.
	void goLive() { m_page->runJavaScript("window.dispatchEvent(new Event('rarestudio-live'))"); }

	void reload() { m_page->triggerAction(QWebEnginePage::ReloadAndBypassCache); }

	// A source nobody sees (not in the preview or on air) is frozen: no timers, no painting.
	void setShowing(bool on)
	{
		m_page->setLifecycleState(on ? QWebEnginePage::LifecycleState::Active : QWebEnginePage::LifecycleState::Frozen);
		if (on)
			m_timer.start();
		else
			m_timer.stop();
	}

private:
	static QString quoted(const QString &s)
	{
		QString out = "'";
		for (const QChar c : s) {
			if (c == '\\' || c == '\'')
				out += '\\';
			if (c == '\n')
				out += "\\n";
			else if (c != '\r')
				out += c;
		}
		return out + "'";
	}

	static QWebEngineProfile *profile()
	{
		static QWebEngineProfile *p = [] {
			auto *prof = new QWebEngineProfile("browsersource", QCoreApplication::instance());
			prof->setPersistentStoragePath(qEnvironmentVariable("APPDATA") + "/RareStudio/browser");
			prof->setCachePath(qEnvironmentVariable("APPDATA") + "/RareStudio/browser/cache");
			return prof;
		}();
		return p;
	}

	void grabFrame()
	{
		if (!m_view || m_view->width() <= 0)
			return;
		QImage img = m_view->grab().toImage();
		if (img.isNull())
			return;
		if (img.size() != QSize(m_cfg.width, m_cfg.height)) // a scaled UI renders at a higher pixel ratio
			img = img.scaled(m_cfg.width, m_cfg.height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		img = img.convertToFormat(QImage::Format_ARGB32); // BGRA bytes, straight alpha

		obs_source_frame frame = {};
		frame.width = uint32_t(img.width());
		frame.height = uint32_t(img.height());
		frame.format = VIDEO_FORMAT_BGRA;
		frame.linesize[0] = uint32_t(img.bytesPerLine());
		frame.data[0] = img.bits();
		frame.timestamp = os_gettime_ns();
		frame.full_range = true;
		video_format_get_parameters(VIDEO_CS_DEFAULT, VIDEO_RANGE_FULL, frame.color_matrix, frame.color_range_min,
					    frame.color_range_max);

		std::lock_guard g(m_state->lock);
		if (m_state->source)
			obs_source_output_video(m_state->source, &frame);
	}

	std::shared_ptr<State> m_state;
	QWebEngineView *m_view = nullptr;
	QWebEnginePage *m_page = nullptr;
	QTimer m_timer;
	Config m_cfg;
	bool m_started = false;
};

// What libobs holds as the source's data pointer.
struct Data {
	std::shared_ptr<State> state = std::make_shared<State>();
	BrowserHost *host = nullptr; // read and written on the UI thread only
};

// Queues work onto the UI thread without ever blocking a libobs thread.
template<class F> void onUi(F &&f)
{
	QMetaObject::invokeMethod(QCoreApplication::instance(), std::forward<F>(f), Qt::QueuedConnection);
}

void readSettings(Data *d, obs_data_t *s)
{
	std::lock_guard g(d->state->lock);
	Config &c = d->state->cfg;
	c.url = QString::fromUtf8(obs_data_get_string(s, "url"));
	c.width = int(obs_data_get_int(s, "width"));
	c.height = int(obs_data_get_int(s, "height"));
	c.fps = int(obs_data_get_int(s, "fps"));
	c.css = QString::fromUtf8(obs_data_get_string(s, "css"));
	c.muted = obs_data_get_bool(s, "mute_page");
}

const char *getName(void *)
{
	return "Browser";
}

void getDefaults(obs_data_t *s)
{
	const Config c;
	obs_data_set_default_string(s, "url", c.url.toUtf8().constData());
	obs_data_set_default_int(s, "width", c.width);
	obs_data_set_default_int(s, "height", c.height);
	obs_data_set_default_int(s, "fps", c.fps);
	obs_data_set_default_string(s, "css", c.css.toUtf8().constData());
	obs_data_set_default_bool(s, "mute_page", false);
}

obs_properties_t *getProperties(void *)
{
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_text(p, "url", "URL", OBS_TEXT_DEFAULT);
	obs_properties_add_int(p, "width", "Width", 1, 7680, 1);
	obs_properties_add_int(p, "height", "Height", 1, 4320, 1);
	obs_property_t *fps = obs_properties_add_int(p, "fps", "Frame rate", 1, 60, 1);
	obs_property_int_set_suffix(fps, " fps");
	obs_properties_add_text(p, "css", "Custom CSS", OBS_TEXT_MULTILINE);
	obs_properties_add_bool(p, "mute_page", "Mute the page's sound");
	obs_properties_add_button2(
		p, "refresh", "Refresh page",
		[](obs_properties_t *, obs_property_t *, void *data) {
			if (auto *d = static_cast<Data *>(data))
				onUi([d] {
					if (d->host)
						d->host->reload();
				});
			return false;
		},
		nullptr);
	return p;
}

void *create(obs_data_t *settings, obs_source_t *source)
{
	auto *d = new Data;
	d->state->source = source;
	readSettings(d, settings);
	// The host is made on the UI thread; until it exists nothing is drawn. Queued work runs in order, so
	// every later update/show/destroy finds it there.
	onUi([d] { d->host = new BrowserHost(d->state); });
	return d;
}

void destroy(void *data)
{
	auto *d = static_cast<Data *>(data);
	{
		std::lock_guard g(d->state->lock); // after this no frame can reach the source
		d->state->source = nullptr;
	}
	// Queued after create/update/show, so it runs last; d goes with the host.
	onUi([d] {
		delete d->host;
		delete d;
	});
}

void update(void *data, obs_data_t *settings)
{
	auto *d = static_cast<Data *>(data);
	readSettings(d, settings);
	onUi([d] {
		if (d->host)
			d->host->apply();
	});
}

void showing(void *data, bool on)
{
	auto *d = static_cast<Data *>(data);
	onUi([d, on] {
		if (d->host)
			d->host->setShowing(on);
	});
}

uint32_t getWidth(void *data)
{
	auto *d = static_cast<Data *>(data);
	std::lock_guard g(d->state->lock);
	return uint32_t(d->state->cfg.width);
}

uint32_t getHeight(void *data)
{
	auto *d = static_cast<Data *>(data);
	std::lock_guard g(d->state->lock);
	return uint32_t(d->state->cfg.height);
}

} // namespace

void BrowserSource::registerSource()
{
	obs_source_info info = {};
	info.id = "browser_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE;
	info.get_name = getName;
	info.create = create;
	info.destroy = destroy;
	info.update = update;
	info.get_defaults = getDefaults;
	info.get_properties = getProperties;
	info.get_width = getWidth;
	info.get_height = getHeight;
	info.show = [](void *d) { showing(d, true); };
	info.hide = [](void *d) { showing(d, false); };
	info.activate = [](void *data) {
		auto *d = static_cast<Data *>(data);
		onUi([d] {
			if (d->host)
				d->host->goLive();
		});
	};
	info.icon_type = OBS_ICON_TYPE_BROWSER;
	obs_register_source(&info);
}
