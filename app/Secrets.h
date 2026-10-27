#pragma once

#include <QString>

// Secrets (stream keys, OAuth tokens) are encrypted at rest with Windows DPAPI, bound to the
// current Windows user, so the settings file alone is useless to anyone who copies it.
namespace Secrets {

QString protect(const QString &plain);   // returns base64 ciphertext, "" on failure
QString unprotect(const QString &b64);   // returns plaintext, "" on failure

} // namespace Secrets
