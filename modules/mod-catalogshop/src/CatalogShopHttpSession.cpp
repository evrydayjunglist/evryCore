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
class CatalogShopHandshakeInitializer final : public Trinity::Net::SocketConnectionInitializer
{
public:
    CatalogShopHandshakeInitializer(SocketImpl* socket, std::string clientInfo)
        : _socket(socket), _clientInfo(std::move(clientInfo))
    {
    }

    void Start() override
    {
        _socket->underlying_stream().async_handshake(boost::asio::ssl::stream_base::server,
            [socketRef = _socket->weak_from_this(), self = this->shared_from_this(),
                clientInfo = _clientInfo](boost::system::error_code const& error)
            {
                std::shared_ptr<SocketImpl> socket = static_pointer_cast<SocketImpl>(socketRef.lock());
                if (!socket)
                    return;

                if (error)
                {
                    TC_LOG_ERROR("server.worldserver", "CatalogShop HTTPS {} TLS handshake failed: {}",
                        clientInfo, error.message());
                    socket->CloseSocket();
                    return;
                }

                TC_LOG_INFO("server.worldserver", "CatalogShop HTTPS {} TLS handshake complete", clientInfo);
                self->InvokeNext();
            });
    }

private:
    SocketImpl* _socket;
    std::string _clientInfo;
};

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
            std::make_shared<CatalogShopHandshakeInitializer<BaseSocket>>(this, _owner.GetClientInfo()),
            std::make_shared<Trinity::Net::Http::HttpConnectionInitializer<BaseSocket>>(this),
            std::make_shared<Trinity::Net::ReadConnectionInitializer<BaseSocket>>(this),
        } };
        Trinity::Net::SocketConnectionInitializer::SetupChain(initializers)->Start();
    }

    Trinity::Net::Http::RequestHandlerResult RequestHandler(Trinity::Net::Http::RequestContext& context) override
    {
        Trinity::Net::Http::RequestHandlerResult result;
        if (sCatalogShop.TryHandleCheckout(context))
            result = Trinity::Net::Http::RequestHandlerResult::Handled;
        else
        {
            result = sCatalogShop.HandleRequest(_owner.shared_from_this(), context);
            if (context.response.result() == boost::beast::http::status::not_found)
            {
                context.response.result(boost::beast::http::status::not_implemented);
                context.response.set(boost::beast::http::field::content_type, "application/json");
                context.response.set(boost::beast::http::field::cache_control, "no-store");
                context.response.keep_alive(false);
                context.response.body() = R"({"error":"catalogshop local module - path logged, not implemented"})";
            }
        }

        TC_LOG_INFO("server.worldserver", "CatalogShop HTTPS {} {} {} -> {}",
            _owner.GetClientInfo(),
            Trinity::Net::Http::ToStdStringView(context.request.method_string()),
            Trinity::Net::Http::ToStdStringView(context.request.target()),
            context.response.result_int());
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
    TC_LOG_INFO("module.catalogshop", "{} Accepted connection", GetClientInfo());
    TC_LOG_INFO("server.worldserver", "CatalogShop HTTPS {} Accepted connection", GetClientInfo());
    _socket->Start();
}

bool CatalogShopHttpSession::Update()
{
    return _socket->Update();
}
}
