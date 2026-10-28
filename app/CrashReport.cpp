#include "CrashReport.h"

#include <QDir>
#include <QFile>

#include <windows.h>

#include <csignal>
#include <cstdio>
#include <exception>

#ifndef RARESTUDIO_BUILD
#define RARESTUDIO_BUILD "dev"
#endif

namespace CrashReport {

namespace {

// Everything below runs while the process is already broken: plain Win32/C calls, fixed buffers,
// no Qt, no allocation beyond the stack.
wchar_t g_report[MAX_PATH];
wchar_t g_marker[MAX_PATH];
volatile LONG g_writing = 0;

void put(HANDLE f, const char *s)
{
	DWORD n;
	WriteFile(f, s, DWORD(strlen(s)), &n, nullptr);
}

void describe(char *out, size_t cap, void *addr)
{
	HMODULE mod = nullptr;
	char name[MAX_PATH] = "?";
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(addr), &mod)) {
		char full[MAX_PATH];
		if (GetModuleFileNameA(mod, full, MAX_PATH)) {
			const char *slash = strrchr(full, '\\');
			strncpy_s(name, slash ? slash + 1 : full, _TRUNCATE);
		}
	}
	snprintf(out, cap, "  %s+0x%llx\r\n", name, (unsigned long long)((char *)addr - (char *)mod));
}

void write(const char *what, DWORD code, void *addr)
{
	if (InterlockedExchange(&g_writing, 1))
		return; // a second fault while writing: give up rather than loop
	HANDLE f = CreateFileW(g_report, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE)
		return;
	SYSTEMTIME st;
	GetLocalTime(&st);
	char line[512];
	snprintf(line, sizeof(line), "==== RareStudio %s (build %s) crashed %04d-%02d-%02d %02d:%02d:%02d\r\n%s code=0x%08lx\r\n", RARESTUDIO_VERSION,
		 RARESTUDIO_BUILD, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, what, code);
	put(f, line);
	if (addr) {
		put(f, "at\r\n");
		describe(line, sizeof(line), addr);
		put(f, line);
	}
	put(f, "stack\r\n");
	void *frames[48];
	const USHORT n = CaptureStackBackTrace(0, 48, frames, nullptr);
	for (USHORT i = 0; i < n; ++i) {
		describe(line, sizeof(line), frames[i]);
		put(f, line);
	}
	put(f, "\r\n");
	CloseHandle(f);
	CloseHandle(CreateFileW(g_marker, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
}

LONG WINAPI onException(EXCEPTION_POINTERS *ep)
{
	write("unhandled exception", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
	return EXCEPTION_CONTINUE_SEARCH; // let Windows (and any debugger) see it too
}

void onTerminate()
{
	write("std::terminate", 0, nullptr);
	std::abort();
}

void onAbort(int)
{
	write("abort", 0, nullptr);
	_exit(3);
}

} // namespace

QString logsDir()
{
	return qEnvironmentVariable("APPDATA") + "/RareStudio";
}

void install()
{
	QDir().mkpath(logsDir());
	QDir::toNativeSeparators(logsDir() + "/crash.log").toWCharArray(g_report);
	QDir::toNativeSeparators(logsDir() + "/crash.pending").toWCharArray(g_marker);
	// Keep the history from growing without bound: start over once it is large.
	if (QFile f(logsDir() + "/crash.log"); f.size() > 512 * 1024)
		f.remove();
	SetUnhandledExceptionFilter(onException);
	std::set_terminate(onTerminate);
	std::signal(SIGABRT, onAbort);
}

QString takePending()
{
	const QString marker = logsDir() + "/crash.pending";
	if (!QFile::exists(marker))
		return {};
	QFile::remove(marker);
	return logsDir() + "/crash.log";
}

} // namespace CrashReport
