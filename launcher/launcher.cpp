// RareStudio launcher.
//
// Layout:
//   <install dir>\RareStudio.exe            this launcher (never replaced by updates)
//   <install dir>\app\                      version shipped with the installer
//   %LOCALAPPDATA%\RareStudio\updates\<v>\  versions downloaded by the in-app updater
//
// Each version dir holds RareStudioApp.exe and a version.txt. An update dir only
// counts once the updater has written an "ok" marker, so half-extracted updates
// are ignored. The newest valid version wins.

#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using Version = std::vector<int>;

static Version parseVersion(const std::wstring &s)
{
	Version v;
	int cur = 0;
	bool any = false;
	for (wchar_t c : s) {
		if (c >= L'0' && c <= L'9') {
			cur = cur * 10 + (c - L'0');
			any = true;
		} else if (c == L'.') {
			v.push_back(cur);
			cur = 0;
			any = false;
		} else if (any) {
			break;
		}
	}
	if (any)
		v.push_back(cur);
	return v;
}

static std::wstring readVersionFile(const fs::path &dir)
{
	std::wifstream f(dir / L"version.txt");
	std::wstring s;
	if (f)
		std::getline(f, s);
	return s;
}

static fs::path localAppData()
{
	PWSTR p = nullptr;
	fs::path out;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)))
		out = p;
	CoTaskMemFree(p);
	return out;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	wchar_t self[MAX_PATH];
	GetModuleFileNameW(nullptr, self, MAX_PATH);
	fs::path installDir = fs::path(self).parent_path();

	fs::path best = installDir / L"app";
	Version bestVer = parseVersion(readVersionFile(best));

	std::error_code ec;
	fs::path updates = localAppData() / L"RareStudio" / L"updates";
	for (fs::directory_iterator it(updates, ec), end; !ec && it != end; it.increment(ec)) {
		const fs::path &dir = it->path();
		if (!it->is_directory() || !fs::exists(dir / L"ok") || !fs::exists(dir / L"RareStudioApp.exe"))
			continue;
		Version v = parseVersion(readVersionFile(dir));
		if (v > bestVer) {
			bestVer = v;
			best = dir;
		}
	}

	fs::path exe = best / L"RareStudioApp.exe";
	std::wstring cmd = L"\"" + exe.wstring() + L"\"";
	// Forward any args we were started with.
	if (LPWSTR args = GetCommandLineW()) {
		int argc = 0;
		LPWSTR *argv = CommandLineToArgvW(args, &argc);
		for (int i = 1; argv && i < argc; ++i)
			cmd += L" \"" + std::wstring(argv[i]) + L"\"";
		LocalFree(argv);
	}

	STARTUPINFOW si{sizeof(si)};
	PROCESS_INFORMATION pi{};
	std::wstring workDir = best.wstring();
	if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, workDir.c_str(), &si, &pi)) {
		MessageBoxW(nullptr, L"Could not start RareStudio. Please reinstall.", L"RareStudio", MB_ICONERROR);
		return 1;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 0;
}
