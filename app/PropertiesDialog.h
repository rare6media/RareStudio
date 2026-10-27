#pragma once

#include <obs.h>

#include <QDialog>

class QFormLayout;
class QScrollArea;
class QTimer;
class QVBoxLayout;

// Builds a settings form from a source's libobs properties (device pickers, window lists, etc.)
// so every OBS source type is configurable without per-source UI code. Changes apply to the live
// source as you make them (preview in the canvas / mixer meters); Cancel restores the original.
// Fields that depend on each other (e.g. a media source's "Local File" box swapping the file picker
// for a URL / stream input) are re-laid out through libobs' modified callbacks, just like OBS.
class PropertiesDialog : public QDialog {
	Q_OBJECT
public:
	PropertiesDialog(obs_source_t *source, QWidget *parent = nullptr);
	~PropertiesDialog() override;

private:
	void rebuild();                                // (re)creates the whole form from m_props + m_settings
	void addProperties(obs_properties_t *props, QFormLayout *form);
	QWidget *makeWidget(obs_property_t *p, bool *ownRow);
	void edited(const QByteArray &name, bool applyNow = true); // a widget wrote m_settings
	void apply();                                  // push m_settings to the source
	void restoreOriginal();

	obs_source_t *m_source = nullptr; // held reference
	obs_properties_t *m_props = nullptr;
	obs_data_t *m_settings = nullptr;
	obs_data_t *m_original = nullptr;
	QTimer *m_live = nullptr;
	QScrollArea *m_scroll = nullptr;
	bool m_rebuildQueued = false;
};
