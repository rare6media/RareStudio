#pragma once

#include <obs.h>

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;

// Add, remove, reorder, enable and configure the filters on a source. Audio sources get the curated
// voice/dynamics set plus one-click recommended chains; video sources get colour, keying and layout.
class FiltersDialog : public QDialog {
	Q_OBJECT
public:
	FiltersDialog(obs_source_t *source, QWidget *parent = nullptr);
	~FiltersDialog() override;

private:
	void refresh(const QString &select = QString());
	obs_source_t *currentFilter() const; // addref'd, caller releases
	void addFilter(const QString &typeId, const QString &name);
	void removeFilter();
	void moveFilter(obs_order_movement dir);
	void editFilter();
	void updateHint();

	obs_source_t *m_source;
	bool m_audio;
	QListWidget *m_list;
	QLabel *m_hint;
};
