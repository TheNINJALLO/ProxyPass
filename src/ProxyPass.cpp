#include "ProxyPass.hpp"
#include "sculk/protocol/codec/MinecraftPackets.hpp"
#include "sculk/protocol/codec/packet/ClientToServerHandshakePacket.hpp"
#include "sculk/protocol/connection/HandShakeToken.hpp"

#include <print>

namespace sculk {

ProxyPass::ProxyPass(
    protocol::thread::ThreadPool&             sharedPool,
    protocol::io::ClientIoRuntime&            sharedIoRuntime,
    protocol::AuthenticationKeyManager const& authManager
)
: mSharedPool(sharedPool),
  mSharedIoRuntime(sharedIoRuntime),
  mProxyServer(sharedPool),
  mAuthManager(authManager) {}

bool ProxyPass::start(
    std::uint16_t    protV4,
    std::uint16_t    protV6,
    std::uint32_t    maxConnections,
    std::string_view upstreamHost,
    std::uint16_t    upstreamPort
) {
    mUpstreamHost       = upstreamHost;
    mUpstreamServerPort = upstreamPort;

    auto serverKeyPair = protocol::ssl::randomES384KeyPair();
    if (!serverKeyPair) {
        return false;
    }
    mProxyServerKeyPair = *serverKeyPair;

    mProxyServer.setOnDisconnected([this](const RakNet::RakNetGUID& guid, const RakNet::SystemAddress&) noexcept {
        onClientDisconnected(guid);
    });

    mProxyServer.setOnPacketReceive([this](
                                        const RakNet::RakNetGUID&            guid,
                                        const RakNet::SystemAddress&         address,
                                        std::unique_ptr<protocol::IPacket>&& packet
                                    ) noexcept { onRealClientPacket(guid, address, *packet); });

    return mProxyServer.start(protV4, protV6, maxConnections);
}

void ProxyPass::onClientDisconnected(const RakNet::RakNetGUID& guid) {
    auto it = mBridges.find(guid.g);
    if (it == mBridges.end()) {
        return;
    }
    auto& bridge = *(it->second);

    bridge.mRealServerConnected.store(false, std::memory_order_release);
    if (bridge.mProxyClient.isConnected()) {
        bridge.mProxyClient.disconnect();
    }
    mBridges.erase(it);
    std::println("[server] Client {} disconnected.", guid.ToString());
}

void ProxyPass::processClientPacket(ProxyBridge& bridge, const protocol::IPacket& packet) {
    switch (packet.getId()) {
    case protocol::MinecraftPacketIds::Login: {
        return handleClient(bridge, static_cast<const protocol::LoginPacket&>(packet));
    }
    case protocol::MinecraftPacketIds::ClientToServerHandshake: {
        std::println("Client => Proxy | {}", packet);
        auto pkt = protocol::RequestNetworkSettingsPacket{protocol::getProtocolVersion()};
        bridge.sendPacketToServer(pkt, true);
        std::println("Proxy => Server | {}", pkt);
        break;
    }
    default: {
        std::println("Client => Proxy => Server | {}", packet);
        bridge.sendPacketToServer(packet);
        break;
    }
    }
}

void ProxyPass::handleClient(protocol::Session& session, const protocol::RequestNetworkSettingsPacket& packet) {
    std::println("Client => Proxy | {}", packet);
    if (packet.mClientNetworkVersion != protocol::getProtocolVersion()) {
        // TODO: handle protocol version mismatch, maybe disconnect with a message?
        return;
    }
    protocol::NetworkSettingsPacket settingsPacket{};
    protocol::Session::Buffer       buffer{};
    protocol::BinaryStream          stream{buffer};
    settingsPacket.writeWithHeader(stream);
    std::println("Proxy => Client | {}", settingsPacket);
    session.sendPacketImmediately(std::move(buffer));
    session.setCompression(
        static_cast<protocol::Session::CompressionType>(settingsPacket.mCompressionAlgorithm),
        settingsPacket.mCompressionThreshold
    );
}

void ProxyPass::handleClient(ProxyBridge& bridge, const protocol::LoginPacket& packet) {
    std::println("Client => Proxy | {}", packet);
    auto request = protocol::ConnectionRequest::fromString(packet.mRawConnectionRequest);
    if (!request) {
        // TODO: handle invalid connection request, maybe disconnect with a message?
        return;
    }
    bridge.mConnectionRequest = std::move(*request);
    auto verificationResult   = bridge.mConnectionRequest.verify(mAuthManager, false, true);
    if (!verificationResult) {
        // TODO: handle failed verification, maybe disconnect with a message?
        return;
    }

    auto token = protocol::HandShakeToken::random(mProxyServerKeyPair);
    if (!token) {
        // TODO: handle failed token generation, maybe disconnect with a message?
        return;
    }
    protocol::ServerToClientHandshakePacket handshakePacket{};
    handshakePacket.mHandshakeWebToken = token->toString();

    bridge.sendPacketToClient(handshakePacket, true);
    std::println("Proxy => Client | {}", handshakePacket);

    auto sessionToken = protocol::CryptoManager::computeSessionKey(
        mProxyServerKeyPair.mPrivateKeyPem,
        bridge.mConnectionRequest.getClientPublicKey(),
        token->getSaltBytes()
    );
    if (!sessionToken) {
        // TODO: handle failed session token computation, maybe disconnect with a message?
        return;
    }
    bridge.mRealClientSession.setEncrypted(std::move(*sessionToken));
}

void ProxyPass::handleFirstClientPacket(
    const RakNet::RakNetGUID&    guid,
    const RakNet::SystemAddress& address,
    const protocol::IPacket&     packet,
    protocol::Session&           session
) {
    if (packet.getId() != protocol::MinecraftPacketIds::RequestNetworkSettings) {
        return;
    }

    auto [it, inserted] =
        mBridges.emplace(guid.g, std::make_unique<ProxyBridge>(guid, address, mSharedPool, mSharedIoRuntime, session));
    auto& bridge = *(it->second);

    bridge.mProxyClient.setOnDisconnected([this, &bridge]() noexcept {
        bridge.mRealServerConnected.store(false, std::memory_order_release);
    });

    bridge.mProxyClient.setOnPacketReceive([this, &bridge](std::unique_ptr<protocol::IPacket>&& packet) noexcept {
        processServerPacket(bridge, *packet);
    });

    if (!bridge.mProxyClient.connect(mUpstreamHost, mUpstreamServerPort)) {
        bridge.mRealClientSession.disconnect();
        mBridges.erase(it);
        return;
    }
    bridge.mRealServerConnected.store(true, std::memory_order_release);

    return handleClient(session, static_cast<const protocol::RequestNetworkSettingsPacket&>(packet));
}

void ProxyPass::onRealClientPacket(
    const RakNet::RakNetGUID&    guid,
    const RakNet::SystemAddress& address,
    const protocol::IPacket&     packet
) {
    auto session = mProxyServer.getSession(guid);
    if (!session) {
        // TODO: logger
        return;
    }

    auto it = mBridges.find(guid.g);
    if (it == mBridges.end()) {
        return handleFirstClientPacket(guid, address, packet, *session);
    }
    auto& bridge = *(it->second);
    processClientPacket(bridge, packet);
}

void ProxyPass::processServerPacket(ProxyBridge& bridge, const protocol::IPacket& packet) {
    switch (packet.getId()) {
    case protocol::MinecraftPacketIds::NetworkSettings: {
        handleServer(bridge, static_cast<const protocol::NetworkSettingsPacket&>(packet));
        break;
    }
    case protocol::MinecraftPacketIds::ServerToClientHandshake: {
        handleServer(bridge, static_cast<const protocol::ServerToClientHandshakePacket&>(packet));
        break;
    }
    default: {
        std::println("Server => Proxy => Client | {}", packet);
        bridge.sendPacketToClient(packet);
        break;
    }
    }
}

void ProxyPass::handleServer(ProxyBridge& bridge, const protocol::NetworkSettingsPacket& packet) {
    std::println("Server => Proxy | {}", packet);
    bridge.mProxyClient.getSession().setCompression(
        static_cast<protocol::Session::CompressionType>(packet.mCompressionAlgorithm),
        packet.mCompressionThreshold
    );

    protocol::LoginPacket loginPacket{};
    loginPacket.mNetworkVersion = protocol::getProtocolVersion();
    bridge.mConnectionRequest.selfSign(mProxyServerKeyPair);
    loginPacket.mRawConnectionRequest = bridge.mConnectionRequest.toString();
    bridge.sendPacketToServer(loginPacket, true);
    std::println("Proxy => Server | {}", loginPacket);
}

void ProxyPass::handleServer(ProxyBridge& bridge, const protocol::ServerToClientHandshakePacket& packet) {
    std::println("Server => Proxy | {}", packet);
    auto handshakeToken = protocol::HandShakeToken::fromString(packet.mHandshakeWebToken);
    if (!handshakeToken || !handshakeToken->verify()) {
        // TODO: handle invalid handshake token, maybe disconnect with a message?
        return;
    }

    auto sessionKey = protocol::CryptoManager::computeSessionKey(
        mProxyServerKeyPair.mPrivateKeyPem,
        handshakeToken->getRemotePublicKey(),
        handshakeToken->getSaltBytes()
    );
    if (!sessionKey) {
        // TODO: handle invalid handshake token, maybe disconnect with a message?
        return;
    }
    bridge.mProxyClient.getSession().setEncrypted(std::move(*sessionKey));
    protocol::ClientToServerHandshakePacket handshakePacket{};
    bridge.sendPacketToServer(handshakePacket, true);
    std::println("Proxy => Server | {}", handshakePacket);
}

} // namespace sculk