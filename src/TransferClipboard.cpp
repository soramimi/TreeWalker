#include "TransferClipboard.h"
#include <QUrl>
#include <QtEndian>

namespace filetransfer {
static const char *tokenFormat = "application/x-treewalker-transfer-token";
static const char *gnomeFormat = "x-special/gnome-copied-files";
static const char *effectFormat = "application/x-qt-windows-mime;value=\"Preferred DropEffect\"";
std::unique_ptr<QMimeData> clipboardData(QStringList const &paths, Operation operation, QString const &token)
{
	auto mime = std::make_unique<QMimeData>();
	QList<QUrl> urls;
	QByteArray gnome = operation == Operation::Move ? "cut\n" : "copy\n";
	for (auto const &path : paths) { QUrl url = QUrl::fromLocalFile(path); urls.push_back(url); gnome += url.toEncoded() + '\n'; }
	mime->setUrls(urls);
	mime->setData(gnomeFormat, gnome);
	mime->setData("application/x-kde-cutselection", operation == Operation::Move ? "1" : "0");
	quint32 effect = qToLittleEndian<quint32>(operation == Operation::Move ? 2 : 1);
	mime->setData(effectFormat, QByteArray(reinterpret_cast<char *>(&effect), sizeof(effect)));
	if (!token.isEmpty()) mime->setData(tokenFormat, token.toUtf8());
	return mime;
}
Request clipboardRequest(QMimeData const *mime)
{
	Request request;
	if (!mime) return request;
	QList<QUrl> urls = mime->urls();
	QByteArray gnome = mime->data(gnomeFormat);
	if (gnome.startsWith("cut\n") || gnome.startsWith("copy\n")) {
		if (gnome.startsWith("cut\n")) request.operation = Operation::Move;
		if (urls.isEmpty()) {
			auto lines = gnome.split('\n');
			for (int i = 1; i < lines.size(); ++i) if (!lines[i].trimmed().isEmpty()) urls.push_back(QUrl::fromEncoded(lines[i].trimmed(), QUrl::StrictMode));
		}
	}
	if (mime->data("application/x-kde-cutselection") == "1") request.operation = Operation::Move;
	QByteArray effect = mime->data(effectFormat);
	if (effect.size() == 4 && qFromLittleEndian<quint32>(effect.constData()) == 2) request.operation = Operation::Move;
	for (auto const &url : urls) {
		if (!url.isValid() || !url.isLocalFile() || url.hasQuery() || url.hasFragment()) { request.sources.clear(); return request; }
		request.sources.push_back(url.toLocalFile());
	}
	return request;
}
QString clipboardToken(QMimeData const *mime) { return mime ? QString::fromUtf8(mime->data(tokenFormat)) : QString(); }
}
