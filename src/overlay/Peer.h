#ifndef __PEER__
#define __PEER__

#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif
#include <asio.hpp>
#include <deque>
#include <mutex>
#include <random>
#include "xdrpp/message.h"
#include "generated/stellar.hh"
#include "fba/QuorumSet.h"
#include "overlay/StellarMessage.h"
#include "util/timer.h"

/*
Another peer out there that we are connected to
*/

namespace stellar
{
    using namespace std;

    class Application;
    class LoopbackPeer;

    class Peer : public enable_shared_from_this <Peer>
    {

    public:
        typedef std::shared_ptr<Peer> pointer;

        enum PeerState
        {
            CONNECTING,
            CONNECTED,
            GOT_HELLO,
            CLOSING
        };

        enum PeerRole
        {
            INITIATOR,
            ACCEPTOR
        };

    protected:
        Application &mApp;

        PeerRole mRole;
        PeerState mState;

        std::string mRemoteVersion;
        int mRemoteProtocolVersion;
        int mRemoteListeningPort;
        void recvMessage(StellarMessagePtr msg);
        void recvMessage(xdr::msg_ptr const& xdrBytes);

        virtual void recvError(StellarMessagePtr msg);
        virtual void recvHello(StellarMessagePtr msg);
        void recvDontHave(StellarMessagePtr msg);
        void recvGetPeers(StellarMessagePtr msg);
        void recvPeers(StellarMessagePtr msg);
        void recvGetHistory(StellarMessagePtr msg);
        void recvHistory(StellarMessagePtr msg);
        void recvGetDelta(StellarMessagePtr msg);
        void recvDelta(StellarMessagePtr msg);
        void recvGetTxSet(StellarMessagePtr msg);
        void recvTxSet(StellarMessagePtr msg);
        void recvGetValidations(StellarMessagePtr msg);
        void recvValidations(StellarMessagePtr msg);
        void recvTransaction(StellarMessagePtr msg);
        void recvGetQuorumSet(StellarMessagePtr msg);
        void recvQuorumSet(StellarMessagePtr msg);
        void recvFBAMessage(StellarMessagePtr msg);

        void sendHello();
        void sendQuorumSet(QuorumSet::pointer qSet);
        void sendDontHave(stellarxdr::MessageType type, stellarxdr::uint256& itemID);
        void sendPeers();

        virtual void sendMessage(xdr::msg_ptr&& xdrBytes) = 0;

    public:

        Peer(Application &app, PeerRole role);
        Application &getApp() { return mApp; }

        void sendGetTxSet(stellarxdr::uint256& setID);
        void sendGetQuorumSet(stellarxdr::uint256& setID);

        void sendMessage(stellarxdr::StellarMessage msg);

        PeerRole getRole() const { return mRole; }
        PeerState getState() const { return mState; }

        std::string const &getRemoreVersion() const { return mRemoteVersion; }
        int getRemoteProtocolVersion() const { return mRemoteProtocolVersion; }
        int getRemoteListeningPort() { return mRemoteListeningPort; }

        // These exist mostly to be overridden in TCPPeer and callable via
        // shared_ptr<Peer> as a captured shared_from_this().
        virtual void connectHandler(const asio::error_code& ec);
        virtual void writeHandler(const asio::error_code& error, std::size_t bytes_transferred) {}
        virtual void readHeaderHandler(const asio::error_code& error, std::size_t bytes_transferred) {}
        virtual void readBodyHandler(const asio::error_code& error, std::size_t bytes_transferred) {}

        virtual void drop() = 0;
        virtual std::string getIP() = 0;
        virtual ~Peer() {}

        friend class LoopbackPeer;
    };
}

#endif
