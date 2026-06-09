#ifndef ITEMIDLIST_H
#define ITEMIDLIST_H

#include <QByteArray>
#include <QString>
#include <QMetaType>

class ItemIdList {
	friend class FileItem;
	friend class WindowsFileSystemProvider;
public:
	enum class Type {
		PATH,
		WIN_SHELL_ITEMIDLIST,
	};
private:
	struct Data {
		Type type = Type::PATH;
		QByteArray iidl;
	} d;
public:
	QByteArray iidl() const { return d.iidl; }
	QByteArray &iidl() { return d.iidl; }
public:
	ItemIdList() = default;
	ItemIdList(QByteArray const &iidl);
	ItemIdList(QString const &path);
	Type type() const { return d.type; }
	int size() const { return d.iidl.size(); }
	char *data() { return d.iidl.data(); }
	char const *data() const { return d.iidl.data(); }
	bool empty() const { return d.iidl.isEmpty(); }
	QString path() const;
	uint8_t operator[](int i) const { return static_cast<uint8_t>(d.iidl[i]); }
	int compare(ItemIdList const &r) const
	{
		if (d.type != r.d.type) {
			return (d.type < r.d.type) ? -1 : 1;
		}
		return d.iidl.compare(r.d.iidl);
	}
	bool operator == (ItemIdList const &r) const
	{
		return compare(r) == 0;
	}
	bool operator != (ItemIdList const &r) const
	{
		return !operator == (r);
	}
	bool operator < (ItemIdList const &r) const
	{
		return compare(r) < 0;
	}
};
Q_DECLARE_METATYPE(ItemIdList)

#endif // ITEMIDLIST_H
