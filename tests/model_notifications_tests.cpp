#include "ApplicationGlobal.h"
#include "FileTableView.h"
#include "FolderTreeView.h"
#include "MainWindow.h"
#include "ThumbnailView.h"

#include <QAbstractItemModelTester>
#include <QPersistentModelIndex>
#include <QScopedValueRollback>
#include <QSignalSpy>
#include <QtTest>

namespace {

FolderTreeItem *item(QString const &text)
{
	auto *result = new FolderTreeItem;
	result->setText(0, text);
	return result;
}

} // namespace

class ModelNotificationTests : public QObject {
	Q_OBJECT
private slots:
	void insertionAndRemovalKeepIndexesConsistent()
	{
		MainWindow window;
		QScopedValueRollback<MainWindow *> context(global->mainwindow, &window);
		FolderTreeView tree;
		auto *model = tree.model();
		QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
		QSignalSpy inserted(model, &QAbstractItemModel::rowsInserted);
		QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
		auto *root = item("root");
		bool observed_before_top_insert = false;
		connect(model, &QAbstractItemModel::rowsAboutToBeInserted, &tree,
			[&](QModelIndex const &parent, int first, int last) {
				if (!parent.isValid()) {
					observed_before_top_insert = true;
					QCOMPARE(model->rowCount(), 0);
					QCOMPARE(first, 0);
					QCOMPARE(last, 0);
					QVERIFY(root->parent() == nullptr);
				}
			});
		tree.addTopLevelItem(root);
		QVERIFY(observed_before_top_insert);
		QCOMPARE(inserted.count(), 1);
		QVERIFY(!inserted[0][0].value<QModelIndex>().isValid());
		QCOMPARE(inserted[0][1].toInt(), 0);
		QCOMPARE(model->rowCount(), 1);
		QVERIFY(!model->parent(tree.indexFromItem(root)).isValid());

		auto *first = item("first");
		auto *last = item("last");
		root->addChild(first);
		root->addChild(last);
		QVERIFY(!last->addChild(first));
		QCOMPARE(first->parent(), root);
		QPersistentModelIndex last_index = tree.indexFromItem(last);
		QCOMPARE(last_index.row(), 1);
		QCOMPARE(model->parent(last_index), tree.indexFromItem(root));

		auto *middle = item("middle");
		bool observed_before_middle_insert = false;
		connect(model, &QAbstractItemModel::rowsAboutToBeInserted, &tree,
			[&](QModelIndex const &parent, int first_row, int last_row) {
				if (parent == tree.indexFromItem(root) && first_row == 1) {
					observed_before_middle_insert = true;
					QCOMPARE(model->rowCount(parent), 2);
					QCOMPARE(last_row, 1);
					QVERIFY(middle->parent() == nullptr);
				}
			});
		root->addChild(middle, 1);
		QVERIFY(observed_before_middle_insert);
		QCOMPARE(model->rowCount(tree.indexFromItem(root)), 3);
		QCOMPARE(last_index.row(), 2);
		QCOMPARE(tree.itemFromIndex(model->index(1, 0, tree.indexFromItem(root))), middle);
		QCOMPARE(inserted.back()[1].toInt(), 1);
		QCOMPARE(inserted.back()[2].toInt(), 1);

		QPersistentModelIndex middle_index = tree.indexFromItem(middle);
		FolderTreeItem *detached = root->takeChild(1);
		QCOMPARE(detached, middle);
		QVERIFY(detached->parent() == nullptr);
		QVERIFY(!middle_index.isValid());
		QVERIFY(!tree.indexFromItem(detached).isValid());
		QCOMPARE(last_index.row(), 1);
		QCOMPARE(removed.count(), 1);
		QCOMPARE(removed[0][1].toInt(), 1);
		QCOMPARE(removed[0][2].toInt(), 1);
		delete detached;

		QVERIFY(!model->index(-1, 0).isValid());
		QVERIFY(!model->index(0, -1).isValid());
		QVERIFY(!model->index(0, 1).isValid());
		QVERIFY(!model->index(1, 0).isValid());
		QVERIFY(!model->hasChildren(tree.indexFromItem(first)));
		QVERIFY(!model->data(QModelIndex()).isValid());
	}

	void prebuiltSubtreeCanBeDetachedAndAttached()
	{
		MainWindow window;
		QScopedValueRollback<MainWindow *> context(global->mainwindow, &window);
		FolderTreeView tree;
		auto *model = tree.model();
		QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
		auto *left = item("left");
		auto *right = item("right");
		auto *branch = item("branch");
		auto *leaf = item("leaf");
		branch->addChild(leaf);
		left->addChild(branch);
		// Detached parents can also remove and reinsert children safely.
		QCOMPARE(branch->takeChild(0), leaf);
		QVERIFY(leaf->parent() == nullptr);
		branch->addChild(leaf);
		tree.addTopLevelItem(left);
		tree.addTopLevelItem(right);
		QPersistentModelIndex old_leaf = tree.indexFromItem(leaf);
		QVERIFY(old_leaf.isValid());
		QCOMPARE(model->parent(old_leaf), tree.indexFromItem(branch));

		QCOMPARE(left->takeChild(0), branch);
		QVERIFY(branch->parent() == nullptr);
		QCOMPARE(leaf->parent(), branch);
		QVERIFY(!old_leaf.isValid());
		QVERIFY(!tree.indexFromItem(branch).isValid());
		QVERIFY(!tree.indexFromItem(leaf).isValid());
		right->addChild(branch);
		QCOMPARE(branch->parent(), right);
		QCOMPARE(model->parent(tree.indexFromItem(branch)), tree.indexFromItem(right));
		QCOMPARE(model->parent(tree.indexFromItem(leaf)), tree.indexFromItem(branch));

		QSignalSpy changed(model, &QAbstractItemModel::dataChanged);
		leaf->setText(0, "renamed leaf");
		QCOMPARE(changed.count(), 1);
		QCOMPARE(changed[0][0].value<QModelIndex>(), tree.indexFromItem(leaf));
		QCOMPARE(model->data(tree.indexFromItem(leaf)).toString(), QString("renamed leaf"));
	}

	void filteredModelRemainsFlat_data()
	{
		QTest::addColumn<QString>("query");
		QTest::addColumn<int>("matches");
		QTest::newRow("zero") << QString("unmatched-query") << 0;
		QTest::newRow("one") << QString("alpha") << 1;
		QTest::newRow("many") << QString("beta") << 2;
	}

	void filteredModelRemainsFlat()
	{
		QFETCH(QString, query);
		QFETCH(int, matches);
		MainWindow window;
		QScopedValueRollback<MainWindow *> context(global->mainwindow, &window);
		FolderTreeView tree;
		auto *model = tree.model();
		QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
		auto *root = item("alpha");
		root->addChild(item("beta-one"));
		root->addChild(item("beta-two"));
		tree.addTopLevelItem(root);
		tree.setCurrentItem(root);
		tree.setFilter(query);
		QCOMPARE(model->rowCount(), matches);
		QCOMPARE(model->hasChildren(), matches > 0);
		QVERIFY(!model->index(matches, 0).isValid());
		for (int row = 0; row < matches; ++row) {
			QModelIndex index = model->index(row, 0);
			QVERIFY(index.isValid());
			QVERIFY(!model->parent(index).isValid());
			QCOMPARE(model->rowCount(index), 0);
			QVERIFY(!model->hasChildren(index));
			QVERIFY(!model->index(0, 0, index).isValid());
		}
		// Filtered indices are flat; structural changes must fail without
		// transferring ownership or spinning on children that cannot be taken.
		auto rejected = std::make_unique<FolderTreeItem>();
		QVERIFY(!root->addChild(rejected.get()));
		QVERIFY(rejected->parent() == nullptr);
		QVERIFY(!root->takeChild(0));
		QVERIFY(!root->clearChildren());
		QCOMPARE(root->childCount(), 2);
		tree.setFilter({});
		QCOMPARE(model->rowCount(), 1);
		QCOMPARE(model->rowCount(tree.indexFromItem(root)), 2);
		QVERIFY(root->addChild(rejected.get()));
		rejected.release();
		QPersistentModelIndex old_child = tree.indexFromItem(root->child(0));
		QVERIFY(root->clearChildren());
		QVERIFY(!old_child.isValid());
		QCOMPARE(root->childCount(), 0);
		QVERIFY(root->clearChildren());
		tree.clear();
		QCOMPARE(model->rowCount(), 0);
		QVERIFY(!model->hasChildren());
	}

	void viewFilteringEmitsOneReset_data()
	{
		QTest::addColumn<bool>("thumbnail");
		QTest::newRow("details") << false;
		QTest::newRow("thumbnails") << true;
	}

	void viewFilteringEmitsOneReset()
	{
		QFETCH(bool, thumbnail);
		MainWindow window;
		QScopedValueRollback<MainWindow *> context(global->mainwindow, &window);
		auto *model = window.fileitemmodel();
		auto *table = window.findChild<FileTableView *>("tableView");
		auto *thumbnails = window.findChild<ThumbnailView *>("thumbnailView");
		QVERIFY(table);
		QVERIFY(thumbnails);
		table->setModel(model);
		thumbnails->setModel(model);
		model->beginResetModel();
		for (QString const &name : {QString("alpha"), QString("beta")}) {
			FileItemModel::Item file;
			file.name = name;
			file.info.name = name;
			file.info.isdir = true;
			model->addItem(std::move(file));
		}
		model->endResetModel();
		QSignalSpy about_reset(model, &QAbstractItemModel::modelAboutToBeReset);
		QSignalSpy reset(model, &QAbstractItemModel::modelReset);
		auto set_filter = [&](QString const &text) {
			if (thumbnail) thumbnails->setFilterText(text);
			else table->setFilterText(text);
		};
		set_filter("alpha");
		QCOMPARE(about_reset.count(), 1);
		QCOMPARE(reset.count(), 1);
		QCOMPARE(model->rowCount(), 1);
		set_filter({});
		QCOMPARE(about_reset.count(), 2);
		QCOMPARE(reset.count(), 2);
		QCOMPARE(model->rowCount(), 2);
	}

	void replacingItemsKeepsUnfilteredRows_data()
	{
		QTest::addColumn<QString>("query");
		QTest::addColumn<int>("matches");
		QTest::newRow("all") << QString() << 2;
		QTest::newRow("one") << QString("alpha") << 1;
		QTest::newRow("none") << QString("unmatched-query") << 0;
	}

	void replacingItemsKeepsUnfilteredRows()
	{
		QFETCH(QString, query);
		QFETCH(int, matches);
		MainWindow window;
		QScopedValueRollback<MainWindow *> context(global->mainwindow, &window);
		auto *model = window.fileitemmodel();
		QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
		QSignalSpy about_reset(model, &QAbstractItemModel::modelAboutToBeReset);
		QSignalSpy reset(model, &QAbstractItemModel::modelReset);
		std::vector<FileItemModel::Item> items;
		for (QString const &name : {QString("alpha"), QString("beta")}) {
			FileItemModel::Item file;
			file.name = file.info.name = name;
			file.info.isdir = true;
			items.push_back(std::move(file));
		}
		model->replaceItems(Kind::Directory, std::move(items), query);
		QCOMPARE(about_reset.count(), 1);
		QCOMPARE(reset.count(), 1);
		QCOMPARE(model->rowCount(), matches);
		QCOMPARE(model->unfilteredCount(), 2);
		model->setFilterText({});
		QCOMPARE(model->rowCount(), 2);
	}
};

int runModelNotificationTests(int argc, char **argv)
{
	ModelNotificationTests tests;
	return QTest::qExec(&tests, argc, argv);
}

#include "model_notifications_tests.moc"
