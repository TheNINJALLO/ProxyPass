#include "ProxyBridge.hpp"
#include "ProxySettings.hpp"
#include "sculk/protocol/auth/AuthenticationKeyManager.hpp"
#include "sculk/protocol/codec/packet/LoginPacket.hpp"
#include "sculk/protocol/codec/packet/NetworkSettingsPacket.hpp"
#include "sculk/protocol/codec/packet/RequestNetworkSettingsPacket.hpp"
#include "sculk/protocol/codec/packet/ServerToClientHandshakePacket.hpp"
#include "sculk/protocol/connection/ServerNetworkSystem.hpp"
#include "sculk/protocol/connection/io/ClientIoRuntime.hpp"
#include "sculk/protocol/connection/thread/ThreadPool.hpp"

namespace sculk {

class ProxyPass {
    protocol::thread::ThreadPool&                                   mSharedPool;
    protocol::io::ClientIoRuntime&                                  mSharedIoRuntime;
    protocol::ServerNetworkSystem                                   mProxyServer{};
    std::unordered_map<std::uint64_t, std::unique_ptr<ProxyBridge>> mBridges{};
    std::string                                                     mUpstreamHost{};
    std::uint16_t                                                   mUpstreamServerPort{};
    const protocol::AuthenticationKeyManager&                       mAuthManager;
    protocol::PemKeyPair                                            mProxyServerKeyPair{};
    std::mutex                                                      mBridgesMutex{};

public:
    ProxyPass(
        protocol::thread::ThreadPool&             sharedPool,
        protocol::io::ClientIoRuntime&            sharedIoRuntime,
        protocol::AuthenticationKeyManager const& authManager
    );

    bool start(
        std::uint16_t    protV4,
        std::uint16_t    protV6,
        std::uint32_t    maxConnections,
        std::string_view upstreamHost,
        std::uint16_t    upstreamPort
    );

private:
    void onClientDisconnected(const RakNet::RakNetGUID&);
    void onRealClientPacket(const RakNet::RakNetGUID&, const RakNet::SystemAddress&, const protocol::IPacket&);
    void handleFirstClientPacket(
        const RakNet::RakNetGUID&,
        const RakNet::SystemAddress&,
        const protocol::IPacket&,
        protocol::Session&
    );
    void processClientPacket(ProxyBridge&, const protocol::IPacket&);
    void handleClient(protocol::Session&, const protocol::RequestNetworkSettingsPacket&);
    void handleClient(ProxyBridge&, const protocol::LoginPacket&);
    void processServerPacket(ProxyBridge&, const protocol::IPacket&);
    void handleServer(ProxyBridge&, const protocol::NetworkSettingsPacket&);
    void handleServer(ProxyBridge&, const protocol::ServerToClientHandshakePacket&);
};

} // namespace sculk