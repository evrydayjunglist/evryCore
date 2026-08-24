/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "PlayerbotBridge.h"
#include "AsyncAcceptor.h"
#include "IoContext.h"
#include "Log.h"
#include "Playerbots.h"
#include "Socket.h"
#include <boost/asio/buffer.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace
{
constexpr uint32 MAX_PLAYERBOT_BRIDGE_PAYLOAD = 64 * 1024;
constexpr std::size_t MAX_PLAYERBOT_BRIDGE_REQUESTS = 32;

class PlayerbotBridgeSession : public std::enable_shared_from_this<PlayerbotBridgeSession>
{
public:
    using RequestHandler = std::function<void(uint64, std::weak_ptr<PlayerbotBridgeSession>, std::string)>;
    using CloseHandler = std::function<void(uint64)>;

    PlayerbotBridgeSession(uint64 connectionId, Trinity::Net::IoContextTcpSocket&& socket,
        RequestHandler requestHandler, CloseHandler closeHandler) :
        _connectionId(connectionId), _socket(std::move(socket)), _requestHandler(std::move(requestHandler)),
        _closeHandler(std::move(closeHandler))
    {
    }

    void Start()
    {
        ReadHeader();
    }

    void Stop()
    {
        std::shared_ptr<PlayerbotBridgeSession> self = shared_from_this();
        boost::asio::post(_socket.get_executor(), [self]
        {
            self->Close();
        });
    }

    void Send(std::string payload)
    {
        if (payload.empty() || payload.size() > MAX_PLAYERBOT_BRIDGE_PAYLOAD)
            return;

        std::shared_ptr<PlayerbotBridgeSession> self = shared_from_this();
        boost::asio::post(_socket.get_executor(), [self, payload = std::move(payload)]() mutable
        {
            if (self->_closed)
                return;

            uint32 const size = uint32(payload.size());
            std::vector<uint8> frame(4 + size);
            frame[0] = uint8(size >> 24);
            frame[1] = uint8(size >> 16);
            frame[2] = uint8(size >> 8);
            frame[3] = uint8(size);
            std::copy(payload.begin(), payload.end(), frame.begin() + 4);

            bool const writeInProgress = !self->_outgoing.empty();
            self->_outgoing.push_back(std::move(frame));
            if (!writeInProgress)
                self->WriteNext();
        });
    }

private:
    void ReadHeader()
    {
        std::shared_ptr<PlayerbotBridgeSession> self = shared_from_this();
        boost::asio::async_read(_socket, boost::asio::buffer(_header),
            [self](boost::system::error_code const& error, std::size_t /*bytesTransferred*/)
        {
            if (error)
            {
                self->Close(error);
                return;
            }

            uint32 const size = (uint32(self->_header[0]) << 24) |
                (uint32(self->_header[1]) << 16) |
                (uint32(self->_header[2]) << 8) |
                uint32(self->_header[3]);
            if (!size || size > MAX_PLAYERBOT_BRIDGE_PAYLOAD)
            {
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: coordinator sent an invalid frame size {}.", size);
                self->Close();
                return;
            }

            self->_incoming.resize(size);
            self->ReadPayload();
        });
    }

    void ReadPayload()
    {
        std::shared_ptr<PlayerbotBridgeSession> self = shared_from_this();
        boost::asio::async_read(_socket, boost::asio::buffer(_incoming),
            [self](boost::system::error_code const& error, std::size_t /*bytesTransferred*/)
        {
            if (error)
            {
                self->Close(error);
                return;
            }

            std::string payload(self->_incoming.begin(), self->_incoming.end());
            self->_requestHandler(self->_connectionId, self, std::move(payload));
            self->ReadHeader();
        });
    }

    void WriteNext()
    {
        std::shared_ptr<PlayerbotBridgeSession> self = shared_from_this();
        boost::asio::async_write(_socket, boost::asio::buffer(_outgoing.front()),
            [self](boost::system::error_code const& error, std::size_t /*bytesTransferred*/)
        {
            if (error)
            {
                self->Close(error);
                return;
            }

            self->_outgoing.pop_front();
            if (!self->_outgoing.empty())
                self->WriteNext();
        });
    }

    void Close(boost::system::error_code const& error = {})
    {
        if (_closed)
            return;

        _closed = true;
        boost::system::error_code ignored;
        _socket.shutdown(boost::asio::socket_base::shutdown_both, ignored);
        _socket.close(ignored);

        if (error && error != boost::asio::error::operation_aborted && error != boost::asio::error::eof)
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: coordinator connection {} closed: {}.", _connectionId, error.message());

        _closeHandler(_connectionId);
    }

    uint64 _connectionId;
    Trinity::Net::IoContextTcpSocket _socket;
    RequestHandler _requestHandler;
    CloseHandler _closeHandler;
    std::array<uint8, 4> _header = {};
    std::vector<uint8> _incoming;
    std::deque<std::vector<uint8>> _outgoing;
    bool _closed = false;
};
}

class PlayerbotBridge::Impl
{
public:
    bool Start(uint16 port)
    {
        if (_running.exchange(true))
            return true;

        _ioContext.restart();
        _acceptor = std::make_unique<Trinity::Net::AsyncAcceptor>(_ioContext, "127.0.0.1", port);
        if (!_acceptor->Bind())
        {
            _acceptor.reset();
            _running = false;
            return false;
        }

        _acceptor->AsyncAccept(
            [this] { return &_ioContext; },
            [this](Trinity::Net::IoContextTcpSocket&& socket) { Accept(std::move(socket)); });
        _networkThread = std::thread([this]
        {
            _ioContext.run();
        });
        return true;
    }

    void Stop()
    {
        if (!_running.exchange(false))
            return;

        _ioContext.stop();
        if (_networkThread.joinable())
            _networkThread.join();

        _session.reset();
        _acceptor.reset();

        std::lock_guard lock(_requestsMutex);
        _requests.clear();
        _disconnectedConnections.clear();
    }

    std::vector<PlayerbotBridgeRequest> TakeRequests()
    {
        std::vector<PlayerbotBridgeRequest> requests;
        std::lock_guard lock(_requestsMutex);
        requests.reserve(_requests.size());
        while (!_requests.empty())
        {
            requests.push_back(std::move(_requests.front()));
            _requests.pop_front();
        }
        return requests;
    }

    std::vector<uint64> TakeDisconnectedConnections()
    {
        std::vector<uint64> connections;
        std::lock_guard lock(_requestsMutex);
        connections.reserve(_disconnectedConnections.size());
        while (!_disconnectedConnections.empty())
        {
            connections.push_back(_disconnectedConnections.front());
            _disconnectedConnections.pop_front();
        }
        return connections;
    }

private:
    void Accept(Trinity::Net::IoContextTcpSocket&& socket)
    {
        uint64 const connectionId = ++_nextConnectionId;
        if (_session)
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: replacing coordinator connection {} with {}.",
                _activeConnectionId, connectionId);
            _session->Stop();
        }

        _activeConnectionId = connectionId;
        _session = std::make_shared<PlayerbotBridgeSession>(connectionId, std::move(socket),
            [this](uint64 id, std::weak_ptr<PlayerbotBridgeSession> session, std::string payload)
            {
                QueueRequest(id, std::move(session), std::move(payload));
            },
            [this](uint64 id)
            {
                if (_activeConnectionId == id)
                {
                    _session.reset();
                    _activeConnectionId = 0;
                }
                QueueDisconnect(id);
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: coordinator connection {} disconnected.", id);
            });
        _session->Start();
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: coordinator connection {} accepted on loopback.", connectionId);
    }

    void QueueRequest(uint64 connectionId, std::weak_ptr<PlayerbotBridgeSession> session, std::string payload)
    {
        std::lock_guard lock(_requestsMutex);
        if (_requests.size() >= MAX_PLAYERBOT_BRIDGE_REQUESTS)
        {
            if (std::shared_ptr<PlayerbotBridgeSession> active = session.lock())
            {
                active->Send("{\"version\":" + std::to_string(PLAYERBOTS_BRIDGE_PROTOCOL_VERSION) +
                    R"(,"type":"error","requestId":"","payload":{"code":"serverBusy","message":"The world thread request queue is full."}})");
            }
            return;
        }

        PlayerbotBridgeRequest request;
        request.ConnectionId = connectionId;
        request.Payload = std::move(payload);
        request.Reply = [session = std::move(session)](std::string response)
        {
            if (std::shared_ptr<PlayerbotBridgeSession> active = session.lock())
                active->Send(std::move(response));
        };
        _requests.push_back(std::move(request));
    }

    void QueueDisconnect(uint64 connectionId)
    {
        std::lock_guard lock(_requestsMutex);
        _disconnectedConnections.push_back(connectionId);
    }

    Trinity::Asio::IoContext _ioContext{ 1 };
    std::unique_ptr<Trinity::Net::AsyncAcceptor> _acceptor;
    std::shared_ptr<PlayerbotBridgeSession> _session;
    std::thread _networkThread;
    std::mutex _requestsMutex;
    std::deque<PlayerbotBridgeRequest> _requests;
    std::deque<uint64> _disconnectedConnections;
    std::atomic<bool> _running = false;
    uint64 _nextConnectionId = 0;
    uint64 _activeConnectionId = 0;
};

PlayerbotBridge::PlayerbotBridge() : _impl(std::make_unique<Impl>())
{
}

PlayerbotBridge::~PlayerbotBridge()
{
    Stop();
}

bool PlayerbotBridge::Start(uint16 port)
{
    return _impl->Start(port);
}

void PlayerbotBridge::Stop()
{
    _impl->Stop();
}

std::vector<PlayerbotBridgeRequest> PlayerbotBridge::TakeRequests()
{
    return _impl->TakeRequests();
}

std::vector<uint64> PlayerbotBridge::TakeDisconnectedConnections()
{
    return _impl->TakeDisconnectedConnections();
}
