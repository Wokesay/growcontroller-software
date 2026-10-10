# SPDX-License-Identifier: AGPL-3.0-or-later
# The Home Assistant app (SD-034, docs/HOME_ASSISTANT.md): a static
# gc_ha_server, the web app and the license texts, nothing else: no shell,
# no package manager. Built from the repository root by
# .github/workflows/app.yml:
#   docker build -f ha/app/Dockerfile --build-arg BUILD_VERSION=<VERSION> \
#     --build-arg BUILD_ARCH=amd64|aarch64 --build-arg GC_SOURCE_REV=<commit> .
# Each stage gets only what it needs: the web app's npm packages never see
# the server's sources, so a bad build package cannot change the server.

# The web app. The commit is for its source code link (AGPL-3.0 §13).
FROM alpine:3.22@sha256:5291449c3df73caf6ed85e649dec1b9e818b39a5d8c871e97afc13e9cd5e8fa8 AS web
RUN apk add --no-cache nodejs npm
WORKDIR /src
COPY web/ web/
ARG GC_SOURCE_REV
RUN test -n "$GC_SOURCE_REV" || { echo "GC_SOURCE_REV is needed (AGPL-3.0 §13)"; exit 1; }
RUN cd web && npm ci --ignore-scripts --no-audit --no-fund && npm run build

# The server, without node. The release (= VERSION and the version in
# config.yaml) and the architecture Home Assistant knows it by are checked.
FROM alpine:3.22@sha256:5291449c3df73caf6ed85e649dec1b9e818b39a5d8c871e97afc13e9cd5e8fa8 AS server
RUN apk add --no-cache build-base cmake ninja linux-headers
WORKDIR /src
COPY CMakeLists.txt VERSION CHANGELOG.md ./
COPY cmake/ cmake/
COPY catalog/ catalog/
COPY core/ core/
COPY sim/ sim/
COPY ha/ ha/
ARG BUILD_VERSION
ARG BUILD_ARCH
RUN test "$BUILD_VERSION" = "$(tr -d '\r\n' < VERSION)" || { echo "BUILD_VERSION must be the version in VERSION"; exit 1; } \
 && case "$BUILD_ARCH-$(uname -m)" in amd64-x86_64|aarch64-aarch64) ;; *) echo "BUILD_ARCH does not match the build machine"; exit 1 ;; esac
# Static against musl: the image has no libraries, and musl resolves the
# Supervisor's name from /etc/resolv.conf without loadable modules.
# static-pie keeps address randomisation. musl gives a thread 128 KiB of
# stack unless the program asks for more; the web server's threads match
# paths with recursive regular expressions, so they get 1 MiB. The checks
# after the build prove both made it into the binary.
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGC_BUILD_TESTS=OFF -DGC_WERROR=OFF \
      "-DCMAKE_EXE_LINKER_FLAGS=-static-pie -Wl,-z,stack-size=1048576" \
 && cmake --build build --target gc_ha_server \
 && strip build/gc_ha_server \
 && readelf -h build/gc_ha_server | grep -q 'Type: *DYN' \
 && readelf -lW build/gc_ha_server | grep -Eq 'GNU_STACK .* 0x0*100000 RW' \
 && ! readelf -lW build/gc_ha_server | grep -q INTERP
# What musl and the C++ runtime came from (the image has no SBOM yet,
# docs/RELEASE.md). musl's license asks for its notice in every copy; the
# notice in tools/licenses is musl 1.2.5's, the version of Alpine 3.22.
RUN apk list -I musl | grep -q '^musl-1\.2\.5-r' || { echo "musl is not 1.2.5: update tools/licenses/musl-COPYRIGHT.txt"; exit 1; } \
 && apk list -I musl musl-dev libstdc++ libstdc++-dev libgcc gcc | sort > build/packages.txt \
 && test "$(wc -l < build/packages.txt)" = 6

# The license texts: ours, the web app's packages, the C++ headers, musl.
FROM alpine:3.22@sha256:5291449c3df73caf6ed85e649dec1b9e818b39a5d8c871e97afc13e9cd5e8fa8 AS licenses
RUN apk add --no-cache nodejs
WORKDIR /src
COPY LICENSE ./
COPY tools/third_party.mjs tools/
COPY tools/licenses/ tools/licenses/
COPY --from=web /src/web/package.json web/
COPY --from=web /src/web/node_modules web/node_modules
COPY --from=server /src/build/_deps/include build/_deps/include
COPY --from=server /src/build/packages.txt /out/BUILD_PACKAGES.txt
RUN cp LICENSE /out/LICENSE.txt \
 && cp tools/licenses/musl-COPYRIGHT.txt /out/musl-COPYRIGHT.txt \
 && node tools/third_party.mjs /out/THIRD_PARTY_LICENSES.txt

FROM scratch
ARG BUILD_VERSION
ARG BUILD_ARCH
ARG GC_SOURCE_REV
COPY --from=server /src/build/gc_ha_server /gc_ha_server
COPY --from=web /src/web/dist /web
COPY --from=licenses /out /licenses
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
