#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/Peer.h"
#include "util/Timer.h"
#include <queue>

namespace medida
{
class Meter;
}

namespace stellar
{
// Peer that communicates via a TCP socket.
class TCPPeer : public Peer
{
    std::string mIP;
    std::shared_ptr<asio::ip::tcp::socket> mSocket;
    std::vector<uint8_t> mIncomingHeader;
    std::vector<uint8_t> mIncomingBody;

    std::queue<std::shared_ptr<xdr::msg_ptr>> mWriteQueue;

    void recvMessage();
    void sendMessage(xdr::msg_ptr&& xdrBytes) override;

    void messageSender();

    int getIncomingMsgLength();
    virtual void connected() override;
    void startRead();

    void writeHandler(asio::error_code const& error,
                      std::size_t bytes_transferred) override;
    void readHeaderHandler(asio::error_code const& error,
                           std::size_t bytes_transferred) override;
    void readBodyHandler(asio::error_code const& error,
                         std::size_t bytes_transferred) override;

  public:
    typedef std::shared_ptr<TCPPeer> pointer;

    TCPPeer(Application& app, Peer::PeerRole role,
            std::shared_ptr<asio::ip::tcp::socket> socket); // hollow
                                                            // constuctor; use
                                                            // `initiate` or
                                                            // `accept` instead

    static pointer initiate(Application& app, std::string const& ip,
                            unsigned short port);
    static pointer accept(Application& app,
                          std::shared_ptr<asio::ip::tcp::socket> socket);

    virtual ~TCPPeer();

    virtual void drop() override;
    virtual std::string getIP() override;
};
}
