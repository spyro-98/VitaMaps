# Contributing

Build in `Release` and keep the effective application and `vita-https` compile
flags at `-O3`. Do not add WebView, HTML, JavaScript map engines, unauthenticated
HTTP, certificate-verification bypasses, or the legacy VitaSDK OpenSSL stack.

Changes that touch rendering or input should be checked on a 960x544 viewport
and on PS Vita hardware. Networking and disk work must remain off the render
thread, while vita2d/GXM texture operations must remain on it.

Before proposing a change:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
unzip -t build/VitaMaps.vpk
```

Provider additions must document attribution and terms. Do not add area
download or aggressive prefetch behavior to the public OpenStreetMap standard
tile provider.
