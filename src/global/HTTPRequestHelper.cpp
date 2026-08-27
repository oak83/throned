#include "include/global/HTTPRequestHelper.hpp"

#include <QNetworkProxy>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QFile>
#include <QApplication>
#include <QMap>
#include <QStringList>



#include "include/global/Configs.hpp"
#include "include/ui/mainwindow.h"
#include "include/global/DeviceDetailsHelper.hpp"

namespace Configs_network {
    namespace {
        QString configureProxy(QNetworkAccessManager &accessManager, bool forceProxy) {
            const auto &settings = Configs::dataManager->settingsRepo;
            // Once a profile is running, keep every application-owned request on its
            // dedicated loopback inbound.  This avoids bootstrap failures when the
            // direct route cannot reach GitHub, geo assets or subscription endpoints.
            const bool requested = settings->internal_proxy_port > 0 ||
                                   settings->net_use_proxy ||
                                   settings->spmode_system_proxy ||
                                   forceProxy;
            if (!requested) return {};
            if (settings->started_id < 0 && settings->internal_proxy_port <= 0) {
                return QObject::tr("Request with proxy but no profile started.");
            }

            QNetworkProxy proxy;
            proxy.setType(QNetworkProxy::HttpProxy);
            if (settings->internal_proxy_port > 0 && !settings->internal_proxy_auth.isEmpty()) {
                proxy.setHostName("127.0.0.1");
                proxy.setPort(settings->internal_proxy_port);
                proxy.setUser(settings->internal_proxy_auth);
                proxy.setPassword(settings->internal_proxy_auth);
            } else {
                proxy.setHostName(settings->inbound_address == "::" ? "127.0.0.1" : settings->inbound_address);
                proxy.setPort(settings->inbound_socks_port);
                if (settings->inbound_auth) {
                    proxy.setUser(settings->inbound_user);
                    proxy.setPassword(settings->inbound_pass);
                }
            }
            accessManager.setProxy(proxy);
            return {};
        }
    }

    HTTPResponse NetworkRequestHelper::HttpGet(const QString &url, bool sendHwid, bool useProxy) {
        QNetworkRequest request;
        QNetworkAccessManager accessManager;
        accessManager.setTransferTimeout(10000);
        request.setUrl(url);
        if (const auto proxyError = configureProxy(accessManager, useProxy); !proxyError.isEmpty())
            return HTTPResponse{proxyError};
        // Set attribute
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::KnownHeaders::UserAgentHeader, Configs::dataManager->settingsRepo->GetUserAgent());
        if (Configs::dataManager->settingsRepo->net_insecure) {
            QSslConfiguration c;
            c.setPeerVerifyMode(QSslSocket::PeerVerifyMode::VerifyNone);
            request.setSslConfiguration(c);
        }
        if (sendHwid) {
            auto details = GetDeviceDetails();

            QMap<QString, QString> customParams;
            if (!Configs::dataManager->settingsRepo->sub_custom_hwid_params.isEmpty()) {
                QStringList pairs = Configs::dataManager->settingsRepo->sub_custom_hwid_params.split(',');
                for (const QString &pair : pairs) {
                    QString trimmed = pair.trimmed();
                    int eqPos = trimmed.indexOf('=');
                    if (eqPos > 0) {
                        QString key = trimmed.left(eqPos).trimmed();
                        QString value = trimmed.mid(eqPos + 1).trimmed();
                        if (!key.isEmpty() && !value.isEmpty() &&
                            !value.contains('\n') && !value.contains('\r') &&
                            value.length() < 1000) {
                            QString lowerKey = key.toLower();
                            if (lowerKey == "hwid" || lowerKey == "os" ||
                                lowerKey == "osversion" || lowerKey == "model") {
                                customParams[lowerKey] = value;
                            }
                        }
                    }
                }
            }

            QString hwid = customParams.contains("hwid") ? customParams["hwid"] : details.hwid;
            QString os = customParams.contains("os") ? customParams["os"] : details.os;
            QString osVersion = customParams.contains("osversion") ? customParams["osversion"] : details.osVersion;
            QString model = customParams.contains("model") ? customParams["model"] : details.model;

            if (!hwid.isEmpty()) request.setRawHeader("x-hwid", hwid.toUtf8());
            if (!os.isEmpty()) request.setRawHeader("x-device-os", os.toUtf8());
            if (!osVersion.isEmpty()) request.setRawHeader("x-ver-os", osVersion.toUtf8());
            if (!model.isEmpty()) request.setRawHeader("x-device-model", model.toUtf8());
        }
        auto _reply = accessManager.get(request);
        connect(_reply, &QNetworkReply::sslErrors, _reply, [](const QList<QSslError> &errors) {
            QStringList error_str;
            for (const auto &err: errors) {
                error_str << err.errorString();
            }
            MW_show_log(QString("SSL Errors: %1 %2").arg(error_str.join(","), Configs::dataManager->settingsRepo->net_insecure ? "(Ignored)" : ""));
        });
        QEventLoop loop;
        connect(_reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        auto result = HTTPResponse{_reply->error() == QNetworkReply::NetworkError::NoError ? "" : _reply->errorString(),
                                       _reply->readAll(), _reply->rawHeaderPairs()};
        _reply->deleteLater();
        return result;
    }

    QString NetworkRequestHelper::GetHeader(const QList<QPair<QByteArray, QByteArray>> &header, const QString &name) {
        const QByteArray needle = name.toLatin1();
        for (const auto &p: header) {
            if (p.first.compare(needle, Qt::CaseInsensitive) == 0) return p.second;
        }
        return {};
    }

    QString NetworkRequestHelper::DownloadAsset(const QString &url, const QString &fileName, bool useProxy) {
        QNetworkRequest request;
        QNetworkAccessManager accessManager;
        accessManager.setTransferTimeout(30000);
        request.setUrl(url);
        if (const auto proxyError = configureProxy(accessManager, useProxy); !proxyError.isEmpty())
            return proxyError;
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        if (Configs::dataManager->settingsRepo->net_insecure) {
            QSslConfiguration c;
            c.setPeerVerifyMode(QSslSocket::PeerVerifyMode::VerifyNone);
            request.setSslConfiguration(c);
        }

        auto _reply = accessManager.get(request);
        connect(_reply, &QNetworkReply::sslErrors, _reply, [](const QList<QSslError> &errors) {
            QStringList error_str;
            for (const auto &err: errors) {
                error_str << err.errorString();
            }
            MW_show_log(QString("SSL Errors: %1 %2").arg(error_str.join(","), Configs::dataManager->settingsRepo->net_insecure ? "(Ignored)" : ""));
        });
        connect(_reply, &QNetworkReply::downloadProgress, _reply, [&](qint64 bytesReceived, qint64 bytesTotal)
        {
            runOnUiThread([=]{
                GetMainWindow()->setDownloadReport(DownloadProgressReport{fileName, bytesReceived, bytesTotal}, true);
                GetMainWindow()->UpdateDataView();
            });
        });
        QEventLoop loop;
        connect(_reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        runOnUiThread([=]
        {
            GetMainWindow()->setDownloadReport({}, false);
            GetMainWindow()->UpdateDataView(true);
        });
        auto netErr = _reply->error();
        const QString netErrStr = _reply->errorString();
        const int httpStatus = _reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = _reply->readAll();
        _reply->deleteLater();

        if (netErr != QNetworkReply::NetworkError::NoError) {
            return netErrStr;
        }

        if (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300)) {
            return QObject::tr("Download failed: server returned HTTP status %1.").arg(httpStatus);
        }
        if (body.isEmpty()) {
            return QObject::tr("Download failed: the server returned an empty response.");
        }

        const auto filePath = Configs::GetBasePath() + "/" + fileName;
        const auto tmpPath = filePath + ".tmp";
        QFile tmp(tmpPath);
        if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return QObject::tr("Could not open file.");
        }
        if (tmp.write(body) != body.size() || !tmp.flush()) {
            tmp.close();
            tmp.remove();
            return QObject::tr("Could not write file.");
        }
        tmp.close();
        QFile::remove(filePath);
        if (!tmp.rename(filePath)) {
            tmp.remove();
            return QObject::tr("Could not save downloaded file.");
        }
        return "";
    }

} // namespace Configs_network
