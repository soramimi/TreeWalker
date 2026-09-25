#include "common/realpath.h"
#include "common/misc.h"
#include "joinpath.h"

#ifdef _WIN32
#include <windows.h>
#include <KnownFolders.h>
#include <ShlObj.h>
#include <QFileInfo>
#include "wstring.h"
#else
#include <QFileInfo>
#include <limits.h>
#include "xdg.h"
#endif

/**
 * @brief パスを絶対パスに変換する
 *
 * 指定されたパスを絶対パスに変換して返します。パスが存在しない場合や、変換に失敗した場合は空文字列を返します。
 * '~'で始まるパスは、環境変数HOMEの値を展開して変換します。
 *
 * @param path 変換するパス
 * @return 絶対パス。変換に失敗した場合は空文字列。
 */
std::string misc::realpath(const char *path)
{
#ifdef _WIN32
	std::wstring ws = misc::convert_utf8_to_wstr(path);
	// std::string s = path;
	for (wchar_t c : ws) {
		if (c == '/') {
			c = '\\';
		}
	}
	if (*path == '~') {
		std::wstring home;
		PWSTR path2 = NULL;
		HRESULT hr = SHGetKnownFolderPath(FOLDERID_Profile, 0, NULL, &path2);

		if (SUCCEEDED(hr)) {
			home = path2;
			// fprintf(stderr, "Home Directory: %s\n", s.c_str());
			// Must free the memory allocated by the API
			CoTaskMemFree(path2);
		}

		ws = home / ws.substr(1);
	}
	wchar_t tmp[MAX_PATH];
	if (_wfullpath(tmp, ws.c_str(), MAX_PATH)) {
		ws = misc::normalizePathSeparator(ws);
		return misc::convert_wstr_to_utf8(ws);
	}
#else
	std::string s;
	if (*path == '~') {
		// char const *home = getenv("HOME");
		auto home = xdg::get_home_dir();
		if (!home.empty()) {
			s = home;
			s = s / (path + 1);
			path = s.c_str();
		}
	}
	char tmp[PATH_MAX];
	char *p = ::realpath(path, tmp);
	if (p) {
		return tmp;
	}
#endif
	fprintf(stderr, "Warning: realpath failed for path: %s.\n", path);
	return {};
}

std::string misc::realpath(std::string const &path)
{
	return realpath(path.c_str());
}

QString misc::realpath(QString const &path)
{
#ifdef _WIN32
	auto s = realpath(convert_wstr_to_utf8(path.toStdWString()));
	return QString::fromStdWString(convert_utf8_to_wstr(s));
#else
	auto s = realpath(path.toStdString());
	return QString::fromStdString(s);
#endif
}
