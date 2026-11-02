# Third-party code and protocol references

libsignal-protocol-c 2.3.3 (GPL-3.0): original source archive is in core/deps/;
OpenSSL 3.0.18 (Apache-2.0): built from https://github.com/openssl/openssl;
OkHttp 4.12.0 and Okio (Apache-2.0): obtained from Maven Central;
zlib is provided by the operating system.

The binary token dictionary and protocol interpretation are adapted from Baileys:
https://github.com/WhiskeySockets/Baileys (MIT).
Dictionary source blob: 2fc851077687867b7b32d09de091b9a47e3c9b73.
Sources reviewed: src/Utils/noise-handler.ts, src/Utils/validate-connection.ts,
src/Utils/crypto.ts, src/Socket/socket.ts, src/Socket/messages-recv.ts,
src/WABinary/{constants,encode,decode}.ts, WAProto/WAProto.proto.
No Baileys runtime or JavaScript is included in the application.

MIT License

Copyright (c) 2026 Rajeh Taher/WhiskeySockets

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

