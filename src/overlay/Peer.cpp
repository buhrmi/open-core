// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "Peer.h"

#include "crypto/Hex.h"
#include "crypto/SHA.h"
#include "crypto/Random.h"
#include "database/Database.h"
#include "overlay/StellarXDR.h"
#include "herder/Herder.h"
#include "herder/TxSetFrame.h"
#include "main/Application.h"
#include "main/Config.h"
#include "overlay/OverlayManager.h"
#include "overlay/PeerRecord.h"
#include "util/Logging.h"

#include "medida/metrics_registry.h"
#include "medida/timer.h"

#include "xdrpp/marshal.h"

#include <soci.h>
#include <time.h>

// LATER: need to add some way of docking peers that are misbehaving by sending
// you bad data

namespace stellar
{

using namespace std;
using namespace soci;

Peer::Peer(Application& app, PeerRole role)
    : mApp(app)
    , mRole(role)
    , mState(role == WE_CALLED_REMOTE ? CONNECTING : CONNECTED)
    , mRemoteOverlayVersion(0)
    , mRemoteListeningPort(0)
    , mRecvErrorTimer(app.getMetrics().NewTimer({"overlay", "recv", "error"}))
    , mRecvHelloTimer(app.getMetrics().NewTimer({"overlay", "recv", "hello"}))
    , mRecvAuthTimer(app.getMetrics().NewTimer({"overlay", "recv", "auth"}))
    , mRecvDontHaveTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "dont-have"}))
    , mRecvGetPeersTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "get-peers"}))
    , mRecvPeersTimer(app.getMetrics().NewTimer({"overlay", "recv", "peers"}))
    , mRecvGetTxSetTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "get-txset"}))
    , mRecvTxSetTimer(app.getMetrics().NewTimer({"overlay", "recv", "txset"}))
    , mRecvTransactionTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "transaction"}))
    , mRecvGetSCPQuorumSetTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "get-scp-qset"}))
    , mRecvSCPQuorumSetTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "scp-qset"}))
    , mRecvSCPMessageTimer(
          app.getMetrics().NewTimer({"overlay", "recv", "scp-message"}))
{
}

void
Peer::sendHello()
{
    CLOG(DEBUG, "Overlay") << "Peer::sendHello to " << toString();

    auto bytes = randomBytes(mSentNonce.size());
    std::copy(bytes.begin(), bytes.end(), mSentNonce.begin());

    StellarMessage msg;
    msg.type(HELLO);
    msg.hello().ledgerVersion = mApp.getConfig().LEDGER_PROTOCOL_VERSION;
    msg.hello().overlayVersion = mApp.getConfig().OVERLAY_PROTOCOL_VERSION;
    msg.hello().versionStr = mApp.getConfig().VERSION_STR;
    msg.hello().networkID = mApp.getNetworkID();
    msg.hello().listeningPort = mApp.getConfig().PEER_PORT;
    msg.hello().peerID = mApp.getConfig().NODE_SEED.getPublicKey();
    msg.hello().nonce = mSentNonce;
    sendMessage(msg);
}

void
Peer::sendAuth()
{
    StellarMessage msg;
    msg.type(AUTH);

    // We do not want to sign things wholly under the control of the peer.
    std::vector<uint8_t> bytes;
    bytes.insert(bytes.end(), mSentNonce.begin(), mSentNonce.end());
    bytes.insert(bytes.end(), mReceivedNonce.begin(), mReceivedNonce.end());
    msg.auth().signature = mApp.getConfig().NODE_SEED.sign(bytes);
    sendMessage(msg);
}

std::string
Peer::toString()
{
    std::stringstream s;
    s << getIP() << ":" << mRemoteListeningPort;
    return s.str();
}

void
Peer::connectHandler(asio::error_code const& error)
{
    if (error)
    {
        CLOG(WARNING, "Overlay")
            << " connectHandler error: " << error.message();
        drop();
    }
    else
    {
        CLOG(DEBUG, "Overlay") << "connected @" << toString();
        connected();
        mState = CONNECTED;
        sendHello();
    }
}

void
Peer::sendDontHave(MessageType type, uint256 const& itemID)
{
    StellarMessage msg;
    msg.type(DONT_HAVE);
    msg.dontHave().reqHash = itemID;
    msg.dontHave().type = type;

    sendMessage(msg);
}

void
Peer::sendSCPQuorumSet(SCPQuorumSetPtr qSet)
{
    StellarMessage msg;
    msg.type(SCP_QUORUMSET);
    msg.qSet() = *qSet;

    sendMessage(msg);
}
void
Peer::sendGetTxSet(uint256 const& setID)
{
    StellarMessage newMsg;
    newMsg.type(GET_TX_SET);
    newMsg.txSetHash() = setID;

    sendMessage(newMsg);
}
void
Peer::sendGetQuorumSet(uint256 const& setID)
{
    CLOG(TRACE, "Overlay") << "Get quorum set: " << hexAbbrev(setID);

    StellarMessage newMsg;
    newMsg.type(GET_SCP_QUORUMSET);
    newMsg.qSetHash() = setID;

    sendMessage(newMsg);
}

void
Peer::sendPeers()
{
    // send top 50 peers we know about
    vector<PeerRecord> peerList;
    PeerRecord::loadPeerRecords(mApp.getDatabase(), 50, mApp.getClock().now(),
                                peerList);
    StellarMessage newMsg;
    newMsg.type(PEERS);
    newMsg.peers().reserve(peerList.size());
    for (auto const& pr : peerList)
    {
        if (pr.isPrivateAddress())
        {
            continue;
        }
        PeerAddress pa;
        pr.toXdr(pa);
        newMsg.peers().push_back(pa);
    }
    sendMessage(newMsg);
}

void
Peer::sendMessage(StellarMessage const& msg)
{
    CLOG(TRACE, "Overlay") << "("
                           << PubKeyUtils::toShortString(
                               mApp.getConfig().NODE_SEED.getPublicKey())
                           << ")send: " << msg.type()
                           << " to : " << PubKeyUtils::toShortString(mPeerID);
    xdr::msg_ptr xdrBytes(xdr::xdr_to_msg(msg));
    this->sendMessage(std::move(xdrBytes));
}

void
Peer::recvMessage(xdr::msg_ptr const& msg)
{
    CLOG(TRACE, "Overlay") << "received xdr::msg_ptr";
    StellarMessage sm;
    try
    {
        xdr::xdr_from_msg(msg, sm);
    }
    catch (xdr::xdr_runtime_error& e)
    {
        CLOG(TRACE, "Overlay") << "received corrupt xdr::msg_ptr " << e.what();
        drop();
        return;
    }
    recvMessage(sm);
}

bool
Peer::isConnected() const
{
    return mState != CONNECTING && mState != CLOSING;
}

bool
Peer::isAuthenticated() const
{
    return mState == GOT_AUTH;
}

bool
Peer::shouldAbort() const
{
    return (mState == CLOSING) || mApp.getOverlayManager().isShuttingDown();
}

void
Peer::recvMessage(StellarMessage const& stellarMsg)
{
    CLOG(TRACE, "Overlay") << "("
                           << PubKeyUtils::toShortString(
                               mApp.getConfig().NODE_SEED.getPublicKey())
                           << ") recv: " << stellarMsg.type()
                           << " from:" << PubKeyUtils::toShortString(mPeerID);

    if (mState < GOT_AUTH &&
        ((stellarMsg.type() != HELLO) &&
         (stellarMsg.type() != AUTH) &&
         (stellarMsg.type() != PEERS)))
    {
        CLOG(WARNING, "Overlay") << "recv: " << stellarMsg.type()
                                 << " before completed handshake";
        drop();
        return;
    }

    switch (stellarMsg.type())
    {
    case ERROR_MSG:
    {
        auto t = mRecvErrorTimer.TimeScope();
        recvError(stellarMsg);
    }
    break;

    case HELLO:
    {
        auto t = mRecvHelloTimer.TimeScope();
        this->recvHello(stellarMsg);
    }
    break;

    case AUTH:
    {
        auto t = mRecvAuthTimer.TimeScope();
        this->recvAuth(stellarMsg);
    }
    break;

    case DONT_HAVE:
    {
        auto t = mRecvDontHaveTimer.TimeScope();
        recvDontHave(stellarMsg);
    }
    break;

    case GET_PEERS:
    {
        auto t = mRecvGetPeersTimer.TimeScope();
        recvGetPeers(stellarMsg);
    }
    break;

    case PEERS:
    {
        auto t = mRecvPeersTimer.TimeScope();
        recvPeers(stellarMsg);
    }
    break;

    case GET_TX_SET:
    {
        auto t = mRecvGetTxSetTimer.TimeScope();
        recvGetTxSet(stellarMsg);
    }
    break;

    case TX_SET:
    {
        auto t = mRecvTxSetTimer.TimeScope();
        recvTxSet(stellarMsg);
    }
    break;

    case TRANSACTION:
    {
        auto t = mRecvTransactionTimer.TimeScope();
        recvTransaction(stellarMsg);
    }
    break;

    case GET_SCP_QUORUMSET:
    {
        auto t = mRecvGetSCPQuorumSetTimer.TimeScope();
        recvGetSCPQuorumSet(stellarMsg);
    }
    break;

    case SCP_QUORUMSET:
    {
        auto t = mRecvSCPQuorumSetTimer.TimeScope();
        recvSCPQuorumSet(stellarMsg);
    }
    break;

    case SCP_MESSAGE:
    {
        auto t = mRecvSCPMessageTimer.TimeScope();
        recvSCPMessage(stellarMsg);
    }
    break;
    }
}

void
Peer::recvDontHave(StellarMessage const& msg)
{
    mApp.getHerder().peerDoesntHave(msg.dontHave().type, msg.dontHave().reqHash,
                                    shared_from_this());
}

void
Peer::recvGetTxSet(StellarMessage const& msg)
{
    auto self = shared_from_this();
    if (auto txSet = mApp.getHerder().getTxSet(msg.txSetHash()))
    {
        StellarMessage newMsg;
        newMsg.type(TX_SET);
        txSet->toXDR(newMsg.txSet());

        self->sendMessage(newMsg);
    }
    else
    {
        sendDontHave(TX_SET, msg.txSetHash());
    }
}

void
Peer::recvTxSet(StellarMessage const& msg)
{
    TxSetFrame frame(mApp.getNetworkID(), msg.txSet());
    mApp.getHerder().recvTxSet(frame.getContentsHash(), frame);
}

void
Peer::recvTransaction(StellarMessage const& msg)
{
    TransactionFramePtr transaction = TransactionFrame::makeTransactionFromWire(
        mApp.getNetworkID(), msg.transaction());
    if (transaction)
    {
        // add it to our current set
        // and make sure it is valid
        if (mApp.getHerder().recvTransaction(transaction) ==
            Herder::TX_STATUS_PENDING)
        {
            mApp.getOverlayManager().recvFloodedMsg(msg, shared_from_this());
            mApp.getOverlayManager().broadcastMessage(msg);
        }
    }
}

void
Peer::recvGetSCPQuorumSet(StellarMessage const& msg)
{
    SCPQuorumSetPtr qset = mApp.getHerder().getQSet(msg.qSetHash());

    if (qset)
    {
        sendSCPQuorumSet(qset);
    }
    else
    {
        CLOG(TRACE, "Overlay")
            << "No quorum set: " << hexAbbrev(msg.qSetHash());
        sendDontHave(SCP_QUORUMSET, msg.qSetHash());
        // do we want to ask other people for it?
    }
}
void
Peer::recvSCPQuorumSet(StellarMessage const& msg)
{
    Hash hash = sha256(xdr::xdr_to_opaque(msg.qSet()));
    mApp.getHerder().recvSCPQuorumSet(hash, msg.qSet());
}

void
Peer::recvSCPMessage(StellarMessage const& msg)
{
    SCPEnvelope envelope = msg.envelope();
    CLOG(TRACE, "Overlay") << "recvSCPMessage node: "
                           << PubKeyUtils::toShortString(
                                  msg.envelope().statement.nodeID);

    mApp.getOverlayManager().recvFloodedMsg(msg, shared_from_this());

    mApp.getHerder().recvSCPEnvelope(envelope);
}

void
Peer::recvError(StellarMessage const& msg)
{
}

void
Peer::noteHandshakeSuccessInPeerRecord()
{
    auto pr = PeerRecord::loadPeerRecord(mApp.getDatabase(), getIP(),
                                         getRemoteListeningPort());
    if (pr)
    {
        pr->resetBackOff(mApp.getClock());
    }
    else
    {
        pr = make_optional<PeerRecord>(getIP(), mRemoteListeningPort,
                                       mApp.getClock().now());
    }
    CLOG(INFO, "Overlay") << "sucessful handshake with " << pr->toString();
    pr->storePeerRecord(mApp.getDatabase());
}

void
Peer::recvHello(StellarMessage const& msg)
{
    using xdr::operator==;
    if (msg.hello().peerID == mApp.getConfig().NODE_SEED.getPublicKey())
    {
        CLOG(DEBUG, "Overlay") << "connecting to self";
        drop();
        return;
    }

    if (msg.hello().networkID != mApp.getNetworkID())
    {
        CLOG(INFO, "Overlay") << "connection from misconfigured peer";
        CLOG(DEBUG, "Overlay")
            << "NetworkID = " << hexAbbrev(msg.hello().networkID)
            << " expected: " << hexAbbrev(mApp.getNetworkID());
        drop();
        return;
    }

    mRemoteOverlayVersion = msg.hello().overlayVersion;
    mRemoteVersion = msg.hello().versionStr;
    if (msg.hello().listeningPort <= 0 ||
        msg.hello().listeningPort > UINT16_MAX)
    {
        CLOG(DEBUG, "Overlay") << "bad port in recvHello";
        drop();
        return;
    }

    mRemoteListeningPort =
        static_cast<unsigned short>(msg.hello().listeningPort);
    CLOG(DEBUG, "Overlay") << "recvHello from " << toString();
    mState = GOT_HELLO;
    mPeerID = msg.hello().peerID;
    mReceivedNonce = msg.hello().nonce;

    if (mRole == WE_CALLED_REMOTE)
    {
        sendAuth();
    }
    else
    {
        sendHello();
    }
}

void
Peer::recvAuth(StellarMessage const& msg)
{

    if (mState != GOT_HELLO)
    {
        CLOG(ERROR, "Overlay") << "Unexpected AUTH message";
        drop();
        return;
    }

    std::vector<uint8_t> bytes;
    bytes.insert(bytes.end(), mReceivedNonce.begin(), mReceivedNonce.end());
    bytes.insert(bytes.end(), mSentNonce.begin(), mSentNonce.end());

    if (!PubKeyUtils::verifySig(mPeerID, msg.auth().signature, bytes))
    {
        CLOG(ERROR, "Overlay") << "Bad signature on AUTH message";
        drop();
        return;
    }

    noteHandshakeSuccessInPeerRecord();

    mState = GOT_AUTH;

    if (mRole == REMOTE_CALLED_US)
    {
        if (mApp.getOverlayManager().isPeerAccepted(shared_from_this()))
        {
            sendAuth();
            sendPeers();
        }
        else
        {
            CLOG(WARNING, "Overlay") << "New peer rejected, all slots taken";
            sendPeers();
            drop();
        }
    }

}

void
Peer::recvGetPeers(StellarMessage const& msg)
{
    sendPeers();
}

void
Peer::recvPeers(StellarMessage const& msg)
{
    for (auto const& peer : msg.peers())
    {
        stringstream ip;

        ip << (int)peer.ip[0] << "." << (int)peer.ip[1] << "."
           << (int)peer.ip[2] << "." << (int)peer.ip[3];

        if (peer.port == 0 || peer.port > UINT16_MAX)
        {
            CLOG(DEBUG, "Overlay") << "ignoring peer with bad port";
            continue;
        }
        PeerRecord pr{ip.str(), static_cast<unsigned short>(peer.port),
                      mApp.getClock().now(), peer.numFailures};

        if (pr.isPrivateAddress())
        {
            CLOG(DEBUG, "Overlay") << "ignoring flooded private address";
        }
        else
        {
            pr.insertIfNew(mApp.getDatabase());
        }
    }
}
}
