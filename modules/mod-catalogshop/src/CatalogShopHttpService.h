/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MOD_CATALOGSHOP_HTTP_SERVICE_H
#define MOD_CATALOGSHOP_HTTP_SERVICE_H

#include "AsyncAcceptor.h"
#include "CatalogShopHttpSession.h"
#include "HttpService.h"
#include "IoContext.h"
#include <boost/beast/http/field.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace ModCatalogShop
{
class CatalogShopHttpService final : public Trinity::Net::Http::HttpService<CatalogShopHttpSession>
{
public:
    using RequestHandlerResult = Trinity::Net::Http::RequestHandlerResult;
    using HttpRequestContext = Trinity::Net::Http::RequestContext;

    CatalogShopHttpService();

    static CatalogShopHttpService& Instance();

    bool StartFromConfig();
    void Stop();

    bool IsListening() const { return _listening.load(std::memory_order_acquire); }

    bool TryHandleCheckout(HttpRequestContext& context);

private:
    void RegisterBrowseHandlers();

    RequestHandlerResult HandleSso(std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context);
    RequestHandlerResult HandleDefaultCurrency(std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context);
    RequestHandlerResult HandleCurrentPages(std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context);
    RequestHandlerResult HandleProductsByStoreId(std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context);
    RequestHandlerResult HandleVcBalance(std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context);
    RequestHandlerResult HandleQuoteDynamicBundle(std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context);

    void ReplyJson(HttpRequestContext& context, std::string body, boost::beast::http::status status = boost::beast::http::status::ok) const;
    void ReplyHtml(HttpRequestContext& context, std::string body, boost::beast::http::status status = boost::beast::http::status::ok) const;

    std::string BuildLocalJwt(std::string_view scope) const;
    std::string ResolveFreeBuySignalDir() const;
    bool WriteFreeBuySignal(uint32 accountId, uint32 productId) const;
    bool StartComplementaryLoopback(std::string const& bindIp, uint16 port);
    static std::string ListenUrl(std::string const& bindIp, uint16 port);

    uint32 ResolveCheckoutAccountId(HttpRequestContext const& context) const;
    static uint32 ResolveCheckoutProductId(std::string_view path, std::string_view target, std::string_view body);
    static std::string HeaderValue(HttpRequestContext const& context, boost::beast::http::field field);
    static std::string ExtractXusToken(std::string_view haystack);
    static uint32 FindXusAccountId(std::string_view haystack);
    static std::string MakeCheckoutCompleteHtml(uint32 productId);
    static std::string MakeCheckoutConfirmHtml(uint32 productId, std::string_view token, bool autoSubmit);
    static std::string Base64UrlEncode(std::string_view raw);
    static std::unordered_map<std::string, std::string> ParseForm(std::string_view body);
    static uint32 ParseSsoAccountId(std::string_view token);
    static uint32 ParseProductIdFromPath(std::string_view path);
    static uint32 ParseProductIdFromMap(std::unordered_map<std::string, std::string> const& fields);
    static std::string HostWithoutPort(std::string_view hostHeader);
    static int64 NowUnixSeconds();
    static int64 NowUnixMillis();

    std::string _freeBuySignalDir;
    std::atomic<uint32> _lastSsoAccountId { 0 };

    std::shared_ptr<Trinity::Asio::IoContext> _ioContext;
    std::thread _ioThread;
    std::unique_ptr<Trinity::Net::AsyncAcceptor> _loopbackAcceptor;
    std::string _loopbackBindIp;
    std::atomic<bool> _listening { false };
    bool _handlersRegistered = false;
};
}

#define sCatalogShop ModCatalogShop::CatalogShopHttpService::Instance()

#endif // MOD_CATALOGSHOP_HTTP_SERVICE_H
