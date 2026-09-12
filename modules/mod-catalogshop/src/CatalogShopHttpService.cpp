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
#include "Base64.h"
#include "CatalogShopSslContext.h"
#include "Config.h"
#include "Log.h"
#include "StringConvert.h"
#include "Util.h"
#include "World.h"
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
constexpr char const* SHELF_PLACEMENT = "c0000000-0000-4000-8000-000000000003";
constexpr char const* CHECKOUT_HOST = "us.checkout.battle.net";

constexpr uint64 CATALOG_SHOP_L80_ENHANCED_PRODUCT_ID = 1969052;
constexpr uint64 CATALOG_SHOP_L80_ENHANCED_CHILD_PRODUCT_ID = 1960794;
constexpr uint64 CATALOG_SHOP_L80_STANDARD_PRODUCT_ID = 1977499;

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

std::string MakeFreePriceRow()
{
    return R"({"currencyCode":"USD","currencyTypeId":1,"currentPrice":"0.00","localizedCurrentPrice":"Free","localizedOriginalPrice":"Free","originalPrice":"0.00"})";
}

std::string MakeProductJson(uint64 productId, char const* name, bool withChild, std::string const& locale)
{
    std::string childIds = withChild
        ? Trinity::StringFormat("[{}]", CATALOG_SHOP_L80_ENHANCED_CHILD_PRODUCT_ID)
        : "[]";
    return Trinity::StringFormat(
        R"({{"productId":{},"name":"{}","productType":1,"serviceItemId":43,"childProductIds":{},"prices":[{}],"localization":{{"locale":"{}","name":"{}","description":""}}}})",
        productId, name, childIds, MakeFreePriceRow(), locale, name);
}

std::string MakePlacementJson(std::string const& placementId, std::string const& pageId,
    std::string const& pageName, std::string const& locale, std::string const& sectionsJson)
{
    return Trinity::StringFormat(
        R"({{"placementId":"{}","nextScheduledPageDisplayTimeMs":null,"page":{{"pageId":"{}","name":"{}","localization":{{"locale":"{}","name":"{}","description":""}},"attributes":[],"sections":{}}}}})",
        placementId, pageId, pageName, locale, pageName, sectionsJson);
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

    std::string bindIp = sConfigMgr->GetStringDefault("CatalogShop.BindIP", "127.0.0.1");
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
        _ioContext.reset();
        return false;
    }

    _ioThread = std::thread([ctx = _ioContext]()
    {
        ctx->run();
    });

    _listening.store(true, std::memory_order_release);
    TC_LOG_INFO("server.worldserver", "CatalogShop HTTPS listening on https://{}:{} (browse https://localhost; Free Buy signal dir '{}')",
        bindIp, port, _freeBuySignalDir);
    TC_LOG_INFO("module.catalogshop", "Stop any Python shop2 stub — this module owns browse and local checkout");
    return true;
}

void CatalogShopHttpService::Stop()
{
    if (!_listening.exchange(false, std::memory_order_acq_rel) && !_ioContext)
        return;

    StopNetwork();

    if (_ioContext)
        _ioContext->stop();

    if (_ioThread.joinable())
        _ioThread.join();

    _ioContext.reset();
    TC_LOG_INFO("module.catalogshop", "CatalogShop HTTPS listener stopped");
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

    std::string_view path = Trinity::Net::Http::ToStdStringView(context.request.target());
    size_t queryIndex = path.find('?');
    if (queryIndex != std::string_view::npos)
        path = path.substr(0, queryIndex);

    uint32 productId = ParseProductIdFromPath(path);
    bool const isClientPurchase = path.find("/client-purchase/") != std::string_view::npos;

    if (productId && isClientPurchase)
    {
        uint32 accountId = 0;
        std::string_view const target = Trinity::Net::Http::ToStdStringView(context.request.target());
        size_t const targetQuery = target.find('?');
        if (targetQuery != std::string_view::npos)
        {
            auto query = ParseForm(target.substr(targetQuery + 1));
            if (query.contains("token"))
                accountId = ParseSsoAccountId(query["token"]);
        }
        if (!accountId)
        {
            auto form = ParseForm(context.request.body());
            if (form.contains("token"))
                accountId = ParseSsoAccountId(form["token"]);
        }
        if (!accountId)
            accountId = _lastSsoAccountId.load(std::memory_order_acquire);
        bool wrote = false;
        if (accountId && IsFreeL80CommerceProduct(productId))
            wrote = WriteFreeBuySignal(accountId, productId);

        TC_LOG_INFO("module.catalogshop",
            "Checkout Free Buy host={} path={} productId={} account={} signal={}",
            host, path, productId, accountId, wrote ? "wrote" : "skip");

        ReplyHtml(context, Trinity::StringFormat(
            R"(<!DOCTYPE html><html><head><meta charset="utf-8"><title>Free grant</title></head>)"
            R"(<body style="font-family:Segoe UI,sans-serif;background:#1a1a1e;color:#eee;display:flex;)"
            R"(align-items:center;justify-content:center;height:100vh;margin:0">)"
            R"(<div style="text-align:center"><h1>Free purchase complete</h1>)"
            R"(<p>Product {} → pending Level 80 Boost (local Free mode).</p></div></body></html>)",
            productId));
        return true;
    }

    TC_LOG_INFO("module.catalogshop", "Checkout shell host={} path={}", host, path);
    ReplyHtml(context,
        R"(<!DOCTYPE html><html><head><meta charset="utf-8"><title>Local Free checkout</title></head>)"
        R"(<body style="font-family:Segoe UI,sans-serif;background:#1a1a1e;color:#eee;display:flex;)"
        R"(align-items:center;justify-content:center;height:100vh;margin:0">)"
        R"(<p>Local Free checkout. Stock CatalogShop Buy still needs a hosts pin for us.checkout.battle.net.</p></body></html>)");
    return true;
}

CatalogShopHttpService::RequestHandlerResult CatalogShopHttpService::HandleSso(
    std::shared_ptr<CatalogShopHttpSession> /*session*/, HttpRequestContext& context)
{
    auto form = ParseForm(context.request.body());
    std::string scope = form.contains("scope") ? form["scope"] : std::string();
    std::string ssoToken = form.contains("token") ? form["token"] : std::string();
    uint32 accountId = ParseSsoAccountId(ssoToken);
    if (accountId)
        _lastSsoAccountId.store(accountId, std::memory_order_release);
    if (!accountId)
        accountId = 1;

    std::string token = BuildLocalJwt(scope);
    int64 now = NowUnixSeconds();

    rapidjson::Document doc;
    doc.SetObject();
    auto& a = doc.GetAllocator();
    doc.AddMember("access_token", rapidjson::Value(token.c_str(), a), a);
    doc.AddMember("token_type", "bearer", a);
    doc.AddMember("expires_in", 86399, a);
    doc.AddMember("scope", rapidjson::Value(scope.c_str(), a), a);
    std::string sub = std::to_string(accountId);
    doc.AddMember("sub", rapidjson::Value(sub.c_str(), a), a);
    doc.AddMember("account_id", accountId, a);

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
    std::string iss = "https://localhost";
    doc.AddMember("iss", rapidjson::Value(iss.c_str(), a), a);
    doc.AddMember("iat", now, a);
    doc.AddMember("game_accounts", rapidjson::Value(rapidjson::kArrayType), a);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    TC_LOG_INFO("module.catalogshop", "POST /sso → 200 local JWT account={}", accountId);
    ReplyJson(context, buffer.GetString());
    return RequestHandlerResult::Handled;
}

CatalogShopHttpService::RequestHandlerResult CatalogShopHttpService::HandleDefaultCurrency(
    std::shared_ptr<CatalogShopHttpSession> /*session*/, HttpRequestContext& context)
{
    ReplyJson(context, R"({"currencyAlphaCode":"USD","error":null})");
    TC_LOG_INFO("module.catalogshop", "POST /PurchaseService/v1/GetAccountDefaultCurrency → 200");
    return RequestHandlerResult::Handled;
}

CatalogShopHttpService::RequestHandlerResult CatalogShopHttpService::HandleCurrentPages(
    std::shared_ptr<CatalogShopHttpSession> /*session*/, HttpRequestContext& context)
{
    rapidjson::Document req;
    req.Parse(context.request.body().c_str());

    std::vector<std::string> placementIds;
    if (req.IsObject() && req.HasMember("placementIds") && req["placementIds"].IsArray())
    {
        for (auto const& id : req["placementIds"].GetArray())
            if (id.IsString())
                placementIds.emplace_back(id.GetString());
    }
    if (placementIds.empty())
        placementIds.emplace_back(POP_PLACEMENT);

    std::string locale = "enUS";
    if (req.IsObject() && req.HasMember("locale") && req["locale"].IsString())
        locale = req["locale"].GetString();

    std::string vcPage = MakePlacementJson(VC_PLACEMENT, "a0000000-0000-4000-8000-000000000010",
        "Currency", locale, "[]");

    std::string popSections = Trinity::StringFormat(
        R"([{{"sectionId":"a0000000-0000-4000-8000-000000000002","localization":{{"locale":"{}","name":"Character Boosts","description":""}},"attributes":[{{"sectionAttributeKey":"placementID","value":"{}"}}],"cards":[{{"productId":{}}},{{"productId":{}}}]}}])",
        locale, SHELF_PLACEMENT, CATALOG_SHOP_L80_ENHANCED_PRODUCT_ID, CATALOG_SHOP_L80_STANDARD_PRODUCT_ID);
    std::string popPage = MakePlacementJson(POP_PLACEMENT, "a0000000-0000-4000-8000-000000000001",
        "Services", locale, popSections);

    std::string shelfSections = Trinity::StringFormat(
        R"([{{"sectionId":"a0000000-0000-4000-8000-000000000004","localization":{{"locale":"{}","name":"Level 80","description":""}},"attributes":[],"cards":[{{"productId":{}}},{{"productId":{}}}]}}])",
        locale, CATALOG_SHOP_L80_ENHANCED_PRODUCT_ID, CATALOG_SHOP_L80_STANDARD_PRODUCT_ID);
    std::string shelfPage = MakePlacementJson(SHELF_PLACEMENT, "a0000000-0000-4000-8000-000000000003",
        "Level 80 Boosts", locale, shelfSections);

    std::string placements;
    for (std::string const& id : placementIds)
    {
        std::string const* chosen = nullptr;
        if (id == VC_PLACEMENT)
            chosen = &vcPage;
        else if (id == POP_PLACEMENT)
            chosen = &popPage;
        else if (id == SHELF_PLACEMENT)
            chosen = &shelfPage;
        else
            continue;
        if (!placements.empty())
            placements.push_back(',');
        placements += *chosen;
    }
    if (placements.empty())
        placements = popPage;

    std::string body = Trinity::StringFormat(
        R"({{"error":null,{},"placements":[{}]}})",
        MakeMetaJson(NowUnixMillis()), placements);

    TC_LOG_INFO("module.catalogshop", "POST /StorefrontService/v1/GetCurrentPages → 200 placements={}",
        placementIds.size());
    ReplyJson(context, std::move(body));
    return RequestHandlerResult::Handled;
}

CatalogShopHttpService::RequestHandlerResult CatalogShopHttpService::HandleProductsByStoreId(
    std::shared_ptr<CatalogShopHttpSession> /*session*/, HttpRequestContext& context)
{
    rapidjson::Document req;
    req.Parse(context.request.body().c_str());

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
        TC_LOG_INFO("module.catalogshop", "POST /ProductCatalogService/v1/GetProductsByStoreId → 200 variant=empty");
    }
    else
    {
        std::string locale = "enUS";
        if (req.IsObject() && req.HasMember("locale") && req["locale"].IsString())
            locale = req["locale"].GetString();

        std::string products = Trinity::StringFormat("{},{}",
            MakeProductJson(CATALOG_SHOP_L80_ENHANCED_PRODUCT_ID, "Enhanced Level 80 Character Boost", true, locale),
            MakeProductJson(CATALOG_SHOP_L80_STANDARD_PRODUCT_ID, "Level 80 Character Boost", false, locale));
        std::string child = MakeProductJson(CATALOG_SHOP_L80_ENHANCED_CHILD_PRODUCT_ID, "Level 80 Character Boost", false, locale);
        body = Trinity::StringFormat(
            R"({{"error":null,"childProducts":[{}],{},"paginationToken":null,"products":[{}]}})",
            child, MakeMetaJson(NowUnixMillis()), products);
        TC_LOG_INFO("module.catalogshop", "POST /ProductCatalogService/v1/GetProductsByStoreId → 200 variant=l80-shelf");
    }

    ReplyJson(context, std::move(body));
    return RequestHandlerResult::Handled;
}

CatalogShopHttpService::RequestHandlerResult CatalogShopHttpService::HandleVcBalance(
    std::shared_ptr<CatalogShopHttpSession> /*session*/, HttpRequestContext& context)
{
    rapidjson::Document req;
    req.Parse(context.request.body().c_str());
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
        accountId = 1;

    std::string body = Trinity::StringFormat(
        R"({{"error":null,"softCapped":false,"balance":{{"ledgerId":{{"accountId":{},"ledgerKey":{{"gameServiceRegionId":null,"titleSegment":"{}"}},"currencyCode":"{}"}},"balance":"0","ecosystem":0,"ecosystemGroupComposition":{{"universalBalance":"0","ecosystemGroupBalances":[]}}}}}})",
        accountId, title, code);

    TC_LOG_INFO("module.catalogshop", "POST /VirtualCurrencyLedgerService/v1/GetBalance → 200");
    ReplyJson(context, std::move(body));
    return RequestHandlerResult::Handled;
}

CatalogShopHttpService::RequestHandlerResult CatalogShopHttpService::HandleQuoteDynamicBundle(
    std::shared_ptr<CatalogShopHttpSession> /*session*/, HttpRequestContext& context)
{
    rapidjson::Document req;
    req.Parse(context.request.body().c_str());
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

    TC_LOG_INFO("module.catalogshop", "POST /PurchaseService/v1/QuoteDynamicBundle → 200");
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
    return true;
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
        R"({{"iss":"local-catalogshop","sub":"local-account","aud":"33dad602838b47bfa5ca03adaebac54c","iat":{},"exp":{},"scope":"{}"}})",
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
