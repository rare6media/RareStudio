#pragma once

#include <obs.h>

#include <QDialog>
#include <QMap>

class QFormLayout;

// Builds a settings form from a source's libobs properties (device pickers, window lists, etc.)
// so every OBS source type is configurable without per-source UI code.
class PropertiesDialog : public QDialog {
	Q_OBJECT
public:
	PropertiesDialog(obs_source_t *source, QWidget *parent = nullptr);
	~PropertiesDialog() override;

private:
	void rebuild();
	void apply();

	obs_source_t *m_source;
	obs_properties_t *m_props = nullptr;
	obs_data_t *m_settings = nullptr;
	QFormLayout *m_form = nullptr;
	QMap<QString, QWidget *> m_widgets;
};
