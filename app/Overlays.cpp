#include "Overlays.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTimeEdit>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace Overlays {

namespace {

struct Template {
	const char *mode, *label, *scene, *title, *sub, *done;
	int minutes; // 0 = no countdown
};

const Template kTemplates[] = {
	{"starting", "Starting soon", "Starting Soon", "STARTING SOON", "Grab a drink, we go live in a moment", "WE ARE LIVE", 5},
	{"brb", "Be right back", "BRB", "BE RIGHT BACK", "Stay with us", "BACK ANY SECOND", 0},
	{"ending", "Stream ending", "Stream Ending", "THANKS FOR WATCHING", "Stream ending. See you next time", "", 0},
	{"countdown", "Countdown", "Countdown", "COMING UP", "", "GO TIME", 10},
};

} // namespace

QString prepare()
{
	const QString dir = qEnvironmentVariable("APPDATA") + "/RareStudio/overlays";
	QDir().mkpath(dir);
	// Rewritten every time so a new version's improvements arrive; copy the file to customise it.
	for (const auto &[res, name] : {std::pair{":/overlay.html", "overlay.html"}, std::pair{":/logo", "logo.png"}}) {
		QFile in(res);
		if (!in.open(QIODevice::ReadOnly))
			continue;
		const QByteArray data = in.readAll();
		QFile out(dir + "/" + name);
		if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
			out.write(data);
	}
	return dir;
}

bool ask(QWidget *parent, const std::function<bool(const QString &)> &taken, Spec *out)
{
	QDialog dlg(parent);
	dlg.setWindowTitle("Add overlay scene");
	dlg.setMinimumWidth(460);

	auto *kind = new QComboBox;
	for (const Template &t : kTemplates)
		kind->addItem(t.label);
	auto *scene = new QLineEdit;
	auto *title = new QLineEdit;
	auto *sub = new QLineEdit;
	auto *done = new QLineEdit;
	done->setPlaceholderText("Shown when the countdown reaches zero");

	auto *none = new QRadioButton("No countdown");
	auto *forMinutes = new QRadioButton("Count down from");
	auto *toClock = new QRadioButton("Count down to");
	auto *minutes = new QSpinBox;
	minutes->setRange(1, 600);
	minutes->setSuffix(" minutes");
	auto *clock = new QTimeEdit(QTime(20, 0));
	clock->setDisplayFormat("h:mm AP");
	auto *group = new QButtonGroup(&dlg);
	for (auto *b : {none, forMinutes, toClock})
		group->addButton(b);
	auto *minutesRow = new QWidget;
	auto *mr = new QHBoxLayout(minutesRow);
	mr->setContentsMargins(0, 0, 0, 0);
	mr->addWidget(forMinutes);
	mr->addWidget(minutes);
	mr->addStretch();
	auto *clockRow = new QWidget;
	auto *cr = new QHBoxLayout(clockRow);
	cr->setContentsMargins(0, 0, 0, 0);
	cr->addWidget(toClock);
	cr->addWidget(clock);
	cr->addStretch();

	auto load = [=](int i) {
		const Template &t = kTemplates[i];
		scene->setText(t.scene);
		title->setText(t.title);
		sub->setText(t.sub);
		done->setText(t.done);
		minutes->setValue(t.minutes ? t.minutes : 5);
		(t.minutes ? forMinutes : none)->setChecked(true);
	};
	QObject::connect(kind, &QComboBox::currentIndexChanged, &dlg, load);
	load(0);

	auto *hint = new QLabel("The countdown restarts each time the scene goes on air. Use the source's Refresh page to restart it by hand.");
	hint->setObjectName("hint");
	hint->setWordWrap(true);

	auto *form = new QFormLayout;
	form->addRow("Template", kind);
	form->addRow("Scene name", scene);
	form->addRow("Title", title);
	form->addRow("Subtitle", sub);
	form->addRow("Countdown", none);
	form->addRow("", minutesRow);
	form->addRow("", clockRow);
	form->addRow("At zero", done);
	auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	box->button(QDialogButtonBox::Ok)->setText("Add scene");
	auto *lay = new QVBoxLayout(&dlg);
	lay->setContentsMargins(18, 16, 18, 14);
	lay->setSpacing(12);
	lay->addLayout(form);
	lay->addWidget(hint);
	lay->addWidget(box);
	QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	QObject::connect(box, &QDialogButtonBox::accepted, &dlg, [&] {
		const QString name = scene->text().trimmed();
		if (name.isEmpty() || taken(name)) {
			QMessageBox::warning(&dlg, "Add overlay scene", name.isEmpty() ? "Give the scene a name." : "Something with that name already exists.");
			return;
		}
		dlg.accept();
	});
	if (dlg.exec() != QDialog::Accepted)
		return false;

	QUrlQuery q;
	q.addQueryItem("mode", kTemplates[kind->currentIndex()].mode);
	q.addQueryItem("title", title->text());
	q.addQueryItem("sub", sub->text());
	q.addQueryItem("done", done->text());
	if (forMinutes->isChecked())
		q.addQueryItem("minutes", QString::number(minutes->value()));
	else if (toClock->isChecked())
		q.addQueryItem("at", clock->time().toString("H:mm"));
	QUrl url = QUrl::fromLocalFile(prepare() + "/overlay.html");
	url.setQuery(q);
	out->sceneName = scene->text().trimmed();
	out->url = url.toString(QUrl::FullyEncoded);
	return true;
}

} // namespace Overlays
