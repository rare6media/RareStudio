#pragma once

#include "MultiStream.h"
#include "Recorder.h"
#include "VerticalClip.h"

#include <util/platform.h>

#include <QMainWindow>
#include <QTimer>

class ClipsPanel;
class QAction;
class StatusChip;
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
	void buildScenesDock(const QStringList &order);
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
	void addOverlayScene();
	void buildCollectionsMenu(class QMenu *menu);
	void switchCollection(const QString &name);
	void buildClipsDock();
	void setReplayUi(bool armed);
	void refreshStreamKeys(bool announce);
	void upsertDestination(const QString &name, const QString &server, const QString &key);
	void createTwitchClip();
	void onShortSaved(const QString &path);
	void uploadShort(int panelId, const QString &path, const QString &title = QString(), const QString &privacy = QString());
	void openClipEditor(const QString &path, bool offerUpload);
	void toggleRecord();
	void togglePause();
	void syncPauseButton();
	void takeScreenshot();
	void toggleReplay();
	void saveClip();
	void onHotkey(int action);
	void moveSelected(obs_order_movement dir);
	void selectInList(const QString &name);
	void updateStats();
	void openSettings();
	void applyDefaultDockSizes();
	void buildSourcesDock();
	void buildMixerDock();
	void buildStreamDock();
	void buildWebDocks();

	void addScene();
	void addSourceDialog();
	void editSelectedSource();
	void removeSelectedSource();
	void refreshSources();
	void updateItemButtons(); // source buttons that need a selection follow it

	// Edit > Copy / Paste / Duplicate. Paste adds the same source (a reference, so one webcam or overlay
	// can sit in several scenes) unless the scene already shows it, in which case it pastes a copy.
	void copySource();
	void pasteSource();
	void duplicateSource();
	void placeSource(obs_source_t *src, bool duplicate);

	void addDestination(const QString &name, const QString &server, const QString &key, bool enabled);
	void editDestination(int row);
	void refreshDestinationRow(int row);
	void setDestinationStatus(int row, const QString &state);
	QList<Destination> destinations() const;
	void saveDestinations() const;
	void persistAll() const;
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
	QLabel *m_sourcesEmpty = nullptr;
	QLabel *m_destsEmpty = nullptr;
	QList<QWidget *> m_itemButtons;
	QPushButton *m_destRemove = nullptr;
	StatusChip *m_liveChip = nullptr;
	StatusChip *m_recChip = nullptr;
	qint64 m_liveSinceMs = 0;
	bool m_restarting = false;
	QTimer m_statsTimer;
	QTimer m_autosave;
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
	QPushButton *m_pauseBtn = nullptr;
	QPushButton *m_replayBtn = nullptr;
	QPushButton *m_clipBtn = nullptr;
	QMenu *m_viewMenu = nullptr;
	QByteArray m_defaultState;
	Updater *m_updater = nullptr;

	struct CopiedItem {
		QString source; // looked up again on paste: it may have been renamed or removed since
		obs_transform_info transform = {};
		obs_sceneitem_crop crop = {};
		bool visible = true;
	} m_copied;
};
