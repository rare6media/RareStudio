#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// A named, configured transition (e.g. "Whip Pan", "My Stinger"). Several can share a type.
struct TransitionDef {
	QString name;
	QString type;         // libobs source id: fade_transition, rare_whip, stinger_transition, ...
	QByteArray settings;  // obs_data JSON
	int ms = 500;         // length (ignored by stingers, which last as long as their video)
};

// Owns the list of transitions, which one is active, and per-scene overrides.
class TransitionManager {
public:
	static TransitionManager &instance();

	void load(); // call after the engine (and RareTransitions) are registered
	void save() const;

	const QList<TransitionDef> &defs() const { return m_defs; }
	QStringList names() const;
	TransitionDef *find(const QString &name);

	QString current() const { return m_current; }
	void setCurrent(const QString &name); // installs it right away
	void setMs(const QString &name, int ms);

	bool add(const TransitionDef &def); // false if the name is taken
	void remove(const QString &name);
	bool rename(const QString &from, const QString &to);

	// Per-scene override: scene -> transition name ("" = use the current one).
	QString overrideFor(const QString &scene) const;
	void setOverride(const QString &scene, const QString &name);

	// ObsCore calls this just before a scene goes on air.
	void applyFor(const QString &scene);
	// Writes the live transition's settings back into its definition (after the properties dialog).
	void captureCurrentSettings();

private:
	TransitionManager() = default;
	void install(const TransitionDef &def);
	void seedMissing();

	QList<TransitionDef> m_defs;
	QString m_current;
};
