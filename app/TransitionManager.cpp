#include "TransitionManager.h"

#include "ObsCore.h"
#include "RareTransitions.h"

#include <QHash>
#include <QSettings>

#include <algorithm>

TransitionManager &TransitionManager::instance()
{
	static TransitionManager m;
	return m;
}

QStringList TransitionManager::names() const
{
	QStringList out;
	for (const TransitionDef &d : m_defs)
		out << d.name;
	return out;
}

TransitionDef *TransitionManager::find(const QString &name)
{
	for (TransitionDef &d : m_defs)
		if (d.name == name)
			return &d;
	return nullptr;
}

// Adds any built-in or RareStudio transition that isn't in the list yet (new releases bring
// new transitions; they appear without disturbing the ones you already configured).
void TransitionManager::seedMissing()
{
	struct Seed {
		const char *name, *type;
		int ms;
	};
	const Seed builtin[] = {
		{"Cut", "cut_transition", 0},
		{"Fade", "fade_transition", 300},
		{"Swipe", "swipe_transition", 500},
		{"Slide", "slide_transition", 500},
		{"Fade to Color", "fade_to_color_transition", 500},
		{"Luma Wipe", "luma_wipe_transition", 600},
		{"Stinger", "stinger_transition", 1500},
	};
	auto have = [this](const QString &type, const QString &name) {
		for (const TransitionDef &d : m_defs)
			if (d.type == type && d.name == name)
				return true;
		return false;
	};
	for (const Seed &s : builtin)
		if (obs_source_get_display_name(s.type) && !have(s.type, s.name))
			m_defs.append({s.name, s.type, {}, s.ms});

	const QHash<QString, int> lengths = {{"rare_smooth", 600},  {"rare_zoom_blur", 700}, {"rare_whip", 600},
					     {"rare_glitch", 700},  {"rare_iris", 800},      {"rare_spin", 800},
					     {"rare_pixelate", 700}, {"rare_ripple", 900},   {"rare_flash", 600},
					     {"rare_blur_dissolve", 700}, {"rare_clock", 800}, {"rare_stinger_brand", 1400}};
	for (const RareTransitions::Info &t : RareTransitions::list()) {
		bool present = false;
		for (const TransitionDef &d : m_defs)
			present |= d.type == t.id && d.name == t.name;
		if (!present)
			m_defs.prepend({t.name, t.id, {}, lengths.value(t.id, 700)}); // ours first: they're the headline
	}
}

void TransitionManager::load()
{
	QSettings s;
	const int n = s.beginReadArray("transitions");
	for (int i = 0; i < n; ++i) {
		s.setArrayIndex(i);
		m_defs.append({s.value("name").toString(), s.value("type").toString(), s.value("settings").toByteArray(),
			       s.value("ms", 500).toInt()});
	}
	s.endArray();
	seedMissing();
	// Anything whose type this libobs doesn't know (e.g. a plugin that went away) is dropped.
	m_defs.erase(std::remove_if(m_defs.begin(), m_defs.end(),
				    [](const TransitionDef &d) { return !obs_source_get_display_name(d.type.toUtf8().constData()); }),
		     m_defs.end());

	m_current = s.value("transition/current", "Smooth Dissolve (eased)").toString();
	if (!find(m_current) && !m_defs.isEmpty())
		m_current = m_defs.first().name;
	if (TransitionDef *d = find(m_current))
		install(*d);

	ObsCore::instance().setTransitionHook([](obs_scene_t *scene) {
		if (scene)
			TransitionManager::instance().applyFor(QString::fromUtf8(obs_source_get_name(obs_scene_get_source(scene))));
	});
}

void TransitionManager::save() const
{
	QSettings s;
	s.beginWriteArray("transitions", m_defs.size());
	for (int i = 0; i < m_defs.size(); ++i) {
		s.setArrayIndex(i);
		s.setValue("name", m_defs[i].name);
		s.setValue("type", m_defs[i].type);
		s.setValue("settings", m_defs[i].settings);
		s.setValue("ms", m_defs[i].ms);
	}
	s.endArray();
	s.setValue("transition/current", m_current);
}

void TransitionManager::install(const TransitionDef &def)
{
	// RARESTUDIO_TRANSITION_SLOWMO=4 stretches every transition 4x (used by automated visual tests).
	static const double slow = qEnvironmentVariable("RARESTUDIO_TRANSITION_SLOWMO", "1").toDouble();
	ObsCore::instance().setTransition(def.type, def.settings, int(def.ms * (slow > 0.0 ? slow : 1.0)));
}

void TransitionManager::setCurrent(const QString &name)
{
	if (TransitionDef *d = find(name)) {
		m_current = name;
		install(*d);
		save();
	}
}

void TransitionManager::setMs(const QString &name, int ms)
{
	if (TransitionDef *d = find(name)) {
		d->ms = ms;
		if (name == m_current)
			install(*d);
		save();
	}
}

bool TransitionManager::add(const TransitionDef &def)
{
	if (def.name.isEmpty() || find(def.name))
		return false;
	m_defs.append(def);
	save();
	return true;
}

void TransitionManager::remove(const QString &name)
{
	for (int i = 0; i < m_defs.size(); ++i)
		if (m_defs[i].name == name && m_defs.size() > 1) {
			m_defs.removeAt(i);
			break;
		}
	if (!find(m_current) && !m_defs.isEmpty())
		setCurrent(m_defs.first().name);
	save();
}

bool TransitionManager::rename(const QString &from, const QString &to)
{
	TransitionDef *d = find(from);
	if (!d || to.isEmpty() || find(to))
		return false;
	d->name = to;
	if (m_current == from)
		m_current = to;
	QSettings s; // keep scene overrides pointing at the renamed transition
	s.beginGroup("transition/override");
	for (const QString &k : s.childKeys())
		if (s.value(k).toString() == from)
			s.setValue(k, to);
	s.endGroup();
	save();
	return true;
}

QString TransitionManager::overrideFor(const QString &scene) const
{
	return QSettings().value("transition/override/" + scene).toString();
}

void TransitionManager::setOverride(const QString &scene, const QString &name)
{
	QSettings s;
	if (name.isEmpty())
		s.remove("transition/override/" + scene);
	else
		s.setValue("transition/override/" + scene, name);
}

void TransitionManager::applyFor(const QString &scene)
{
	const QString over = overrideFor(scene);
	if (TransitionDef *d = find(over.isEmpty() ? m_current : over))
		install(*d);
	else if (TransitionDef *cur = find(m_current))
		install(*cur);
}

void TransitionManager::captureCurrentSettings()
{
	obs_source_t *src = ObsCore::instance().transitionSource();
	TransitionDef *d = find(m_current);
	if (!src || !d)
		return;
	obs_data_t *s = obs_source_get_settings(src);
	d->settings = QByteArray(obs_data_get_json(s));
	obs_data_release(s);
	save();
}
