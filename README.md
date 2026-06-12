# ProxyPass
[📄 中文文档](README.zh.md)  

MITM proxy tool for `Minecraft: Bedrock Edition`

![Protocol Version](https://img.shields.io/badge/Bedrock-v975%20%7C%2026.20-30ffee?style=flat-square)
![C++23](https://img.shields.io/badge/C++-23-blue?logo=C%2B%2B&logoColor=41a3ed)


## 🚀 Introduction
`ProxyPass` is a MITM proxy for `Minecraft: Bedrock Edition` that sits between an unmodified vanilla client and server.
It helps protocol developers inspect, forward, and optionally modify packets in real time, making protocol debugging and vanilla behavior analysis much easier.

## 📝 Notes
`ProxyPass` requires the backend server to run with online mode disabled (`online-mode=false`).
With online mode disabled, the server does not verify client identity, so `ProxyPass` can relay traffic between the client and server.

If online mode is enabled, the server will reject `ProxyPass` because it cannot provide valid authentication.
Make sure your server is configured correctly before testing.

## 🔗 Links
- [Protocol library](https://github.com/SculkCatalystMC/Protocol) used in this project

## 🤝 Contributing

Contributions are welcome.

- Open an Issue for bug reports, feature requests, or protocol discussions.
- Open a Pull Request for fixes, improvements, and new packet support.
- Keep changes focused and provide clear descriptions of behavior changes.

## ⚖️ License

This project is licensed under the GNU Affero General Public License v3.0 (AGPL-3.0).
See the full text in [LICENSE](LICENSE).

Core AGPL-3.0 requirements (summary):

- If you modify this software and distribute it, or make it available over a network, you must release the complete corresponding source code under AGPL-3.0.
- You must keep existing copyright and license notices in all source files.
- You may not impose any further restrictions on the recipients' exercise of the rights granted herein.
- Any derivative work or combined work must also be licensed under AGPL-3.0.

This summary is for convenience only. The [LICENSE](LICENSE) file is the authoritative legal text.

### Copyright © 2026 SculkCatalystMC. All rights reserved.