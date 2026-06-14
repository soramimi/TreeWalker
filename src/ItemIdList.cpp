#include "ItemIdList.h"
#include "ApplicationGlobal.h"

#include "common/realpath.h"
#include "common/str.h"
#include "common/misc.h"

#ifdef Q_OS_WIN
#include "WindowsFileSystemProvider.h"
#include "WindowsShellAPI.h"
#endif

std::string iidlBinaryToString(QByteArray const &ba)
{
	std::string hex;
	char const *begin = ba.data();
	char const *end = ba.data() + ba.size();
	char const *ptr = begin;
	while (ptr < end) {
		uint16_t len = *(uint16_t *)ptr;
		if (len == 0) break;
		if (len <= 2 || ptr + len >= end) {
			return {};
		}
		for (int i = 2; i < len; i++) {
			char tmp[3];
			sprintf(tmp, "%02x", (uint8_t)ptr[i]);
			hex += tmp;
		}
		hex += '/';
		ptr += len;
	}
	return (std::string)prefix_itemidlist + hex;
}

QByteArray iidlStringToBinary(std::string const hex)
{
	QByteArray ba;
	if (misc::starts_with(hex, prefix_itemidlist)) {
		char const *begin = hex.data() + prefix_itemidlist.size();
		char const *end = hex.data() + hex.size();
		char const *ptr = begin;
		while (ptr < end) {
			QByteArray ba2;
			while (ptr < end) {
				if (*ptr == '/') {
					ptr++;
					break;
				}
				if (ptr + 1 < end) {
					char tmp[3] = {ptr[0], ptr[1], 0};
					uint8_t c = (uint8_t)strtoul(tmp, nullptr, 16);
					ba2.push_back(c);
					ptr += 2;
				} else {
					return {};
				}
			}
			if (ba2.isEmpty()) return {};
			uint16_t len = ba2.size() + 2;
			ba.push_back((char)(len & 0xff));
			ba.push_back((char)((len >> 8) & 0xff));
			ba += ba2;
		}
		ba.push_back((char)0);
		ba.push_back((char)0);
		return ba;
	}
	return {};
}

ItemIdList::ItemIdList(const QByteArray &iidl)
{
	d.iidl = iidl;
	d.type = ItemIdList::Type::WIN_SHELL_ITEMIDLIST;
}

ItemIdList::ItemIdList(QString const &path)
{
#ifdef Q_OS_WIN
	if (path.startsWith("//") && path.indexOf("//", 2) > 2) {
		if (path.startsWith((misc::str)prefix_itemidlist)) {
			QByteArray iidl = QByteArray::fromHex(path.mid(prefix_itemidlist.size()).toUtf8());
			int n = iidl.size();
			while (n > 0 && iidl[n - 1] == 0) {
				n--;
			}
			iidl.resize(n);
			iidl.push_back((uint8_t)0); // two zeros for terminating the ITEMIDLIST
			iidl.push_back((uint8_t)0);
			d.iidl = iidl;
			d.type = ItemIdList::Type::WIN_SHELL_ITEMIDLIST;
		} else {
			d.iidl = path.toUtf8();
			d.type = ItemIdList::Type::PATH;
		}
	} else {
		QString path2 = path;
		path2.replace('/', '\\');
		d.iidl = global->shapi->parseDisplayName(path2);
		d.type = ItemIdList::Type::WIN_SHELL_ITEMIDLIST;
	}
#else
	if (path.startsWith("//") && path.indexOf("//", 2) > 2) {
		d.iidl = path.toUtf8(); // realpathしない
	} else {
		d.iidl = misc::realpath(path).toUtf8();
	}
	d.type = ItemIdList::Type::PATH;
#endif
}

QString ItemIdList::path() const
{
	if (d.iidl.isEmpty()) return {};

	if (d.type == ItemIdList::Type::PATH) {
		return QString::fromUtf8(d.iidl);
	}

#ifdef Q_OS_WIN
	if (d.type == ItemIdList::Type::WIN_SHELL_ITEMIDLIST) {
		return global->shapi->getPathFromIDList(d.iidl);
	}
#else
	return QString::fromUtf8(d.iidl);
#endif
	return {};
}
