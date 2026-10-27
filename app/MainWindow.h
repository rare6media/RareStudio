#pragma once

#include "MultiStream.h"
#include "Recorder.h"
#include "VerticalClip.h"

#include <util/platform.h>

#include <QMainWindow>
#include <QTimer>

class ClipsPanel;
class HotkeyManager;
class MixerPanel;
class QMenu;
class PreviewWidget;
class QListWidget;
class QSplitter;
class QPushButton;
class QLabel;
class QTableWidget;
class Updater;

class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	MainWindow();
	~MainWindow() override;

protected:
	void closeEvent(QCloseEvent *e) override;
	void changeEvent(QEvent *e) override;

private:
	void buildToolbar();
	void buildScenesDock();
	void showItemMenu(const QPoint &globalPos);
	void transformSelected();
	void filtersSelected();
	void setStudio(bool on);
	void showSceneMenu(const QPoint &globalPos);
	void renameScene();
	void duplicateScene();
	void removeScene();
	void renameSource();
	void toggleLockSelected();
	void checkForUpdates();
	void buildClipsDock();
	void setReplayUi(bool armed);
	void refreshStreamKeys(bool announce);
	void upsertDestination(const QString &name, const QString &server, const QString &key);
	void createTwitchClip();
	void onShortSaved(const QString &path);
	void uploadShort(int panelId, const QString &path);
	void toggleRecord();
	void toggleReplay();
	void saveClip();
	void onHotkey(int action);
	void moveSelected(obs_order_movement dir);
	void selectInList(const QString &name);
	void updateStats();
	void openSettings();
	void buildSourcesDock();
	void buildMixerDock();
	void buildStreamDock();
	void buildWebDocks();

	void addScene();
	void addSourceDialog();
	void editSelectedSource();
	void removeSelectedSource();
	void refreshSources();

	void addDestination(const QString &name, const QString &server, const QString &key, bool enabled);
	QList<Destination> destinations() const;
	void saveDestinations() const;
	void loadDestinations();
	void toggleStream();

	PreviewWidget *m_preview = nullptr;  // editor
	PreviewWidget *m_program = nullptr;  // Studio Mode: what is on air
	QWidget *m_programPane = nullptr;
	QWidget *m_previewPane = nullptr;
	QWidget *m_previewHead = nullptr;
	QSplitter *m_split = nullptr;
	QPushButton *m_studioBtn = nullptr;
	QListWidget *m_scenes = nullptr;
	QListWidget *m_sources = nullptr;
	MixerPanel *m_mixer = nullptr;
	QTableWidget *m_dests = nullptr;
	QPushButton *m_goLive = nullptr;
	QLabel *m_statusLabel = nullptr;
	QLabel *m_stats = nullptr;
	QTimer m_statsTimer;
	os_cpu_usage_info_t *m_cpu = nullptr;
	quint64 m_lastBytes = 0;
	qint64 m_lastBytesMs = 0;
	double m_kbps = 0.0;
	MultiStream m_stream;
	Recorder m_rec;
	VerticalClip m_vertical;
	ClipsPanel *m_clips = nullptr;
	HotkeyManager *m_hotkeys = nullptr;
	QPushButton *m_recBtn = nullptr;
	QPushButton *m_replayBtn = nullptr;
	QPushButton *m_clipBtn = nullptr;
	QMenu *m_viewMenu = nullptr;
	QByteArray m_defaultState;
	Updater *m_updater = nullptr;
};
