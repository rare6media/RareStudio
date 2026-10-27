#pragma once

#include <obs.h>

#include <QDialog>

class QComboBox;
class QListWidget;

// Add, remove, reorder, enable and configure the filters on a source (video or audio):
// chroma key, color correction, noise suppression, gain, compressor, noise gate, and so on.
class FiltersDialog : public QDialog {
	Q_OBJECT
public:
	FiltersDialog(obs_source_t *source, QWidget *parent = nullptr);
	~FiltersDialog() override;

private:
	void refresh(const QString &select = QString());
	obs_source_t *currentFilter() const; // addref'd, caller releases
	void addFilter();
	void removeFilter();
	void moveFilter(obs_order_movement dir);
	void editFilter();

	obs_source_t *m_source;
	QListWidget *m_list;
	QComboBox *m_types;
};
