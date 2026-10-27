#include "Secrets.h"

#include <QByteArray>

#include <windows.h>
#include <dpapi.h>

namespace Secrets {

QString protect(const QString &plain)
{
	QByteArray in = plain.toUtf8();
	DATA_BLOB src{DWORD(in.size()), reinterpret_cast<BYTE *>(in.data())}, dst{};
	if (!CryptProtectData(&src, L"RareStudio", nullptr, nullptr, nullptr, 0, &dst))
		return {};
	QByteArray out(reinterpret_cast<char *>(dst.pbData), int(dst.cbData));
	LocalFree(dst.pbData);
	return QString::fromLatin1(out.toBase64());
}

QString unprotect(const QString &b64)
{
	QByteArray in = QByteArray::fromBase64(b64.toLatin1());
	if (in.isEmpty())
		return {};
	DATA_BLOB src{DWORD(in.size()), reinterpret_cast<BYTE *>(in.data())}, dst{};
	if (!CryptUnprotectData(&src, nullptr, nullptr, nullptr, nullptr, 0, &dst))
		return {};
	QString out = QString::fromUtf8(reinterpret_cast<char *>(dst.pbData), int(dst.cbData));
	LocalFree(dst.pbData);
	return out;
}

} // namespace Secrets
