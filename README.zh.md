# ProxyPass
[📄 English README](README.md)  

适用于 `Minecraft: Bedrock Edition` 的中间人代理工具

![Protocol Version](https://img.shields.io/badge/Bedrock-v975%20%7C%2026.20-30ffee?style=flat-square)
![C++23](https://img.shields.io/badge/C++-23-blue?logo=C%2B%2B&logoColor=41a3ed)


## 🚀 介绍
`ProxyPass` 是一个面向 `Minecraft: Bedrock Edition` 的 MITM 代理，位于未修改的原版客户端与服务器之间。
它可以帮助协议开发者实时检查、转发，并按需修改数据包，让协议调试与原版网络行为分析更加高效。

## 📝 注意事项
`ProxyPass` 需要后端服务器关闭在线模式（`online-mode=false`）才能正常工作。  
在关闭在线模式时，服务器不会验证客户端身份，因此 `ProxyPass` 才能在客户端与服务器之间转发流量。  

如果在线模式开启，服务器会拒绝 `ProxyPass` 连接，因为它无法提供有效身份验证。  
请先确认服务器配置正确，再进行测试。

## 🔗 链接
- [Protocol library](https://github.com/SculkCatalystMC/Protocol) 用于本项目

## 🤝 欢迎贡献

欢迎任何形式的贡献。

- 你可以通过 Issue 提交 Bug、功能建议和协议讨论。
- 你可以通过 Pull Request 提交修复、优化与新数据包支持。
- 建议保持改动聚焦，并在描述中清晰说明行为变化。

## ⚖️ 许可证

本项目基于 GNU Affero 通用公共许可证 v3.0 (AGPL-3.0) 授权。
完整协议文本请参见 [LICENSE](LICENSE)。

AGPL-3.0 核心要求（摘要）：

- 如果你修改了本软件并进行分发，或者将其作为网络服务提供，你必须以 AGPL-3.0 协议公开完整的对应源代码。
- 你必须在所有源文件中保留原有的版权声明和许可声明。
- 你不能对接收者行使本协议授予的权利施加任何进一步的限制。
- 任何衍生作品或组合作品也必须采用 AGPL-3.0 协议进行授权。

此摘要仅供参考。具有法律效力的权威文本为 [LICENSE](LICENSE) 文件。

### 版权所有 © 2026 SculkCatalystMC。保留所有权利。