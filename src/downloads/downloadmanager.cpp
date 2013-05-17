/* ============================================================
* QuiteRSS is a open-source cross-platform RSS/Atom news feeds reader
* Copyright (C) 2011-2013 QuiteRSS Team <quiterssteam@gmail.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
* ============================================================ */

#include "downloadmanager.h"
#include "downloaditem.h"
#include "rsslisting.h"

DownloadManager::DownloadManager(QWidget *parentWidget, QWidget *parent)
  : QWidget(parent)
{
  setWindowFlags(windowFlags() ^ Qt::WindowMaximizeButtonHint);
  setWindowTitle(tr("Download Manager"));
  setMinimumWidth(400);
  setMinimumHeight(300);

  rssl_ = qobject_cast<RSSListing*>(parentWidget);

  networkManager_ = new NetworkManager(this);
  networkManager_->setCookieJar(rssl_->cookieJar_);

  listWidget_ = new QListWidget();
  listWidget_->setFrameStyle(QFrame::NoFrame);

  clearButton_ = new QPushButton(tr("Clear"));

  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->setMargin(5);
  buttonLayout->addWidget(clearButton_);
  buttonLayout->addStretch();

  QVBoxLayout *mainLayout = new QVBoxLayout();
  mainLayout->setMargin(0);
  mainLayout->setSpacing(0);
  mainLayout->addWidget(listWidget_);
  mainLayout->addLayout(buttonLayout);
  setLayout(mainLayout);

  connect(clearButton_, SIGNAL(clicked()), this, SLOT(clearList()));
  connect(this, SIGNAL(signalItemCreated(QListWidgetItem*,DownloadItem*)),
          this, SLOT(itemCreated(QListWidgetItem*,DownloadItem*)));
}

DownloadManager::~DownloadManager()
{
  networkManager_->cookieJar()->setParent(rssl_);
}

void DownloadManager::download(const QNetworkRequest &request)
{
  handleUnsupportedContent(networkManager_->get(request));
}

void DownloadManager::handleUnsupportedContent(QNetworkReply* reply)
{
  QString fileName(getFileName(reply));
  QFileInfo fileInfo(fileName);
  QString filter = QString(tr("File %1 (*.%2)") + ";;" + tr("All Files (*.*)")).
      arg(fileInfo.suffix().toUpper()).
      arg(fileInfo.suffix().toLower());
  fileName = QFileDialog::getSaveFileName(0, tr("Save As..."), fileName, filter);
  if (fileName.isNull()) {
    reply->abort();
    reply->deleteLater();
    return;
  }

  QListWidgetItem *item = new QListWidgetItem(listWidget_);
  DownloadItem *downItem = new DownloadItem(item, reply, fileName, false, this);
  emit signalItemCreated(item, downItem);
}

QString DownloadManager::getFileName(QNetworkReply* reply)
{
  QString path;
  if (reply->hasRawHeader("Content-Disposition")) {
    QString value = QString::fromLatin1(reply->rawHeader("Content-Disposition"));

    // We try to use UTF-8 encoded filename first if present
    if (value.contains(QRegExp("filename\\s*\\*\\s*=\\s*UTF-8", Qt::CaseInsensitive))) {
      QRegExp reg("filename\\s*\\*\\s*=\\s*UTF-8''([^;]*)", Qt::CaseInsensitive);
      reg.indexIn(value);
      path = QUrl::fromPercentEncoding(reg.cap(1).toUtf8()).trimmed();
    }
    else if (value.contains(QRegExp("filename\\s*=", Qt::CaseInsensitive))) {
      QRegExp reg("filename\\s*=([^;]*)", Qt::CaseInsensitive);
      reg.indexIn(value);
      path = reg.cap(1).trimmed();

      if (path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"'))) {
        path = path.mid(1, path.length() - 2);
      }
    }
  }

  if (path.isEmpty()) {
    path = reply->url().path();
  }

  QFileInfo info(path);
  QString baseName = info.completeBaseName();
  QString endName = info.suffix();

  if (baseName.isEmpty()) {
    baseName = "no_name";
  }

  if (!endName.isEmpty()) {
    endName.prepend(QLatin1Char('.'));
  }

  QString name = baseName + endName;

  if (name.contains(QLatin1Char('"'))) {
    name.remove(QLatin1String("\";"));
  }

  return name;
}

void DownloadManager::startExternalApp(const QString &executable, const QUrl &url)
{
  QStringList arguments;
  arguments.append(url.toEncoded());

  bool success = QProcess::startDetached(executable, arguments);

  if (!success) {
    QString info = "<ul><li><b>%1</b>%2</li><li><b>%3</b>%4</li></ul>";
    info = info.arg(tr("Executable: "), executable,
                    tr("Arguments: "), arguments.join(QLatin1String(" ")));

    QMessageBox::critical(0, QObject::tr("Cannot start external program"),
                          QObject::tr("Cannot start external program! %1").arg(info));
  }
}

void DownloadManager::itemCreated(QListWidgetItem* item, DownloadItem* downItem)
{
  connect(downItem, SIGNAL(deleteItem(DownloadItem*)), this, SLOT(deleteItem(DownloadItem*)));

  listWidget_->setItemWidget(item, downItem);
  item->setSizeHint(downItem->sizeHint());
  downItem->show();

  show();
  raise();
  activateWindow();
}

void DownloadManager::deleteItem(DownloadItem* item)
{
  if (item && !item->isDownloading()) {
    delete item;
  }
}

void DownloadManager::clearList()
{
  QList<DownloadItem*> items;
  for (int i = 0; i < listWidget_->count(); i++) {
    DownloadItem* downItem = qobject_cast<DownloadItem*>(listWidget_->itemWidget(listWidget_->item(i)));
    if (!downItem) {
      continue;
    }
    if (downItem->isDownloading()) {
      continue;
    }
    items.append(downItem);
  }
  qDeleteAll(items);
}