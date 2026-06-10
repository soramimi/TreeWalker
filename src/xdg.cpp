#include "xdg.h"


#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

static std::string get_home_dir()
{
    if (const char *home = std::getenv("HOME")) {
        if (*home) return home;
    }
    return {};
}

static std::string unquote_xdg_value(std::string s)
{
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }

    std::string home = get_home_dir();

    // XDG config normally uses "$HOME/Desktop"
    const std::string prefix = "$HOME";
    if (s.rfind(prefix, 0) == 0) {
        s.replace(0, prefix.size(), home);
    }

    return s;
}

static std::filesystem::path _get_desktop_dir()
{
    const std::string home = get_home_dir();
    if (home.empty()) {
        return {};
    }

    // 1. XDG_CONFIG_HOME/user-dirs.dirs
    std::filesystem::path config;
    if (const char *xdg_config_home = std::getenv("XDG_CONFIG_HOME")) {
        if (*xdg_config_home) {
            config = xdg_config_home;
        }
    }

    if (config.empty()) {
        config = std::filesystem::path(home) / ".config";
    }

    std::ifstream ifs(config / "user-dirs.dirs");
    std::string line;

    while (std::getline(ifs, line)) {
        constexpr const char *key = "XDG_DESKTOP_DIR=";

        if (line.rfind(key, 0) == 0) {
            std::string value = line.substr(std::char_traits<char>::length(key));
            value = unquote_xdg_value(value);

            if (!value.empty()) {
                return value;
            }
        }
    }

    // 2. fallback
    return std::filesystem::path(home) / "Desktop";
}

std::string xdg::get_desktop_dir()
{
	auto path = _get_desktop_dir();
	if (path.empty()) {
		return {};
	}
	return path.string();
}
