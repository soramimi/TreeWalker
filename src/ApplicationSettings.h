#ifndef APPLICATIONSETTINGS_H
#define APPLICATIONSETTINGS_H

#define ORGANIZATION_NAME "soramimi.jp"
#define APPLICATION_NAME "TreeWalker"

class ApplicationSettings {
public:
	bool remember_and_restore_window_position = true;
	bool strongly_draw_file_suffix = true; // ファイル名の拡張子を強調表示する
	static ApplicationSettings defaultSettings()
	{
		return {};
	}
};

#endif // APPLICATIONSETTINGS_H
