#pragma once

#include <QDialog>

class QListWidget;
class QTableWidget;

// Manage transitions and stingers, tune each one, and choose per-scene overrides.
class TransitionsDialog : public QDialog {
	Q_OBJECT
public:
	explicit TransitionsDialog(QWidget *parent = nullptr);

signals:
	void changed(); // the list or the current transition changed; refresh the toolbar

private:
	void refresh();
	void addTransition();
	void addStinger();
	void duplicate();
	void rename();
	void remove();
	void configure();
	void makeCurrent();
	void buildOverrides();
	QString selectedName() const;

	QListWidget *m_list;
	QTableWidget *m_overrides;
};
