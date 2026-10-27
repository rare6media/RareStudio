#include "Autosave.h"

#include <obs.h>

#include <QCoreApplication>
#include <QPointer>
#include <QThread>

namespace {

QPointer<Autosave> g_instance;

// source_save is left out on purpose: it fires while saving and would loop.
const char *const kSignals[] = {"source_create", "source_remove", "source_update", "source_rename",
				"source_volume", "source_filter_add", "source_filter_remove"};

} // namespace

Autosave::Autosave(QObject *parent) : QObject(parent)
{
	m_debounce.setSingleShot(true);
	m_debounce.setInterval(400);
	connect(&m_debounce, &QTimer::timeout, this, &Autosave::run);
}

Autosave *Autosave::instance()
{
	if (!g_instance && qApp)
		g_instance = new Autosave(qApp);
	return g_instance;
}

void Autosave::touch()
{
	Autosave *self = g_instance;
	if (!self)
		return;
	if (QThread::currentThread() == self->thread()) {
		if (!self->m_saving) // saving itself can emit source signals; that is not a new change
			self->m_debounce.start();
		return;
	}
	QMetaObject::invokeMethod(self, [self] { self->m_debounce.start(); }, Qt::QueuedConnection);
}

void Autosave::setSaver(std::function<void()> save)
{
	m_save = std::move(save);
}

void Autosave::run()
{
	if (!m_save || m_saving)
		return;
	m_saving = true;
	m_save();
	m_saving = false;
}

void Autosave::flush()
{
	if (m_debounce.isActive()) {
		m_debounce.stop();
		run();
	}
}

void Autosave::onObsSignal(void *, calldata *cd)
{
	// Private sources (transitions, the Shorts canvas) are rebuilt by code, not edited by you.
	if (obs_source_t *src = static_cast<obs_source_t *>(calldata_ptr(cd, "source")))
		if (obs_obj_is_private(src))
			return;
	touch();
}

void Autosave::hookObs()
{
	if (m_hooked)
		return;
	signal_handler_t *sh = obs_get_signal_handler();
	for (const char *s : kSignals)
		signal_handler_connect(sh, s, &Autosave::onObsSignal, nullptr);
	m_hooked = true;
}

void Autosave::unhookObs()
{
	if (!m_hooked)
		return;
	signal_handler_t *sh = obs_get_signal_handler();
	for (const char *s : kSignals)
		signal_handler_disconnect(sh, s, &Autosave::onObsSignal, nullptr);
	m_hooked = false;
}
