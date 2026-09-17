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

#include "CatalogShopHttpService.h"
#include "AsyncAcceptor.h"
#include "Base64.h"
#include "CatalogShopSslContext.h"
#include "Config.h"
#include "Log.h"
#include "StringConvert.h"
#include "Util.h"
#include "World.h"
#include <boost/beast/http/field.hpp>
#include <boost/system/system_error.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace
{
constexpr char const* VC_PLACEMENT = "9aad42ec-a68c-487b-a300-b67518267a85";
constexpr char const* POP_PLACEMENT = "b94aa31b-f910-4ec8-a180-3fb6bdac3215";
constexpr char const* CHECKOUT_HOST = "us.checkout.battle.net";

constexpr uint64 CATALOG_SHOP_L80_ENHANCED_PRODUCT_ID = 1969052;
constexpr uint64 CATALOG_SHOP_L80_ENHANCED_CHILD_PRODUCT_ID = 1960794;
constexpr uint64 CATALOG_SHOP_L80_STANDARD_PRODUCT_ID = 1977499;

#include "CatalogShopRetailResponses.inc"

bool IsFreeL80CommerceProduct(uint64 productId)
{
    return productId == CATALOG_SHOP_L80_ENHANCED_PRODUCT_ID
        || productId == CATALOG_SHOP_L80_ENHANCED_CHILD_PRODUCT_ID
        || productId == CATALOG_SHOP_L80_STANDARD_PRODUCT_ID;
}

std::string UrlDecode(std::string_view in)
{
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i)
    {
        if (in[i] == '+')
            out.push_back(' ');
        else if (in[i] == '%' && i + 2 < in.size())
        {
            auto hex = Trinity::StringTo<uint8>(in.substr(i + 1, 2), 16);
            if (!hex)
            {
                out.push_back(in[i]);
                continue;
            }
            out.push_back(static_cast<char>(*hex));
            i += 2;
        }
        else
            out.push_back(in[i]);
    }
    return out;
}

std::string MakeMetaJson(int64 serverTimeMs)
{
    return Trinity::StringFormat(
        R"("metaData":{{"cacheControl":{{"maxAgeDurationS":3600}},"serverTimeMs":{}}})",
        serverTimeMs);
}

// Request bodies come from whoever connects. Iterative parsing keeps a deeply nested body
// from overflowing the network thread's stack.
void ParseRequestBody(rapidjson::Document& doc, std::string const& body)
{
    doc.Parse<rapidjson::kParseIterativeFlag>(body.data(), body.size());
}

std::string SerializeJson(rapidjson::Document const& doc)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    return buffer.GetString();
}

std::string RefreshRetailResponse(std::string_view json, int64 serverTimeMs)
{
    rapidjson::Document doc;
    doc.Parse(json.data(), json.size());
    if (doc.IsObject() && doc.HasMember("metaData") && doc["metaData"].IsObject())
    {
        rapidjson::Value& metaData = doc["metaData"];
        if (metaData.HasMember("serverTimeMs"))
            metaData["serverTimeMs"].SetInt64(serverTimeMs);
    }
    return SerializeJson(doc);
}

std::string FilterRetailPlacements(std::vector<std::string> const& placementIds, int64 serverTimeMs)
{
    rapidjson::Document source;
    source.Parse(RetailChildPlacementPages.data(), RetailChildPlacementPages.size());

    rapidjson::Document out;
    out.SetObject();
    rapidjson::Document::AllocatorType& allocator = out.GetAllocator();
    out.AddMember("error", rapidjson::Value().SetNull(), allocator);

    rapidjson::Value metaData(source["metaData"], allocator);
    metaData["serverTimeMs"].SetInt64(serverTimeMs);
    out.AddMember("metaData", metaData, allocator);

    rapidjson::Value placements(rapidjson::kArrayType);
    for (rapidjson::Value const& placement : source["placements"].GetArray())
    {
        if (!placement.HasMember("placementId") || !placement["placementId"].IsString())
            continue;

        std::string_view id = placement["placementId"].GetString();
        if (std::find_if(placementIds.begin(), placementIds.end(),
            [id](std::string const& requested) { return requested == id; }) != placementIds.end())
            placements.PushBack(rapidjson::Value(placement, allocator), allocator);
    }
    out.AddMember("placements", placements, allocator);
    return SerializeJson(out);
}
}

namespace ModCatalogShop
{
CatalogShopHttpService::CatalogShopHttpService()
    : HttpService("catalogshop")
{
}

CatalogShopHttpService& CatalogShopHttpService::Instance()
{
    static CatalogShopHttpService instance;
    return instance;
}

bool CatalogShopHttpService::StartFromConfig()
{
    if (_listening.load(std::memory_order_acquire))
        return true;

    if (!sConfigMgr->GetBoolDefault("CatalogShop.Enable", false))
    {
        TC_LOG_INFO("module.catalogshop", "CatalogShop HTTPS listener disabled (CatalogShop.Enable = 0)");
        return true;
    }

    std::string configuredBindIp = sConfigMgr->GetStringDefault("CatalogShop.BindIP", "127.0.0.1");
    std::string bindIp = configuredBindIp == "localhost" ? "127.0.0.1" : configuredBindIp;
    uint16 port = uint16(sConfigMgr->GetIntDefault("CatalogShop.Port", 443));
    int32 threads = sConfigMgr->GetIntDefault("CatalogShop.Threads", 1);
    if (threads < 1)
        threads = 1;

    std::string certFile = sConfigMgr->GetStringDefault("CatalogShop.CertificateFile",
        "temp/catalogshop-certs/catalogshop-localhost.pem");
    std::string keyFile = sConfigMgr->GetStringDefault("CatalogShop.PrivateKeyFile",
        "temp/catalogshop-certs/catalogshop-localhost-key.pem");
    _freeBuySignalDir = ResolveFreeBuySignalDir();
    if (!_freeBuySignalDir.empty())
    {
        std::error_code dirEc;
        std::filesystem::create_directories(_freeBuySignalDir, dirEc);
        if (dirEc)
            TC_LOG_ERROR("module.catalogshop", "Failed to create CatalogShop Free Buy signal dir '{}': {}", _freeBuySignalDir, dirEc.message());
    }

    std::error_code existsEc;
    if (!std::filesystem::exists(certFile, existsEc) || existsEc)
    {
        TC_LOG_ERROR("module.catalogshop", "CatalogShop certificate file is missing: '{}' (SAN must include DNS:localhost)", certFile);
        return false;
    }
    if (!std::filesystem::exists(keyFile, existsEc) || existsEc)
    {
        TC_LOG_ERROR("module.catalogshop", "CatalogShop private key file is missing: '{}'", keyFile);
        return false;
    }

    if (!SslContext::Initialize(certFile, keyFile))
        return false;

    RegisterBrowseHandlers();

    _ioContext = std::make_shared<Trinity::Asio::IoContext>();
    if (!StartNetwork(*_ioContext, bindIp, port, threads))
    {
        TC_LOG_ERROR("module.catalogshop", "Failed to bind CatalogShop HTTPS on {}:{} (elevated shell needed for port 443 on Windows)", bindIp, port);
        TC_LOG_ERROR("server.worldserver", "CatalogShop HTTPS failed to bind {}", ListenUrl(bindIp, port));
        _ioContext.reset();
        return false;
    }
    TC_LOG_INFO("server.worldserver", "CatalogShop HTTPS listening on {}", ListenUrl(bindIp, port));

    // Windows resolves localhost as ::1 first, then 127.0.0.1. IPv4-only bind misses the first dial.
    if (!StartComplementaryLoopback(bindIp, port))
    {
        TC_LOG_ERROR("server.worldserver", "CatalogShop HTTPS requires both loopback listeners when BindIP is '{}'", configuredBindIp);
        StopNetwork();
        _ioContext.reset();
        return false;
    }

    _ioThread = std::thread([ctx = _ioContext]()
    {
        ctx->run();
    });

    _listening.store(true, std::memory_order_release);
    TC_LOG_INFO("server.worldserver", "CatalogShop HTTPS ready for https://localhost (Free Buy signal dir '{}')",
        _freeBuySignalDir);
    TC_LOG_INFO("module.catalogshop", "Stop any Python shop2 stub — this module owns browse and local checkout");
    return true;
}

void CatalogShopHttpService::Stop()
{
    if (!_listening.exchange(false, std::memory_order_acq_rel) && !_ioContext)
        return;

    if (_loopbackAcceptor)
        _loopbackAcceptor->Close();

    StopNetwork();
    _loopbackAcceptor.reset();
    _loopbackBindIp.clear();

    if (_ioContext)
        _ioContext->stop();

    if (_ioThread.joinable())
        _ioThread.join();

    _ioContext.reset();
    TC_LOG_INFO("module.catalogshop", "CatalogShop HTTPS listener stopped");
}

bool CatalogShopHttpService::StartComplementaryLoopback(std::string const& bindIp, uint16 port)
{
    if (bindIp == "127.0.0.1")
        _loopbackBindIp = "::1";
    else
        return true;

    try
    {
        _loopbackAcceptor = std::make_unique<Trinity::Net::AsyncAcceptor>(*_ioContext, _loopbackBindIp, port);
    }
    catch (boost::system::system_error const& err)
    {
        TC_LOG_ERROR("server.worldserver", "CatalogShop HTTPS failed to open complementary listener {}:{}: {}",
            _loopbackBindIp, port, err.what());
        _loopbackAcceptor.reset();
        _loopbackBindIp.clear();
        return false;
    }

    if (!_loopbackAcceptor->Bind())
    {
        TC_LOG_ERROR("server.worldserver", "CatalogShop HTTPS failed to bind complementary {} (Windows localhost uses ::1 first)",
            ListenUrl(_loopbackBindIp, port));
        _loopbackAcceptor.reset();
        _loopbackBindIp.clear();
        return false;
    }

    _loopbackAcceptor->AsyncAccept(
        [this] { return SelectThreadWithMinConnections(); },
        [this](Trinity::Net::IoContextTcpSocket&& sock) { OnSocketOpen(std::move(sock)); });

    TC_LOG_INFO("server.worldserver", "CatalogShop HTTPS listening on {}", ListenUrl(_loopbackBindIp, port));
    return true;
}

std::string CatalogShopHttpService::ListenUrl(std::string const& bindIp, uint16 port)
{
    if (bindIp.find(':') != std::string::npos)
        return Trinity::StringFormat("https://[{}]:{}", bindIp, port);
    return Trinity::StringFormat("https://{}:{}", bindIp, port);
}

void CatalogShopHttpService::RegisterBrowseHandlers()
{
    if (_handlersRegistered)
        return;

    RegisterHandler(boost::beast::http::verb::post, "/sso"sv,
        [this](std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context)
        {
            return HandleSso(std::move(session), context);
        });

    RegisterHandler(boost::beast::http::verb::post, "/PurchaseService/v1/GetAccountDefaultCurrency"sv,
        [this](std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context)
        {
            return HandleDefaultCurrency(std::move(session), context);
        });

    RegisterHandler(boost::beast::http::verb::post, "/StorefrontService/v1/GetCurrentPages"sv,
        [this](std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context)
        {
            return HandleCurrentPages(std::move(session), context);
        });

    RegisterHandler(boost::beast::http::verb::post, "/ProductCatalogService/v1/GetProductsByStoreId"sv,
        [this](std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context)
        {
            return HandleProductsByStoreId(std::move(session), context);
        });

    RegisterHandler(boost::beast::http::verb::post, "/VirtualCurrencyLedgerService/v1/GetBalance"sv,
        [this](std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context)
        {
            return HandleVcBalance(std::move(session), context);
        });

    RegisterHandler(boost::beast::http::verb::post, "/PurchaseService/v1/QuoteDynamicBundle"sv,
        [this](std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context)
        {
            return HandleQuoteDynamicBundle(std::move(session), context);
        });

    _handlersRegistered = true;
}

void CatalogShopHttpService::ReplyJson(HttpRequestContext& context, std::string body, boost::beast::http::status status) const
{
    context.response.result(status);
    context.response.set(boost::beast::http::field::content_type, "application/json");
    context.response.set(boost::beast::http::field::cache_control, "no-store");
    context.response.keep_alive(false);
    context.response.body() = std::move(body);
}

void CatalogShopHttpService::ReplyHtml(HttpRequestContext& context, std::string body, boost::beast::http::status status) const
{
    context.response.result(status);
    context.response.set(boost::beast::http::field::content_type, "text/html; charset=utf-8");
    context.response.set(boost::beast::http::field::cache_control, "no-store");
    context.response.keep_alive(false);
    context.response.body() = std::move(body);
}

bool CatalogShopHttpService::TryHandleCheckout(HttpRequestContext& context)
{
    auto hostIt = context.request.find(boost::beast::http::field::host);
    if (hostIt == context.request.end())
        return false;

    std::string const host = HostWithoutPort(Trinity::Net::Http::ToStdStringView(hostIt->value()));
    if (host != CHECKOUT_HOST)
        return false;

    std::string_view const target = Trinity::Net::Http::ToStdStringView(context.request.target());
    std::string_view path = target;
    size_t queryIndex = path.find('?');
    if (queryIndex != std::string_view::npos)
        path = path.substr(0, queryIndex);

    bool const isClientPurchase = path.find("/client-purchase/") != std::string_view::npos;
    bool const isLoading = path.find("/blizzard-checkout/loading") != std::string_view::npos;

    uint32 productId = ResolveCheckoutProductId(path, target, context.request.body());
    uint32 accountId = ResolveCheckoutAccountId(context);

    if (isClientPurchase || isLoading)
    {
        if (!productId)
            productId = uint32(CATALOG_SHOP_L80_ENHANCED_PRODUCT_ID);

        bool wrote = false;
        if (accountId && IsFreeL80CommerceProduct(productId))
            wrote = WriteFreeBuySignal(accountId, productId);

        TC_LOG_INFO("server.worldserver",
            "CatalogShop checkout Free Buy host={} path={} productId={} account={} signal={}",
            host, path, productId, accountId, wrote ? "wrote" : "skip");
        TC_LOG_INFO("module.catalogshop",
            "Checkout Free Buy host={} path={} productId={} account={} signal={}",
            host, path, productId, accountId, wrote ? "wrote" : "skip");

        if (wrote)
        {
            ReplyHtml(context, MakeCheckoutCompleteHtml(productId));
            return true;
        }

        std::string token = ExtractXusToken(target);
        if (token.empty())
            token = ExtractXusToken(context.request.body());
        if (token.empty())
            token = ExtractXusToken(HeaderValue(context, boost::beast::http::field::cookie));
        // Auto-POST only from loading. client-purchase already is that POST; do not loop.
        ReplyHtml(context, MakeCheckoutConfirmHtml(productId, token, !isClientPurchase));
        return true;
    }

    TC_LOG_INFO("server.worldserver", "CatalogShop checkout shell host={} path={}", host, path);
    TC_LOG_INFO("module.catalogshop", "Checkout shell host={} path={}", host, path);
    ReplyHtml(context,
        R"(<!DOCTYPE html><html><head><meta charset="utf-8"><title></title></head><body></body></html>)");
    return true;
}

CatalogShopHttpService::RequestHandlerResult CatalogShopHttpService::HandleSso(
    std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context)
{
    auto form = ParseForm(context.request.body());
    std::string scope = form.contains("scope") ? form["scope"] : std::string();
    std::string ssoToken = form.contains("token") ? form["token"] : std::string();
    uint32 accountId = ParseSsoAccountId(ssoToken);
    if (!accountId)
        accountId = FindXusAccountId(context.request.body());
    if (accountId)
        _lastSsoAccountId.store(accountId, std::memory_order_release);
    // JWT `sub` only. Free Buy never falls back to account 1 when SSO does not parse.
    uint32 jwtAccountId = accountId ? accountId : 1;

    std::string token = BuildLocalJwt(scope);
    int64 now = NowUnixSeconds();

    rapidjson::Document doc;
    doc.SetObject();
    auto& a = doc.GetAllocator();
    doc.AddMember("access_token", rapidjson::Value(token.c_str(), a), a);
    doc.AddMember("token_type", "bearer", a);
    doc.AddMember("expires_in", 86399, a);
    doc.AddMember("scope", rapidjson::Value(scope.c_str(), a), a);
    std::string sub = std::to_string(jwtAccountId);
    doc.AddMember("sub", rapidjson::Value(sub.c_str(), a), a);
    doc.AddMember("account_id", jwtAccountId, a);

    rapidjson::Value accountRoles(rapidjson::kArrayType);
    accountRoles.PushBack("ROLE_USER", a);
    doc.AddMember("account_roles", accountRoles, a);

    rapidjson::Value clientRoles(rapidjson::kArrayType);
    clientRoles.PushBack("ROLE_FIRST_PARTY_CLIENT", a);
    clientRoles.PushBack("ROLE_CLIENT", a);
    clientRoles.PushBack("ROLE_GAME_CLIENT", a);
    doc.AddMember("authorities", rapidjson::Value(clientRoles, a), a);
    doc.AddMember("client_roles", clientRoles, a);
    doc.AddMember("account_authorities", rapidjson::Value(rapidjson::kArrayType), a);
    doc.AddMember("client_authorities", rapidjson::Value(rapidjson::kArrayType), a);
    doc.AddMember("active", "true", a);

    rapidjson::Value programs(rapidjson::kArrayType);
    programs.PushBack("WoW", a);
    doc.AddMember("programs", programs, a);
    doc.AddMember("env", "local", a);
    std::string iss = "https://auth.catalogshop.local";
    doc.AddMember("iss", rapidjson::Value(iss.c_str(), a), a);
    doc.AddMember("iat", now, a);
    doc.AddMember("game_accounts", rapidjson::Value(rapidjson::kArrayType), a);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    TC_LOG_INFO("server.worldserver", "CatalogShop POST /sso → 200 local JWT account={} client={}",
        jwtAccountId, session->GetClientInfo());
    TC_LOG_INFO("module.catalogshop", "POST /sso → 200 local JWT account={} client={}",
        jwtAccountId, session->GetClientInfo());
    ReplyJson(context, buffer.GetString());
    return RequestHandlerResult::Handled;
}

CatalogShopHttpService::RequestHandlerResult CatalogShopHttpService::HandleDefaultCurrency(
    std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context)
{
    ReplyJson(context, R"({"currencyAlphaCode":"USD","error":null})");
    TC_LOG_INFO("module.catalogshop", "POST /PurchaseService/v1/GetAccountDefaultCurrency → 200 client={}",
        session->GetClientInfo());
    return RequestHandlerResult::Handled;
}

CatalogShopHttpService::RequestHandlerResult CatalogShopHttpService::HandleCurrentPages(
    std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context)
{
    rapidjson::Document req;
    ParseRequestBody(req, context.request.body());

    std::vector<std::string> placementIds;
    if (req.IsObject() && req.HasMember("placementIds") && req["placementIds"].IsArray())
    {
        for (auto const& id : req["placementIds"].GetArray())
            if (id.IsString())
                placementIds.emplace_back(id.GetString());
    }
    if (placementIds.empty())
        placementIds.emplace_back(POP_PLACEMENT);

    std::string body;
    if (placementIds.size() == 1 && placementIds.front() == VC_PLACEMENT)
        body = RefreshRetailResponse(RetailVirtualCurrencyPage, NowUnixMillis());
    else if (placementIds.size() == 1 && placementIds.front() == POP_PLACEMENT)
        body = RefreshRetailResponse(RetailPlacementPage, NowUnixMillis());
    else
        body = FilterRetailPlacements(placementIds, NowUnixMillis());

    TC_LOG_INFO("module.catalogshop", "POST /StorefrontService/v1/GetCurrentPages → 200 placements={} client={}",
        placementIds.size(), session->GetClientInfo());
    ReplyJson(context, std::move(body));
    return RequestHandlerResult::Handled;
}

CatalogShopHttpService::RequestHandlerResult CatalogShopHttpService::HandleProductsByStoreId(
    std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context)
{
    rapidjson::Document req;
    ParseRequestBody(req, context.request.body());

    bool empty = false;
    if (req.IsObject() && req.HasMember("modifiedSinceFilterMs") && !req["modifiedSinceFilterMs"].IsNull())
        empty = true;
    else if (req.IsObject() && req.HasMember("paginationToken") && !req["paginationToken"].IsNull()
        && !(req["paginationToken"].IsString() && req["paginationToken"].GetStringLength() == 0))
        empty = true;

    std::string body;
    if (empty)
    {
        body = Trinity::StringFormat(
            R"({{"error":null,"childProducts":[],{},"paginationToken":null,"products":[]}})",
            MakeMetaJson(NowUnixMillis()));
        TC_LOG_INFO("module.catalogshop", "POST /ProductCatalogService/v1/GetProductsByStoreId → 200 variant=empty client={}",
            session->GetClientInfo());
    }
    else
    {
        body = RefreshRetailResponse(RetailProducts, NowUnixMillis());
        TC_LOG_INFO("module.catalogshop", "POST /ProductCatalogService/v1/GetProductsByStoreId → 200 variant=retail-l80-replay client={}",
            session->GetClientInfo());
    }

    ReplyJson(context, std::move(body));
    return RequestHandlerResult::Handled;
}

CatalogShopHttpService::RequestHandlerResult CatalogShopHttpService::HandleVcBalance(
    std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context)
{
    rapidjson::Document req;
    ParseRequestBody(req, context.request.body());
    std::string code = "XVV";
    std::string title = "T2_WOW_US";
    if (req.IsObject())
    {
        if (req.HasMember("currencyCode") && req["currencyCode"].IsString())
            code = req["currencyCode"].GetString();
        if (req.HasMember("ledgerKey") && req["ledgerKey"].IsObject()
            && req["ledgerKey"].HasMember("titleSegment") && req["ledgerKey"]["titleSegment"].IsString())
            title = req["ledgerKey"]["titleSegment"].GetString();
    }

    uint32 accountId = _lastSsoAccountId.load(std::memory_order_acquire);
    if (!accountId)
        accountId = 1; // browse JSON only; checkout signal never uses this fallback

    std::string body = Trinity::StringFormat(
        R"({{"error":null,"softCapped":false,"balance":{{"ledgerId":{{"accountId":{},"ledgerKey":{{"gameServiceRegionId":null,"titleSegment":"{}"}},"currencyCode":"{}"}},"balance":"0","ecosystem":0,"ecosystemGroupComposition":{{"universalBalance":"0","ecosystemGroupBalances":[]}}}}}})",
        accountId, title, code);

    TC_LOG_INFO("module.catalogshop", "POST /VirtualCurrencyLedgerService/v1/GetBalance → 200 client={}",
        session->GetClientInfo());
    ReplyJson(context, std::move(body));
    return RequestHandlerResult::Handled;
}

CatalogShopHttpService::RequestHandlerResult CatalogShopHttpService::HandleQuoteDynamicBundle(
    std::shared_ptr<CatalogShopHttpSession> session, HttpRequestContext& context)
{
    rapidjson::Document req;
    ParseRequestBody(req, context.request.body());
    uint64 productId = 0;
    std::string currency = "USD";
    if (req.IsObject())
    {
        if (req.HasMember("productId") && req["productId"].IsUint64())
            productId = req["productId"].GetUint64();
        else if (req.HasMember("productId") && req["productId"].IsInt())
            productId = uint64(req["productId"].GetInt());
        if (req.HasMember("currencyCode") && req["currencyCode"].IsString())
            currency = req["currencyCode"].GetString();
    }

    std::string body = Trinity::StringFormat(
        R"({{"error":null,"result":{{"amount":"0","localizedAmount":"Free","currencyCode":"{}","discounts":[],"quoteToken":"local-free-quote","components":[{{"amount":"0","localizedAmount":"Free","productId":{}}}]}}}})",
        currency, productId);

    TC_LOG_INFO("module.catalogshop", "POST /PurchaseService/v1/QuoteDynamicBundle → 200 client={}",
        session->GetClientInfo());
    ReplyJson(context, std::move(body));
    return RequestHandlerResult::Handled;
}

std::string CatalogShopHttpService::ResolveFreeBuySignalDir() const
{
    std::string dir = sWorld->GetCatalogShopFreeBuySignalDir();
    if (dir.empty())
        dir = sConfigMgr->GetStringDefault("BattlePay.CatalogShopFreeBuySignalDir", "");
    if (dir.empty())
        dir = sConfigMgr->GetStringDefault("CatalogShop.FreeBuySignalDir", "temp/catalogshop-free-buy");
    return dir;
}

bool CatalogShopHttpService::WriteFreeBuySignal(uint32 accountId, uint32 productId) const
{
    if (!accountId || !productId || _freeBuySignalDir.empty())
        return false;

    std::error_code ec;
    std::filesystem::path dir(_freeBuySignalDir);
    std::filesystem::create_directories(dir, ec);
    std::filesystem::path path = dir / Trinity::StringFormat("{}.signal", accountId);
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out)
    {
        TC_LOG_ERROR("module.catalogshop", "Failed to write Free Buy signal {}", path.string());
        return false;
    }
    out << productId;
    TC_LOG_INFO("server.worldserver", "CatalogShop Free Buy signal wrote {} productId={}",
        path.string(), productId);
    return true;
}

uint32 CatalogShopHttpService::ResolveCheckoutAccountId(HttpRequestContext const& context) const
{
    std::string_view const target = Trinity::Net::Http::ToStdStringView(context.request.target());
    std::unordered_map<std::string, std::string> query;
    size_t const queryIndex = target.find('?');
    if (queryIndex != std::string_view::npos)
        query = ParseForm(target.substr(queryIndex + 1));
    auto const form = ParseForm(context.request.body());

    auto fromMap = [](std::unordered_map<std::string, std::string> const& fields) -> uint32
    {
        for (char const* key : { "token", "ssoToken", "sso_token", "sso" })
        {
            auto it = fields.find(key);
            if (it == fields.end() || it->second.empty())
                continue;
            if (uint32 id = ParseSsoAccountId(it->second))
                return id;
            if (uint32 id = FindXusAccountId(it->second))
                return id;
        }
        return 0;
    };

    if (uint32 id = fromMap(query))
        return id;
    if (uint32 id = fromMap(form))
        return id;

    if (uint32 id = FindXusAccountId(target))
        return id;
    if (uint32 id = FindXusAccountId(context.request.body()))
        return id;
    if (uint32 id = FindXusAccountId(HeaderValue(context, boost::beast::http::field::cookie)))
        return id;
    if (uint32 id = FindXusAccountId(HeaderValue(context, boost::beast::http::field::referer)))
        return id;
    if (uint32 id = FindXusAccountId(HeaderValue(context, boost::beast::http::field::authorization)))
        return id;

    return _lastSsoAccountId.load(std::memory_order_acquire);
}

uint32 CatalogShopHttpService::ResolveCheckoutProductId(std::string_view path, std::string_view target, std::string_view body)
{
    if (uint32 id = ParseProductIdFromPath(path))
        return id;

    size_t const queryIndex = target.find('?');
    if (queryIndex != std::string_view::npos)
        if (uint32 id = ParseProductIdFromMap(ParseForm(target.substr(queryIndex + 1))))
            return id;
    if (uint32 id = ParseProductIdFromMap(ParseForm(body)))
        return id;

    auto findSku = [](std::string_view haystack) -> uint32
    {
        if (haystack.find("1969052") != std::string_view::npos)
            return uint32(CATALOG_SHOP_L80_ENHANCED_PRODUCT_ID);
        if (haystack.find("1960794") != std::string_view::npos)
            return uint32(CATALOG_SHOP_L80_ENHANCED_CHILD_PRODUCT_ID);
        if (haystack.find("1977499") != std::string_view::npos)
            return uint32(CATALOG_SHOP_L80_STANDARD_PRODUCT_ID);
        return 0;
    };
    if (uint32 id = findSku(target))
        return id;
    return findSku(body);
}

std::string CatalogShopHttpService::HeaderValue(HttpRequestContext const& context, boost::beast::http::field field)
{
    auto it = context.request.find(field);
    if (it == context.request.end())
        return {};
    return std::string(Trinity::Net::Http::ToStdStringView(it->value()));
}

std::string CatalogShopHttpService::ExtractXusToken(std::string_view haystack)
{
    size_t pos = haystack.find("XUS-");
    if (pos == std::string_view::npos)
        return {};
    std::string_view token = haystack.substr(pos);
    size_t end = token.find_first_of("&; \t\"'");
    if (end != std::string_view::npos)
        token = token.substr(0, end);
    if (token.size() < 8)
        return {};
    return std::string(token);
}

uint32 CatalogShopHttpService::FindXusAccountId(std::string_view haystack)
{
    std::string token = ExtractXusToken(haystack);
    if (token.empty())
        return 0;
    return ParseSsoAccountId(token);
}

std::string CatalogShopHttpService::MakeCheckoutCompleteHtml(uint32 productId)
{
    return Trinity::StringFormat(
        R"(<!DOCTYPE html><html><head><meta charset="utf-8"><title>Free grant</title></head>)"
        R"(<body style="font-family:Segoe UI,sans-serif;background:#1a1a1e;color:#eee;display:flex;)"
        R"(align-items:center;justify-content:center;height:100vh;margin:0">)"
        R"(<div style="text-align:center"><h1>Free purchase complete</h1>)"
        R"(<p>Product {} → pending Level 80 Boost (local Free mode).</p></div></body></html>)",
        productId);
}

std::string CatalogShopHttpService::MakeCheckoutConfirmHtml(uint32 productId, std::string_view token, bool autoSubmit)
{
    return Trinity::StringFormat(
        R"(<!DOCTYPE html><html><head><meta charset="utf-8"><title>Local Free checkout</title></head>)"
        R"(<body style="font-family:Segoe UI,sans-serif;background:#1a1a1e;color:#eee;display:flex;)"
        R"(align-items:center;justify-content:center;height:100vh;margin:0">)"
        R"(<div style="text-align:center"><h1>Local Free checkout</h1>)"
        R"(<p>Confirm Free Buy for product {}.</p>)"
        R"(<form id="freebuy" method="POST" action="/shop/en-us/client-purchase/{}">)"
        R"(<input type="hidden" name="token" value="{}">)"
        R"(<button type="submit" style="font-size:1.1rem;padding:0.6rem 1.2rem">Confirm Free Buy</button>)"
        R"(</form>{}</div></body></html>)",
        productId, productId, token,
        autoSubmit ? R"(<script>document.getElementById("freebuy").submit();</script>)" : "");
}

uint32 CatalogShopHttpService::ParseSsoAccountId(std::string_view token)
{
    std::string_view body = token;
    size_t dash = body.find('-');
    if (dash != std::string_view::npos)
        body = body.substr(dash + 1);
    size_t nextDash = body.find('-');
    std::string_view hexpart = nextDash == std::string_view::npos ? body : body.substr(0, nextDash);
    if (hexpart.size() < 8)
        return 0;
    if (Optional<uint32> parsed = Trinity::StringTo<uint32>(hexpart.substr(0, 8), 16))
        return *parsed;
    return 0;
}

uint32 CatalogShopHttpService::ParseProductIdFromPath(std::string_view path)
{
    constexpr std::string_view marker = "/client-purchase/";
    size_t pos = path.find(marker);
    if (pos == std::string_view::npos)
        return 0;
    std::string_view tail = path.substr(pos + marker.size());
    size_t slash = tail.find('/');
    if (slash != std::string_view::npos)
        tail = tail.substr(0, slash);
    size_t q = tail.find('?');
    if (q != std::string_view::npos)
        tail = tail.substr(0, q);
    if (Optional<uint32> parsed = Trinity::StringTo<uint32>(tail))
        return *parsed;
    return 0;
}

uint32 CatalogShopHttpService::ParseProductIdFromMap(std::unordered_map<std::string, std::string> const& fields)
{
    for (char const* key : { "productId", "product_id", "sku", "product" })
    {
        auto it = fields.find(key);
        if (it == fields.end() || it->second.empty())
            continue;
        if (Optional<uint32> parsed = Trinity::StringTo<uint32>(it->second))
            return *parsed;
    }
    return 0;
}

std::string CatalogShopHttpService::HostWithoutPort(std::string_view hostHeader)
{
    std::string host(hostHeader);
    while (!host.empty() && (host.front() == ' ' || host.front() == '\t'))
        host.erase(host.begin());
    while (!host.empty() && (host.back() == ' ' || host.back() == '\t'))
        host.pop_back();
    size_t colon = host.find(':');
    if (colon != std::string::npos)
        host.resize(colon);
    for (char& c : host)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return host;
}

std::string CatalogShopHttpService::BuildLocalJwt(std::string_view scope) const
{
    int64 now = NowUnixSeconds();
    std::string header = R"({"alg":"none","typ":"JWT"})";
    std::string payload = Trinity::StringFormat(
        R"({{"iss":"local-catalogshop-stub","sub":"local-account","aud":"33dad602838b47bfa5ca03adaebac54c","iat":{},"exp":{},"scope":"{}"}})",
        now, now + 86400, scope);
    return Trinity::StringFormat("{}.{}.", Base64UrlEncode(header), Base64UrlEncode(payload));
}

std::string CatalogShopHttpService::Base64UrlEncode(std::string_view raw)
{
    std::vector<uint8> bytes(raw.begin(), raw.end());
    std::string enc = Trinity::Encoding::Base64::Encode(bytes);
    for (char& c : enc)
    {
        if (c == '+')
            c = '-';
        else if (c == '/')
            c = '_';
    }
    while (!enc.empty() && enc.back() == '=')
        enc.pop_back();
    return enc;
}

std::unordered_map<std::string, std::string> CatalogShopHttpService::ParseForm(std::string_view body)
{
    std::unordered_map<std::string, std::string> out;
    for (std::string_view part : Trinity::Tokenize(body, '&', false))
    {
        size_t eq = part.find('=');
        if (eq == std::string_view::npos)
            out[UrlDecode(part)] = {};
        else
            out[UrlDecode(part.substr(0, eq))] = UrlDecode(part.substr(eq + 1));
    }
    return out;
}

int64 CatalogShopHttpService::NowUnixSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

int64 CatalogShopHttpService::NowUnixMillis()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
}
