#include "WebDock.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QRegularExpression>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineView>

namespace {

// Qt's default profile is persistent (cookies and logins survive restarts).
QWebEngineProfile *dashboardProfile()
{
	QWebEngineProfile *p = QWebEngineProfile::defaultProfile();
	// Twitch and Google reject user agents that look embedded or too old. Qt 6.8 bundles Chromium 122,
	// which sites now flag as "unsupported", so present as a current desktop Chrome instead.
	p->setHttpUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
			    "Chrome/141.0.0.0 Safari/537.36");
	return p;
}

// Sign-in pages (Google especially) pop a Windows "insert your security key" prompt when WebAuthn
// is available. Report it as unavailable so they fall back to the normal password flow.
const char *kNoWebAuthn = R"JS(
(function () {
  try {
    const deny = () => Promise.reject(new DOMException('Not allowed', 'NotAllowedError'));
    const cc = navigator.credentials;
    if (cc) {
      for (const name of ['get', 'create']) {
        const orig = cc[name] ? cc[name].bind(cc) : null;
        cc[name] = function (opts) {
          if (opts && opts.publicKey) return deny();
          return orig ? orig(opts) : Promise.reject(new DOMException('Not supported', 'NotSupportedError'));
        };
      }
    }
    if (window.PublicKeyCredential) {
      PublicKeyCredential.isUserVerifyingPlatformAuthenticatorAvailable = () => Promise.resolve(false);
      PublicKeyCredential.isConditionalMediationAvailable = () => Promise.resolve(false);
    }
  } catch (e) {}
})();
)JS";

} // namespace

WebDock::WebDock(const QString &title, const QList<Site> &sites, QWidget *parent) : QDockWidget(title, parent)
{
	setObjectName("dashboards");
	setMinimumWidth(380);

	m_tabs = new QTabWidget;
	m_tabs->setDocumentMode(true);
	setWidget(m_tabs);

	for (const Site &site : sites) {
		Tab tab;
		tab.home = site.home;
		tab.container = new QWidget;
		tab.sleep = new QTimer(this);
		tab.sleep->setSingleShot(true);

		auto *lay = new QVBoxLayout(tab.container);
		lay->setContentsMargins(0, 0, 0, 0);
		lay->setSpacing(0);
		auto *nav = new QHBoxLayout;
		nav->setContentsMargins(4, 4, 4, 4);
		auto *back = new QPushButton("◀");
		auto *reload = new QPushButton("↻");
		auto *home = new QPushButton("Home");
		back->setToolTip("Back");
		reload->setToolTip("Reload");
		for (auto *b : {back, reload})
			b->setFixedWidth(34);
		nav->addWidget(back);
		nav->addWidget(reload);
		nav->addWidget(home);
		nav->addStretch();
		lay->addLayout(nav);

		const int index = m_data.size();
		m_data.append(tab);
		m_tabs->addTab(tab.container, site.title);

		connect(back, &QPushButton::clicked, this, [this, index] {
			if (m_data[index].view)
				m_data[index].view->back();
		});
		connect(reload, &QPushButton::clicked, this, [this, index] {
			if (m_data[index].view)
				m_data[index].view->reload();
		});
		connect(home, &QPushButton::clicked, this, [this, index] {
			if (m_data[index].view)
				m_data[index].view->setUrl(m_data[index].home);
		});

		// Frozen first (stops scripts and rendering), then discarded (drops the renderer).
		connect(tab.sleep, &QTimer::timeout, this, [this, index] {
			QWebEngineView *view = m_data[index].view;
			if (!view)
				return;
			QWebEnginePage *page = view->page();
			if (page->lifecycleState() == QWebEnginePage::LifecycleState::Active) {
				page->setLifecycleState(QWebEnginePage::LifecycleState::Frozen);
				m_data[index].sleep->start(4000);
			} else if (page->lifecycleState() == QWebEnginePage::LifecycleState::Frozen) {
				page->setLifecycleState(QWebEnginePage::LifecycleState::Discarded);
			}
		});
	}

	connect(this, &QDockWidget::visibilityChanged, this, [this] { sync(); });
	connect(m_tabs, &QTabWidget::currentChanged, this, [this] { sync(); });
}

void WebDock::sync()
{
	const bool shown = isVisible();
	for (int i = 0; i < m_data.size(); ++i) {
		if (shown && i == m_tabs->currentIndex())
			wake(i);
		else
			scheduleSleep(i);
	}
}

void WebDock::ensureView(int index)
{
	Tab &tab = m_data[index];
	if (tab.view)
		return;
	tab.view = new QWebEngineView(dashboardProfile(), tab.container);

	QWebEngineScript script;
	script.setName("rarestudio-no-webauthn");
	script.setInjectionPoint(QWebEngineScript::DocumentCreation);
	script.setWorldId(QWebEngineScript::MainWorld);
	script.setRunsOnSubFrames(true);
	script.setSourceCode(QString::fromUtf8(kNoWebAuthn));
	tab.view->page()->scripts().insert(script);

	qobject_cast<QVBoxLayout *>(tab.container->layout())->addWidget(tab.view, 1);
	tab.view->load(tab.home);
}

void WebDock::wake(int index)
{
	Tab &tab = m_data[index];
	tab.sleep->stop();
	ensureView(index);
	tab.view->page()->setLifecycleState(QWebEnginePage::LifecycleState::Active);
}

void WebDock::scheduleSleep(int index)
{
	Tab &tab = m_data[index];
	if (tab.view && !tab.sleep->isActive() &&
	    tab.view->page()->lifecycleState() == QWebEnginePage::LifecycleState::Active)
		tab.sleep->start(3000);
}
