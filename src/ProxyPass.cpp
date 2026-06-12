// Copyright © 2026 SculkCatalystMC. All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, version 3 of the License.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ProxyPass.hpp"
#include <sculk/protocol/codec/MinecraftPackets.hpp>
#include <sculk/protocol/codec/packet/ClientToServerHandshakePacket.hpp>
#include <sculk/protocol/codec/packet/DisconnectPacket.hpp>
#include <sculk/protocol/codec/packet/PlayStatusPacket.hpp>
#include <sculk/protocol/connection/HandShakeToken.hpp>

#include <print>

namespace sculk {

#define PROXY_PASS_SHOULD_LOG_PACKET(ID)                                                                               \
    (mSettings.packets_logger->black_list_mode && !mSettings.packets_logger->packet_ids->contains(ID))                 \
        || (!mSettings.packets_logger->black_list_mode && mSettings.packets_logger->packet_ids->contains(ID))

ProxyPass::ProxyPass(protocol::AuthenticationKeyManager const& authManager, ProxySettings& settings)
: mProxyServer(1),
  mAuthManager(authManager),
  mSettings(settings) {}

bool ProxyPass::start() {
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
    mProxyServer.setMotd(mSettings.motd);
    return mProxyServer.start(mSettings.proxy_port, mSettings.proxy_port_v6, mSettings.max_players);
}

void ProxyPass::disconnectClient(const RakNet::RakNetGUID& guid, protocol::PlayStatus status) {
    protocol::PlayStatusPacket playStatusPacket{};
    playStatusPacket.mStatus = status;
    mProxyServer.disconnectClient(guid);

    std::shared_ptr<ProxyBridge> bridge{};
    mBridges.erase_if(guid.g, [&bridge](auto& entry) {
        bridge = entry.second;
        return true;
    });

    if (bridge && bridge->mProxyClient.isConnected()) {
        bridge->mProxyClient.disconnect();
    }
}

void ProxyPass::disconnectClient(
    const RakNet::RakNetGUID&      guid,
    std::string_view               message,
    protocol::DisconnectFailReason reason
) {
    protocol::DisconnectPacket disconnectPacket{};
    disconnectPacket.mReason  = reason;
    disconnectPacket.mMessage = message;
    if (auto session = mProxyServer.getSession(guid)) {
        protocol::Session::Buffer buffer{};
        protocol::BinaryStream    stream{buffer};
        disconnectPacket.writeWithHeader(stream);
        session->sendPacketImmediately(std::move(buffer));
    }
    mProxyServer.disconnectClient(guid);

    std::shared_ptr<ProxyBridge> bridge{};
    mBridges.erase_if(guid.g, [&bridge](auto& entry) {
        bridge = entry.second;
        return true;
    });

    if (bridge && bridge->mProxyClient.isConnected()) {
        bridge->mProxyClient.disconnect();
    }
}

void ProxyPass::onClientDisconnected(const RakNet::RakNetGUID& guid) {
    std::shared_ptr<ProxyBridge> bridge{};
    mBridges.erase_if(guid.g, [&bridge](auto& entry) {
        bridge = entry.second;
        return true;
    });

    if (!bridge) {
        return;
    }

    if (bridge->mProxyClient.isConnected()) {
        bridge->mProxyClient.disconnect();
    }
    std::println(
        "[ProxyPass] [{}] Player disconnected: {}, xuid: {}, pfid: {}",
        bridge->mRealAddress.ToString(),
        bridge->mClientInfo.name,
        bridge->mClientInfo.xuid.empty() ? "N/A" : bridge->mClientInfo.xuid,
        bridge->mClientInfo.pfid.empty() ? "N/A" : bridge->mClientInfo.pfid
    );
}

void ProxyPass::processClientPacket(ProxyBridge& bridge, const protocol::IPacket& packet) {
    auto id = packet.getId();
    switch (id) {
    case protocol::MinecraftPacketIds::Login: {
        return handleClient(bridge, static_cast<const protocol::LoginPacket&>(packet));
    }
    case protocol::MinecraftPacketIds::ClientToServerHandshake: {
        if (PROXY_PASS_SHOULD_LOG_PACKET(id)) {
            std::println("[ProxyPass] Client => Proxy | {}", packet);
        }
        if (bridge.mRealClientSession.isConnected()) {
            auto pkt = protocol::RequestNetworkSettingsPacket{protocol::getProtocolVersion()};
            bridge.sendPacketToServer(pkt, true);
            if (PROXY_PASS_SHOULD_LOG_PACKET(id)) {
                std::println("[ProxyPass] Proxy => Server | {}", pkt);
            }
        }
        bridge.mClientReady.store(true, std::memory_order_release);
        break;
    }
    default: {
        if (PROXY_PASS_SHOULD_LOG_PACKET(id)) {
            std::println("[ProxyPass] Client => Proxy => Server | {}", packet);
        }
        bridge.sendPacketToServer(packet);
        break;
    }
    }
}

void ProxyPass::handleClient(protocol::Session& session, const protocol::RequestNetworkSettingsPacket& packet) {
    if (PROXY_PASS_SHOULD_LOG_PACKET(protocol::MinecraftPacketIds::RequestNetworkSettings)) {
        std::println("[ProxyPass] Client => Proxy | {}", packet);
    }
    if (packet.mClientNetworkVersion != protocol::getProtocolVersion()) {
        if (packet.mClientNetworkVersion > protocol::getProtocolVersion()) {
            return disconnectClient(session.getGuid(), protocol::PlayStatus::LoginFailedServerOld);
        } else {
            return disconnectClient(session.getGuid(), protocol::PlayStatus::LoginFailedClientOld);
        }
    }
    protocol::NetworkSettingsPacket settingsPacket{};
    protocol::Session::Buffer       buffer{};
    protocol::BinaryStream          stream{buffer};
    settingsPacket.writeWithHeader(stream);
    if (PROXY_PASS_SHOULD_LOG_PACKET(protocol::MinecraftPacketIds::NetworkSettings)) {
        std::println("[ProxyPass] Proxy => Client | {}", settingsPacket);
    }
    session.sendPacketImmediately(std::move(buffer));
    session.setCompressed(
        static_cast<protocol::Session::CompressionType>(settingsPacket.mCompressionAlgorithm),
        settingsPacket.mCompressionThreshold
    );
}

void ProxyPass::handleClient(ProxyBridge& bridge, const protocol::LoginPacket& packet) {
    if (PROXY_PASS_SHOULD_LOG_PACKET(protocol::MinecraftPacketIds::Login)) {
        std::println("[ProxyPass] Client => Proxy | {}", packet);
    }

    if (packet.mNetworkVersion != protocol::getProtocolVersion()) {
        if (packet.mNetworkVersion > protocol::getProtocolVersion()) {
            return disconnectClient(bridge.mRealGuid, protocol::PlayStatus::LoginFailedServerOld);
        } else {
            return disconnectClient(bridge.mRealGuid, protocol::PlayStatus::LoginFailedClientOld);
        }
    }

    auto request = protocol::ConnectionRequest::fromString(packet.mRawConnectionRequest);
    if (!request) {
        return disconnectClient(
            bridge.mRealGuid,
            "Invalid connection request",
            protocol::DisconnectFailReason::BadPacket
        );
    }

    bridge.mConnectionRequest = std::move(*request);
    bridge.mClientInfo.name   = bridge.mConnectionRequest.getXboxLiveName();
    bridge.mClientInfo.xuid   = bridge.mConnectionRequest.getXboxLiveID().value_or("");
    bridge.mClientInfo.pfid   = bridge.mConnectionRequest.getPlayFabID();
    if (!bridge.mConnectionRequest.verify(mAuthManager, mSettings.online_mode)) {
        return disconnectClient(
            bridge.mRealGuid,
            "Connection request verification failed",
            protocol::DisconnectFailReason::NotAuthenticated
        );
    }

    auto token = protocol::HandShakeToken::random(mProxyServerKeyPair);
    if (!token) {
        return disconnectClient(
            bridge.mRealGuid,
            "Failed to generate handshake token",
            protocol::DisconnectFailReason::BadPacket
        );
    }
    protocol::ServerToClientHandshakePacket handshakePacket{};
    handshakePacket.mHandshakeWebToken = token->toString();

    bridge.sendPacketToClient(handshakePacket, true);
    if (PROXY_PASS_SHOULD_LOG_PACKET(protocol::MinecraftPacketIds::ServerToClientHandshake)) {
        std::println("[ProxyPass] Proxy => Client | {}", handshakePacket);
    }

    auto sessionToken = protocol::CryptoManager::computeSessionKey(
        mProxyServerKeyPair.mPrivateKeyPem,
        bridge.mConnectionRequest.getClientPublicKey(),
        token->getSaltBytes()
    );
    if (!sessionToken) {
        return disconnectClient(
            bridge.mRealGuid,
            "Failed to compute client session token",
            protocol::DisconnectFailReason::BadPacket
        );
    }
    bridge.mRealClientSession.setEncrypted(std::move(*sessionToken));
    auto pfid = bridge.mConnectionRequest.getPlayFabID();
    std::println(
        "[ProxyPass] [{}] Player connected: {}, xuid: {}, pfid: {}",
        bridge.mRealAddress.ToString(),
        bridge.mClientInfo.name,
        bridge.mClientInfo.xuid.empty() ? "N/A" : bridge.mClientInfo.xuid,
        bridge.mClientInfo.pfid.empty() ? "N/A" : bridge.mClientInfo.pfid
    );
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

    std::shared_ptr<ProxyBridge> bridge{};
    auto [bridgePtr, inserted] = mBridges.try_emplace_p(guid.g, std::make_shared<ProxyBridge>(guid, address, session));
    bridge                     = bridgePtr->second;

    std::weak_ptr<ProxyBridge> weakBridge = bridge;

    bridge->mProxyClient.setOnPacketReceive([this, weakBridge](std::unique_ptr<protocol::IPacket>&& packet) noexcept {
        auto currentBridge = weakBridge.lock();
        if (!currentBridge) {
            return;
        }
        processServerPacket(*currentBridge, *packet);
    });

    bridge->mProxyClient.setOnConnected([this, weakBridge]() noexcept {
        auto currentBridge = weakBridge.lock();
        if (!currentBridge) {
            return;
        }

        if (!currentBridge->mClientReady.load(std::memory_order_acquire)) {
            return;
        }

        auto pkt = protocol::RequestNetworkSettingsPacket{protocol::getProtocolVersion()};
        currentBridge->sendPacketToServer(pkt, true);
        if (PROXY_PASS_SHOULD_LOG_PACKET(pkt.getId())) {
            std::println("[ProxyPass] Proxy => Server | {}", pkt);
        }

        currentBridge->mClientReady.store(true, std::memory_order_release);
    });

    bridge->mProxyClient.setOnConnectionFailed([this, guid, weakBridge]() noexcept {
        auto currentBridge = weakBridge.lock();
        if (!currentBridge) {
            return;
        }

        std::println(
            "[ProxyPass] Failed to connect to upstream server for player: {}.",
            currentBridge->mConnectionRequest.getXboxLiveName()
        );
        std::println(
            "[ProxyPass] [{}] Player disconnected: {}, xuid: {}, pfid: {}",
            currentBridge->mRealAddress.ToString(),
            currentBridge->mClientInfo.name,
            currentBridge->mClientInfo.xuid.empty() ? "N/A" : currentBridge->mClientInfo.xuid,
            currentBridge->mClientInfo.pfid.empty() ? "N/A" : currentBridge->mClientInfo.pfid
        );
        disconnectClient(guid, "Failed to connect to upstream server", protocol::DisconnectFailReason::CantConnect);
    });

    if (!bridge->mProxyClient.connect(mSettings.upstream_host, mSettings.upstream_port)) {
        std::println(
            "[ProxyPass] Failed to connect to upstream server for player: {}.",
            bridge->mConnectionRequest.getXboxLiveName()
        );
        std::println(
            "[ProxyPass] [{}] Player disconnected: {}, xuid: {}, pfid: {}",
            bridge->mRealAddress.ToString(),
            bridge->mClientInfo.name,
            bridge->mClientInfo.xuid.empty() ? "N/A" : bridge->mClientInfo.xuid,
            bridge->mClientInfo.pfid.empty() ? "N/A" : bridge->mClientInfo.pfid
        );
        disconnectClient(guid, "Failed to connect to upstream server", protocol::DisconnectFailReason::CantConnect);
        return;
    }

    return handleClient(session, static_cast<const protocol::RequestNetworkSettingsPacket&>(packet));
}

void ProxyPass::onRealClientPacket(
    const RakNet::RakNetGUID&    guid,
    const RakNet::SystemAddress& address,
    const protocol::IPacket&     packet
) {
    auto session = mProxyServer.getSession(guid);
    if (!session) {
        return;
    }

    std::shared_ptr<ProxyBridge> bridge{};
    if (!mBridges.if_contains(guid.g, [&bridge](auto const& entry) { bridge = entry.second; })) {
        return handleFirstClientPacket(guid, address, packet, *session);
    }

    processClientPacket(*bridge, packet);
}

void ProxyPass::processServerPacket(ProxyBridge& bridge, const protocol::IPacket& packet) {
    auto id = packet.getId();
    switch (id) {
    case protocol::MinecraftPacketIds::NetworkSettings: {
        handleServer(bridge, static_cast<const protocol::NetworkSettingsPacket&>(packet));
        break;
    }
    case protocol::MinecraftPacketIds::ServerToClientHandshake: {
        handleServer(bridge, static_cast<const protocol::ServerToClientHandshakePacket&>(packet));
        break;
    }
    default: {
        if (PROXY_PASS_SHOULD_LOG_PACKET(id)) {
            std::println("[ProxyPass] Server => Proxy => Client | {}", packet);
        }
        bridge.sendPacketToClient(packet);
        break;
    }
    }
}

void ProxyPass::handleServer(ProxyBridge& bridge, const protocol::NetworkSettingsPacket& packet) {
    if (PROXY_PASS_SHOULD_LOG_PACKET(protocol::MinecraftPacketIds::NetworkSettings)) {
        std::println("[ProxyPass] Server => Proxy | {}", packet);
    }
    bridge.mProxyClient.getSession().setCompressed(
        static_cast<protocol::Session::CompressionType>(packet.mCompressionAlgorithm),
        packet.mCompressionThreshold
    );

    protocol::LoginPacket loginPacket{};
    loginPacket.mNetworkVersion = protocol::getProtocolVersion();
    (void)bridge.mConnectionRequest.selfSign(mProxyServerKeyPair);
    loginPacket.mRawConnectionRequest = bridge.mConnectionRequest.toString();

    bridge.sendPacketToServer(loginPacket, true);
    if (PROXY_PASS_SHOULD_LOG_PACKET(protocol::MinecraftPacketIds::Login)) {
        std::println("[ProxyPass] Proxy => Server | {}", loginPacket);
    }
}

void ProxyPass::handleServer(ProxyBridge& bridge, const protocol::ServerToClientHandshakePacket& packet) {
    if (PROXY_PASS_SHOULD_LOG_PACKET(protocol::MinecraftPacketIds::ServerToClientHandshake)) {
        std::println("[ProxyPass] Server => Proxy | {}", packet);
    }
    auto handshakeToken = protocol::HandShakeToken::fromString(packet.mHandshakeWebToken);
    if (!handshakeToken || !handshakeToken->verify()) {
        auto pfid = bridge.mConnectionRequest.getPlayFabID();
        std::println(
            "[ProxyPass] [{}] Player disconnected: {}, xuid: {}, pfid: {}",
            bridge.mRealAddress.ToString(),
            bridge.mClientInfo.name,
            bridge.mClientInfo.xuid.empty() ? "N/A" : bridge.mClientInfo.xuid,
            bridge.mClientInfo.pfid.empty() ? "N/A" : bridge.mClientInfo.pfid
        );
        return disconnectClient(bridge.mRealGuid, "Invalid handshake token", protocol::DisconnectFailReason::BadPacket);
    }

    auto sessionKey = protocol::CryptoManager::computeSessionKey(
        mProxyServerKeyPair.mPrivateKeyPem,
        handshakeToken->getRemotePublicKey(),
        handshakeToken->getSaltBytes()
    );
    if (!sessionKey) {
        std::println(
            "[ProxyPass] [{}] Player disconnected: {}, xuid: {}, pfid: {}",
            bridge.mRealAddress.ToString(),
            bridge.mClientInfo.name,
            bridge.mClientInfo.xuid.empty() ? "N/A" : bridge.mClientInfo.xuid,
            bridge.mClientInfo.pfid.empty() ? "N/A" : bridge.mClientInfo.pfid
        );
        return disconnectClient(
            bridge.mRealGuid,
            "Failed to compute server session key",
            protocol::DisconnectFailReason::BadPacket
        );
    }
    bridge.mProxyClient.getSession().setEncrypted(std::move(*sessionKey));
    protocol::ClientToServerHandshakePacket handshakePacket{};
    bridge.sendPacketToServer(handshakePacket, true);
    if (PROXY_PASS_SHOULD_LOG_PACKET(protocol::MinecraftPacketIds::ClientToServerHandshake)) {
        std::println("[ProxyPass] Proxy => Server | {}", handshakePacket);
    }
}

} // namespace sculk