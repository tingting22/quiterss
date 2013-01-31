#include <QDebug>
#include "updatethread.h"

UpdateThread::UpdateThread(QObject *parent, int requestTimeout) :
  QThread(parent),
  requestTimeout_(requestTimeout)
{
  qDebug() << "UpdateThread::constructor";
  start();
}

UpdateThread::~UpdateThread()
{
  qDebug() << "UpdateThread::~destructor";
}

void UpdateThread::run()
{
  QTimer timeout_;
  connect(&timeout_, SIGNAL(timeout()), this, SLOT(slotRequestTimeout()));
  timeout_.start(1000);

  QTimer getUrlTimer_;
  getUrlTimer_.setSingleShot(true);
  connect(this, SIGNAL(startGetUrlTimer()), &getUrlTimer_, SLOT(start()));
  connect(&getUrlTimer_, SIGNAL(timeout()), this, SLOT(getQueuedUrl()));

  UpdateObject updateObject_;
  connect(this, SIGNAL(signalHead(QNetworkRequest)),
          &updateObject_, SLOT(slotHead(QNetworkRequest)));
  connect(this, SIGNAL(signalGet(QNetworkRequest)),
          &updateObject_, SLOT(slotGet(QNetworkRequest)));
  connect(&updateObject_, SIGNAL(signalNetworkReply(QNetworkReply*)),
          this, SLOT(slotNetworkReply(QNetworkReply*)));
  connect(&updateObject_, SIGNAL(signalFinished(QNetworkReply*)),
          this, SLOT(finished(QNetworkReply*)));
  connect(&updateObject_.networkManager_,
          SIGNAL(authenticationRequired(QNetworkReply*,QAuthenticator*)),
          this, SIGNAL(signalAuthentication(QNetworkReply*,QAuthenticator*)),
          Qt::BlockingQueuedConnection);

  exec();
}

/*! \brief Постановка сетевого адреса в очередь запросов **********************/
void UpdateThread::requestUrl(const QString &urlString, const QDateTime &date,
                              const QString	&userInfo)
{
  urlsQueue_.enqueue(QUrl::fromEncoded(urlString.toLocal8Bit()));
  dateQueue_.enqueue(date);
  userInfo_.enqueue(userInfo);
  qDebug() << "urlsQueue_ <<" << urlString << "count=" << urlsQueue_.count();
}

/*! \brief Обработка очереди запросов *****************************************/
void UpdateThread::getQueuedUrl()
{
  if (REPLY_MAX_COUNT <= currentFeeds_.size()) return;

  if (!urlsQueue_.isEmpty()) {
    QUrl getUrl = urlsQueue_.dequeue();
    QUrl feedUrl = getUrl;
    QString userInfo = userInfo_.dequeue();
    if (!userInfo.isEmpty()) {
      getUrl.setUserInfo(userInfo);
      getUrl.addQueryItem("auth", "http");
    }
    QDateTime currentDate = dateQueue_.dequeue();
    qDebug() << "urlsQueue_ >>" << feedUrl << "count=" << urlsQueue_.count();
    head(getUrl, feedUrl, currentDate);
    if (currentFeeds_.size() < REPLY_MAX_COUNT) emit startGetUrlTimer();
  } else {
    qDebug() << "urlsQueue_ -- count=" << urlsQueue_.count();
    emit getUrlDone(urlsQueue_.count());
  }
}

/*! \brief Инициация сетевого запроса и подсоединение сигналов ****************/
void UpdateThread::get(const QUrl &getUrl, const QUrl &feedUrl, const QDateTime &date)
{
  qDebug() << objectName() << "::get:" << getUrl << "feed:" << feedUrl;
  QNetworkRequest request(getUrl);
  request.setRawHeader("User-Agent", "Opera/9.80 (Windows NT 6.1) Presto/2.12.388 Version/12.12");
  qDebug() << "request.rawHeaderList:" << request.rawHeaderList();
  emit signalGet(request);
  currentUrls_.append(getUrl);
  currentFeeds_.append(feedUrl);
  currentDates_.append(date);
  currentHead_.append(false);
  currentTime_.append(requestTimeout_);
}

void UpdateThread::head(const QUrl &getUrl, const QUrl &feedUrl, const QDateTime &date)
{
  qDebug() << objectName() << "::head:" << getUrl << "feed:" << feedUrl;
  QNetworkRequest request(getUrl);
  request.setRawHeader("User-Agent", "Opera/9.80 (Windows NT 6.1) Presto/2.10.229 Version/11.62");
  qDebug() << "request.rawHeaderList:" << request.rawHeaderList();
  emit signalHead(request);
  currentUrls_.append(getUrl);
  currentFeeds_.append(feedUrl);
  currentDates_.append(date);
  currentHead_.append(true);
  currentTime_.append(requestTimeout_);
}

/*! \brief Завершение обработки сетевого запроса

    The default behavior is to keep the text edit read only.

    If an error has occurred, the user interface is made available
    to the user for further input, allowing a new fetch to be
    started.

    If the HTTP get request has finished, we make the
    user interface available to the user for further input.
 *******************************************************************************/
void UpdateThread::finished(QNetworkReply *reply)
{
  qDebug() << "reply.finished():" << reply->url().toString();
  qDebug() << reply->header(QNetworkRequest::ContentTypeHeader);
  qDebug() << reply->header(QNetworkRequest::ContentLengthHeader);
  qDebug() << reply->header(QNetworkRequest::LocationHeader);
  qDebug() << reply->header(QNetworkRequest::LastModifiedHeader);
  qDebug() << reply->header(QNetworkRequest::CookieHeader);
  qDebug() << reply->header(QNetworkRequest::SetCookieHeader);

  int currentReplyIndex = currentUrls_.indexOf(reply->url());

  if (currentReplyIndex >= 0) {
    currentTime_.removeAt(currentReplyIndex);
    currentUrls_.removeAt(currentReplyIndex);
    QUrl feedUrl       = currentFeeds_.takeAt(currentReplyIndex);
    QDateTime feedDate = currentDates_.takeAt(currentReplyIndex);
    bool headOk = currentHead_.takeAt(currentReplyIndex);

    if (reply->error() != QNetworkReply::NoError) {
      qDebug() << "  error retrieving RSS feed:" << reply->error();
      if (!headOk) {
        emit readedXml(NULL, feedUrl);
        if (reply->error() == QNetworkReply::AuthenticationRequiredError)
          emit getUrlDone(-2);
        else
          emit getUrlDone(-1);
        getQueuedUrl();
      } else {
        get(reply->url(), feedUrl, feedDate);
      }
    }
    else {
      QUrl redirectionTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
      if (redirectionTarget.isValid()) {
        if (reply->operation() == QNetworkAccessManager::HeadOperation) {
          qDebug() << objectName() << "  head redirect...";
          if (redirectionTarget.host().isNull())
            redirectionTarget.setUrl("http://"+feedUrl.host()+redirectionTarget.toString());
          head(redirectionTarget, feedUrl, feedDate);
        } else {
          qDebug() << objectName() << "  get redirect...";
          if (redirectionTarget.host().isNull())
            redirectionTarget.setUrl("http://"+feedUrl.host()+redirectionTarget.toString());
          get(redirectionTarget, feedUrl, feedDate);
        }
      } else {
        QDateTime replyDate = reply->header(QNetworkRequest::LastModifiedHeader).toDateTime();
        QDateTime replyLocalDate = QDateTime(replyDate.date(), replyDate.time());

        qDebug() << feedDate << replyDate << replyLocalDate;
        qDebug() << feedDate.toMSecsSinceEpoch() << replyDate.toMSecsSinceEpoch() << replyLocalDate.toMSecsSinceEpoch();
        if ((reply->operation() == QNetworkAccessManager::HeadOperation) &&
            ((!feedDate.isValid()) || (!replyLocalDate.isValid()) || (feedDate < replyLocalDate))) {
          get(reply->url(), feedUrl, feedDate);
        }
        else {
          QByteArray data = reply->readAll();
          emit readedXml(data, feedUrl);
          emit getUrlDone(urlsQueue_.count(), replyLocalDate);

          getQueuedUrl();
        }
      }
    }
  } else {
    qCritical() << "Request Url error: " << reply->url().toString() << reply->errorString();
  }

  int replyIndex = requestUrl_.indexOf(reply->url());
  requestUrl_.removeAt(replyIndex);
  networkReply_.takeAt(replyIndex)->deleteLater();
}

void UpdateThread::setProxy(const QNetworkProxy proxy)
{
  networkProxy_ = proxy;
  if (QNetworkProxy::DefaultProxy == networkProxy_.type())
    QNetworkProxyFactory::setUseSystemConfiguration(true);
  else
    QNetworkProxy::setApplicationProxy(networkProxy_);
}

void UpdateThread::slotNetworkReply(QNetworkReply *reply)
{
  requestUrl_.append(reply->url());
  networkReply_.append(reply);
}

void UpdateThread::slotRequestTimeout()
{
  for (int i = currentTime_.count() - 1; i >= 0; i--) {
    int time = currentTime_.at(i) - 1;
    if (time <= 0) {
      QUrl url = currentUrls_.takeAt(i);
      currentTime_.removeAt(i);
      currentDates_.removeAt(i);
      currentHead_.removeAt(i);
      QUrl feedUrl = currentFeeds_.takeAt(i);
      emit readedXml(NULL, feedUrl);
      emit getUrlDone(-3);
      getQueuedUrl();

      int replyIndex = requestUrl_.indexOf(url);
      requestUrl_.removeAt(replyIndex);
      networkReply_.takeAt(replyIndex)->deleteLater();
    } else {
      currentTime_.replace(i, time);
    }
  }
}
