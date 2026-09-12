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

#include "CatalogShopHttpSession.h"
#include "CatalogShopHttpService.h"
#include "CatalogShopSslContext.h"
#include "HttpSslSocket.h"
#include "Log.h"
#include "Socket.h"
#include "SslStream.h"
#include <array>

namespace
{
template<typename SocketImpl>
class CatalogShopHttpSocketImpl final : public SocketImpl
{
public:
    using BaseSocket = SocketImpl;

    explicit CatalogShopHttpSocketImpl(Trinity::Net::IoContextTcpSocket&& socket, ModCatalogShop::CatalogShopHttpSession& owner)
        : BaseSocket(std::move(socket), ModCatalogShop::SslContext::instance()), _owner(owner)
    {
    }

    CatalogShopHttpSocketImpl(CatalogShopHttpSocketImpl const&) = delete;
    CatalogShopHttpSocketImpl(CatalogShopHttpSocketImpl&&) = delete;
    CatalogShopHttpSocketImpl& operator=(CatalogShopHttpSocketImpl const&) = delete;
    CatalogShopHttpSocketImpl& operator=(CatalogShopHttpSocketImpl&&) = delete;

    void Start() override
    {
        std::array<std::shared_ptr<Trinity::Net::SocketConnectionInitializer>, 3> initializers = { {
            std::make_shared<Trinity::Net::SslHandshakeConnectionInitializer<BaseSocket>>(this),
            std::make_shared<Trinity::Net::Http::HttpConnectionInitializer<BaseSocket>>(this),
            std::make_shared<Trinity::Net::ReadConnectionInitializer<BaseSocket>>(this),
        } };
        Trinity::Net::SocketConnectionInitializer::SetupChain(initializers)->Start();
    }

    Trinity::Net::Http::RequestHandlerResult RequestHandler(Trinity::Net::Http::RequestContext& context) override
    {
        if (sCatalogShop.TryHandleCheckout(context))
            return Trinity::Net::Http::RequestHandlerResult::Handled;

        Trinity::Net::Http::RequestHandlerResult result = sCatalogShop.HandleRequest(_owner.shared_from_this(), context);
        if (context.response.result() == boost::beast::http::status::not_found)
        {
            context.response.result(boost::beast::http::status::not_implemented);
            context.response.set(boost::beast::http::field::content_type, "application/json");
            context.response.set(boost::beast::http::field::cache_control, "no-store");
            context.response.keep_alive(false);
            context.response.body() = R"({"error":"catalogshop local module - path logged, not implemented"})";
            TC_LOG_INFO("module.catalogshop", "{} {} {} → 501 (unhandled)",
                _owner.GetClientInfo(),
                Trinity::Net::Http::ToStdStringView(context.request.method_string()),
                Trinity::Net::Http::ToStdStringView(context.request.target()));
        }
        return result;
    }

protected:
    // HttpService::CreateNewSessionState stores a real UUID. A bare make_shared leaves Id
    // nil, then SocketRemoved → KillInactiveSessions can erase(end()).
    std::shared_ptr<Trinity::Net::Http::SessionState> ObtainSessionState(Trinity::Net::Http::RequestContext& /*context*/) const override
    {
        return sCatalogShop.CreateNewSessionState(this->GetRemoteIpAddress());
    }

    ModCatalogShop::CatalogShopHttpSession& _owner;
};
}

namespace ModCatalogShop
{
CatalogShopHttpSession::CatalogShopHttpSession(Trinity::Net::IoContextTcpSocket&& socket)
    : _socket(std::make_shared<CatalogShopHttpSocketImpl<Trinity::Net::Http::SslSocket>>(std::move(socket), *this))
{
}

CatalogShopHttpSession::~CatalogShopHttpSession() = default;

void CatalogShopHttpSession::Start()
{
    TC_LOG_TRACE("module.catalogshop", "{} Accepted connection", GetClientInfo());
    _socket->Start();
}

bool CatalogShopHttpSession::Update()
{
    return _socket->Update();
}
}
