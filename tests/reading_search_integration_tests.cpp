#include <QTest>
#include <QPainter>
#include <QScopedValueRollback>
#include <QStyledItemDelegate>

#include "ApplicationGlobal.h"
#include "MainWindow.h"
#include "FileTableView.h"
#include "ThumbnailView.h"
#include "IncrementalSearchHelper.h"

class ReadingSearchIntegrationTests : public QObject {
	Q_OBJECT

private slots:
	void fragmentsPreserveSource()
	{
		auto &search = *global->incremental_search;
		IncrementalSearchFilter filter;
		filter.original_text = fromQString("東京都.txt");
		auto result = search.match(fromQString(QString("東京都.txt")), filter);
		QVERIFY(result.match);
		auto prefix = incrementalsearch::textFragments(result, 0, 4);
		auto suffix = incrementalsearch::textFragments(result, 4);
		QString restored;
		for (auto const &part : prefix) { restored += part.text; QVERIFY(part.match); }
		QCOMPARE(restored, QString("東京都."));
		for (auto const &part : suffix) { restored += part.text; QVERIFY(part.match); }
		QCOMPARE(restored, QString("東京都.txt"));
	}

	void modelAndPaintingUseCurrentName()
	{
		MainWindow window;
		QScopedValueRollback<MainWindow *> context(global->mainwindow, &window);
		auto *model = window.fileitemmodel();
		auto *details = window.findChild<FileTableView *>("tableView");
		auto *thumbnails = window.findChild<ThumbnailView *>("thumbnailView");
		QVERIFY(details && thumbnails);
		details->setModel(model);
		thumbnails->setModel(model);
		details->setKind(FileItemModel::Kind::Directory);
		thumbnails->setKind(FileItemModel::Kind::Directory);
		std::vector<FileItemModel::Item> items;
		FileItemModel::Item item;
		item.name = "東京都写真整理メモのとても長い名前😀.txt";
		item.info.set_name(item.name);
		item.type = "テキストファイル";
		item.size = 1234;
		item.modified = QDateTime::fromMSecsSinceEpoch(1000);
		items.push_back(std::move(item));
		model->replaceItems(FileItemModel::Kind::Directory, std::move(items), "toukyou");
		for (auto const &query : {QString("toukyou"), QString("shashin"), QString("unmatched-query"), QString("toukyou")}) {
			model->setFilterText(query);
			QCOMPARE(model->rowCount(), query == "unmatched-query" ? 0 : 1);
		}
		QScopedValueRollback<bool> suffix(global->appsettings.strongly_draw_file_suffix);
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
		FileItemModel::Item renamed;
		renamed.name = "大阪府.txt";
		renamed.info.set_name(renamed.name);
		model->replaceItems(FileItemModel::Kind::Directory, {std::move(renamed)}, "oosaka");
		QCOMPARE(model->rowCount(), 1);
		auto result = model->matchText(model->index(0, 0));
		QVERIFY(result.match);
		QCOMPARE(result.parts.size(), size_t(2));
		QVERIFY(result.parts[0].match);
		QCOMPARE(toQString(result.parts[0].text), QString("大阪"));
		QVERIFY(!result.parts[1].match);
		QCOMPARE(toQString(result.parts[1].text), QString("府.txt"));
	}

	void repeatedFilteringOfLargeList()
	{
		FileItemModel model;
		std::vector<FileItemModel::Item> items;
		for (int i = 0; i < 1000; ++i) {
			FileItemModel::Item item;
			item.name = QString("東京都写真整理メモ%1.txt").arg(i);
			items.push_back(std::move(item));
		}
		model.replaceItems(FileItemModel::Kind::Directory, std::move(items), {});
		for (auto const &query : {QString("toukyou"), QString("shashin"), QString("unmatched-query"), QString("toukyou")}) {
			model.setFilterText(query);
			QCOMPARE(model.count(), query == "unmatched-query" ? 0 : 1000);
		}
		model.setFilterText({});
		QCOMPARE(model.count(), 1000);
	}
};

int runReadingSearchIntegrationTests(int argc, char **argv)
{
	ReadingSearchIntegrationTests tests;
	return QTest::qExec(&tests, argc, argv);
}

#include "reading_search_integration_tests.moc"
