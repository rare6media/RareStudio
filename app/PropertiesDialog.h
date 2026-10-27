#pragma once

#include <obs.h>

#include <QDialog>
#include <QMap>

class QFormLayout;

class QTimer;

// Builds a settings form from a source's libobs properties (device pickers, window lists, etc.)
// so every OBS source type is configurable without per-source UI code. Changes apply to the live
// source as you make them (preview in the canvas / mixer meters); Cancel restores the original.
class PropertiesDialog : public QDialog {
	Q_OBJECT
public:
	PropertiesDialog(obs_source_t *source, QWidget *parent = nullptr);
	~PropertiesDialog() override;

private:
	void rebuild();
	void collect();      // widgets -> m_settings
	void apply();        // collect + push to the source
	void hook(QWidget *w);
	void restoreOriginal();

	obs_source_t *m_source;
	obs_properties_t *m_props = nullptr;
	obs_data_t *m_settings = nullptr;
	obs_data_t *m_original = nullptr;
	QTimer *m_live = nullptr;
	QFormLayout *m_form = nullptr;
	QMap<QString, QWidget *> m_widgets;
};
