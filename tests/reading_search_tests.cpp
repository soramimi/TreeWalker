#include <QTest>
#include <QElapsedTimer>
#include <QPainter>
#include <QScopedValueRollback>
#include <QStyledItemDelegate>

#include "ApplicationGlobal.h"
#include "MainWindow.h"
#include "FileTableView.h"
#include "ThumbnailView.h"
#include "IncrementalSearchHelper.h"

class ReadingSearchTests : public QObject {
	Q_OBJECT
private:
	static void verifyResult(QString const &text, incrementalsearch::Result const &result,
		bool matches, QStringList const &highlights)
	{
		QCOMPARE(result.match, matches);
		// ハイライトの範囲に重複や欠落がなく、元の UTF-8 文字列を復元できること。
		std::string original = text.toStdString();
		std::string restored;
		QStringList actual_highlights;
		size_t cursor = 0;
		for (auto const &part : result.parts) {
			QCOMPARE(part.pos, cursor);
			QVERIFY(part.pos <= part.end);
			QVERIFY(part.end <= original.size());
			QCOMPARE(to_std_string(part.text), original.substr(part.pos, part.end - part.pos));
			restored += to_std_string(part.text);
			if (part.match) actual_highlights.push_back(toQString(part.text));
			cursor = part.end;
		}
		QCOMPARE(cursor, original.size());
		QCOMPARE(restored, original);
		QCOMPARE(actual_highlights, highlights);
	}

private slots:
	void initTestCase()
	{
		QVERIFY(global);
		QVERIFY(global->incremental_search);
	}

	void readingMatch_data()
	{
		QTest::addColumn<QString>("text");
		QTest::addColumn<QString>("query");
		QTest::addColumn<QStringList>("highlights");
		QTest::addColumn<bool>("matches");
		QTest::newRow("Tokyo and prefecture suffix")
			<< QString("東京都.txt") << QString("toukyouto")
			<< QStringList{"東京都"} << true;
		QTest::newRow("whole sentence across five morphemes")
			<< QString("吾輩は猫である.txt") << QString("wagahaihanekodearu")
			<< QStringList{"吾輩は猫である"} << true;
		QTest::newRow("two morphemes")
			<< QString("吾輩は猫である.txt") << QString("wagahaiha")
			<< QStringList{"吾輩は"} << true;
		QTest::newRow("partial first and last readings")
			<< QString("吾輩は猫である.txt") << QString("haihane")
			<< QStringList{"吾輩は猫"} << true;
		QTest::newRow("suffix across three morphemes")
			<< QString("吾輩は猫である.txt") << QString("nekodearu")
			<< QStringList{"猫である"} << true;
		QTest::newRow("repeated occurrences")
			<< QString("吾輩は猫である、吾輩は猫である.txt") << QString("nekodearu")
			<< QStringList{"猫である", "猫である"} << true;
		QTest::newRow("overlapping occurrences")
			<< QString("吾輩は猫である吾輩は猫である吾輩は猫である.txt") << QString("aruwagahaihanekodearu")
			<< QStringList{"ある吾輩は猫である吾輩は猫である"} << true;
		QTest::newRow("source gap between morphemes")
			<< QString("吾輩 は猫である.txt") << QString("wagahaiha")
			<< QStringList{"吾輩 は"} << true;
		QTest::newRow("katakana original spelling")
			<< QString("カタカナの資料.txt") << QString("タ")
			<< QStringList{"タ"} << true;
		QTest::newRow("single morpheme")
			<< QString("吾輩は猫である.txt") << QString("neko")
			<< QStringList{"猫"} << true;
		QTest::newRow("case insensitive original spelling")
			<< QString("ReadMe.TXT") << QString("rEaDmE")
			<< QStringList{"ReadMe"} << true;
		QTest::newRow("no match")
			<< QString("吾輩は猫である.txt") << QString("inudehaaru")
			<< QStringList{} << false;
		QTest::newRow("empty filter")
			<< QString("吾輩は猫である.txt") << QString()
			<< QStringList{} << true;
		QTest::newRow("empty source")
			<< QString() << QString("neko")
			<< QStringList{} << false;
		QTest::newRow("name and extension")
			<< QString("東京都.txt") << QString("都.tx")
			<< QStringList{"都.tx"} << true;
	}

	void readingMatch()
	{
		QFETCH(QString, text);
		QFETCH(QString, query);
		QFETCH(QStringList, highlights);
		QFETCH(bool, matches);
		auto &search = *global->incremental_search;
		auto filter = search.makeFilter(query.toStdString());
		auto result = search.match(fromQString(text), filter);
		verifyResult(text, result, matches, highlights);
		auto prepared = search.prepare(fromQString(text));
		const auto count = search.readingGenerationCount();
		for (int i = 0; i < 3; ++i) verifyResult(text, search.match(prepared, filter), matches, highlights);
		QCOMPARE(search.readingGenerationCount(), count);
	}

	void originalAndReadingMatchesDoNotDuplicateText()
	{
		IncrementalSearchFilter filter;
		// 通常のローマ字変換を介さず、公開フィルタの原表記と読みの一致範囲が
		// 重なる場合を作り、形態素全体に含まれる小さい一致の結合を検証する。
		filter.original_text = fromQString("タ");
		filter.katakana_text = fromQString("タ");
		const QString text = "カタカナの資料.txt";
		auto result = global->incremental_search->match(fromQString(text), filter);
		verifyResult(text, result, true, {"カタカナ"});
		verifyResult(text, global->incremental_search->match(global->incremental_search->prepare(fromQString(text)), filter), true, {"カタカナ"});
	}

	void preparedTextOwnsSourceAndFragments()
	{
		auto &search = *global->incremental_search;
		auto prepared = search.prepare(fromQString(QString("東京都.txt")));
		IncrementalSearchFilter filter;
		filter.original_text = fromQString("東京都.txt");
		auto result = search.match(prepared, filter);
		verifyResult("東京都.txt", result, true, {"東京都.txt"});
		auto prefix = incrementalsearch::textFragments(result, 0, 4);
		auto suffix = incrementalsearch::textFragments(result, 4);
		QString restored;
		for (auto const &part : prefix) { restored += part.text; QVERIFY(part.match); }
		QCOMPARE(restored, QString("東京都."));
		for (auto const &part : suffix) { restored += part.text; QVERIFY(part.match); }
		QCOMPARE(restored, QString("東京都.txt"));
		QCOMPARE(QString::fromStdString(search.convertRomajiToKatakana("nn")), QString("ン"));
		QCOMPARE(QString::fromStdString(search.convertRomajiToKatakana("konna")), QString("コンア"));
	}

	void modelAndPaintingReuseReadings()
	{
		MainWindow window;
		QScopedValueRollback<MainWindow *> context(global->mainwindow, &window);
		auto *model = window.fileitemmodel();
		auto *details = window.findChild<FileTableView *>("tableView");
		auto *thumbnails = window.findChild<ThumbnailView *>("thumbnailView");
		QVERIFY(details && thumbnails);
		details->setModel(model);
		thumbnails->setModel(model);
		details->setKind(Kind::Directory);
		thumbnails->setKind(Kind::Directory);
		std::vector<FileItemModel::Item> items;
		FileItemModel::Item item;
		item.name = item.info.name = "東京都写真整理メモのとても長い名前😀.txt";
		item.type = "テキストファイル";
		item.size = 1234;
		item.modified = QDateTime::fromMSecsSinceEpoch(1000);
		items.push_back(std::move(item));
		model->replaceItems(Kind::Directory, std::move(items), "toukyou");
		auto &search = *global->incremental_search;
		const auto prepared = search.readingGenerationCount();
		for (auto const &query : {QString("toukyou"), QString("shashin"), QString("unmatched-query"), QString("toukyou")}) {
			model->setFilterText(query);
			model->rowCount();
		}
		QCOMPARE(search.readingGenerationCount(), prepared);
		QScopedValueRollback<bool> suffix(global->appsettings.strongly_draw_file_suffix);
		QElapsedTimer drawing;
		drawing.start();
		for (bool strong : {false, true}) {
			global->appsettings.strongly_draw_file_suffix = strong;
			for (int width : {100, 800}) {
				QImage image(width, 200, QImage::Format_ARGB32);
				image.fill(Qt::white);
				QPainter painter(&image);
				QStyleOptionViewItem option;
				option.initFrom(details);
				option.rect = QRect(0, 0, width, 24);
				option.widget = details;
				for (int column = 0; column < 4; ++column) details->itemDelegate()->paint(&painter, option, model->index(0, column));
				option.widget = thumbnails;
				option.rect.setHeight(200);
				thumbnails->itemDelegate()->paint(&painter, option, model->index(0, 0));
			}
		}
		QCOMPARE(search.readingGenerationCount(), prepared);
		qInfo() << "Cached detail/thumbnail drawing:" << drawing.elapsed() << "ms; additional reading generations: 0";
		items.clear();
		FileItemModel::Item renamed;
		renamed.name = renamed.info.name = "大阪府.txt";
		items.push_back(std::move(renamed));
		model->replaceItems(Kind::Directory, std::move(items), "oosaka");
		QCOMPARE(model->rowCount(), 1);
		QVERIFY(search.readingGenerationCount() > prepared);
		verifyResult("大阪府.txt", model->matchText(model->index(0, 0)), true, {"大阪"});
	}

	void readingCacheWorkload()
	{
		auto &search = *global->incremental_search;
		FileItemModel model;
		std::vector<FileItemModel::Item> items;
		for (int i = 0; i < 1000; ++i) {
			FileItemModel::Item item;
			item.name = QString("東京都写真整理メモ%1.txt").arg(i);
			items.push_back(std::move(item));
		}
		QElapsedTimer timer;
		timer.start();
		model.replaceItems(Kind::Directory, std::move(items), {});
		const auto preparation_ms = timer.elapsed();
		const auto count = search.readingGenerationCount();
		timer.restart();
		for (int i = 0; i < 20; ++i) {
			model.setFilterText(i % 2 ? "toukyou" : "shashin");
			QCOMPARE(model.count(), 1000);
		}
		const auto cached_ms = timer.elapsed();
		QCOMPARE(search.readingGenerationCount(), count);
		auto filter = search.makeFilter("shashin");
		timer.restart();
		for (int i = 0; i < 20; ++i) for (int row = 0; row < model.count(); ++row) {
			QVERIFY(search.match(fromQString(model.item(row)->name), filter));
		}
		const auto uncached_ms = timer.elapsed();
		QCOMPARE(search.readingGenerationCount() - count, quint64(20000));
		qInfo() << "1000 names: prepare" << preparation_ms << "ms; 20 searches cached" << cached_ms << "ms, uncached" << uncached_ms << "ms";
	}
};

int runReadingSearchTests(int argc, char **argv)
{
	ReadingSearchTests tests;
	return QTest::qExec(&tests, argc, argv);
}

#include "reading_search_tests.moc"
