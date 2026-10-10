# SPDX-License-Identifier: AGPL-3.0-or-later
# The Home Assistant app (SD-034, docs/HOME_ASSISTANT.md): a static
# gc_ha_server, the web app and the license texts, nothing else: no shell,
# no package manager. Built from the repository root by
# .github/workflows/app.yml:
#   docker build -f ha/app/Dockerfile --build-arg BUILD_VERSION=<VERSION> \
#     --build-arg BUILD_ARCH=amd64|aarch64 --build-arg GC_SOURCE_REV=<commit> .
FROM alpine:3.22@sha256:5291449c3df73caf6ed85e649dec1b9e818b39a5d8c871e97afc13e9cd5e8fa8 AS build
RUN apk add --no-cache build-base cmake ninja nodejs npm linux-headers
WORKDIR /src
COPY . .
# The release (= VERSION and the version in config.yaml), the architecture
# Home Assistant knows it by, and the commit for the source code link in
# the web app (AGPL-3.0 §13).
ARG BUILD_VERSION
ARG BUILD_ARCH
ARG GC_SOURCE_REV
RUN test "$BUILD_VERSION" = "$(tr -d '\r\n' < VERSION)" || { echo "BUILD_VERSION must be the version in VERSION"; exit 1; } \
 && case "$BUILD_ARCH-$(uname -m)" in amd64-x86_64|aarch64-aarch64) ;; *) echo "BUILD_ARCH does not match the build machine"; exit 1 ;; esac \
 && test -n "$GC_SOURCE_REV" || { echo "GC_SOURCE_REV is needed (AGPL-3.0 §13)"; exit 1; }
RUN cd web && npm ci --ignore-scripts --no-audit --no-fund && npm run build
# Static against musl: the image has no libraries, and musl resolves the
# Supervisor's name from /etc/resolv.conf without loadable modules. build/
# is where tools/third_party.mjs finds the C++ headers.
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGC_BUILD_TESTS=OFF -DGC_WERROR=OFF -DCMAKE_EXE_LINKER_FLAGS=-static \
 && cmake --build build --target gc_ha_server \
 && strip build/gc_ha_server
# The server holds musl, whose license asks for its notice in every copy:
# the notice in tools/licenses is musl 1.2.5's, the version of Alpine 3.22.
RUN apk list -I musl | grep -q '^musl-1\.2\.5-r' || { echo "musl is not 1.2.5: update tools/licenses/musl-COPYRIGHT.txt"; exit 1; } \
 && mkdir -p /out/web /out/licenses \
 && cp -r web/dist/. /out/web/ \
 && cp LICENSE /out/licenses/LICENSE.txt \
 && cp tools/licenses/musl-COPYRIGHT.txt /out/licenses/musl-COPYRIGHT.txt \
 && node tools/third_party.mjs /out/licenses/THIRD_PARTY_LICENSES.txt

FROM scratch
ARG BUILD_VERSION
ARG BUILD_ARCH
ARG GC_SOURCE_REV
COPY --from=build /src/build/gc_ha_server /gc_ha_server
COPY --from=build /out/web /web
COPY --from=build /out/licenses /licenses
# io.hass.*: what the Supervisor reads from an image it did not build
LABEL io.hass.version="$BUILD_VERSION" \
      io.hass.type="app" \
      io.hass.arch="$BUILD_ARCH" \
      org.opencontainers.image.source="https://github.com/Wokesay/growcontroller-software" \
      org.opencontainers.image.revision="$GC_SOURCE_REV" \
      org.opencontainers.image.version="$BUILD_VERSION" \
      org.opencontainers.image.licenses="AGPL-3.0-or-later" \
      org.opencontainers.image.title="growcontroller for Home Assistant"
EXPOSE 8099
ENTRYPOINT ["/gc_ha_server", "--app"]
