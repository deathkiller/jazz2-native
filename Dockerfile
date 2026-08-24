# Jazz² Resurrection - Dedicated multiplayer server
#
# The image contains only the game engine. The original Jazz Jackrabbit 2 files are *not* part of it
# and have to be provided by the operator as a volume, exactly like for a local installation.
#
# The image is published for both amd64 and arm64, so this file is needed only to build it from
# sources:
#
#   docker pull ghcr.io/deathkiller/jazz2-server:latest
#
# where ":latest" is the latest release and ":edge" the current state of the "master" branch.
#
# Build (from the repository root):
#
#   docker build -t jazz2-server .
#
# Run:
#
#   docker run -d -i --name jazz2-server \
#       -p 7438:7438/udp -p 7438:7438/tcp \
#       -v "$PWD/Source:/app/Source:ro" \
#       -v "$PWD/Config:/app/Config" \
#       -v jazz2-cache:/app/Cache \
#       jazz2-server
#
# where "./Config/" contains "Jazz2.Server.config" (the server configuration) and "./Source/" the
# original game files.
#
# See "docker-compose.yml" for the same thing spelled out declaratively and
# "Docs/Snippets/ServerConfiguration.json" for a documented configuration file.

ARG DEBIAN_RELEASE=bookworm

# ─── Build stage ──────────────────────────────────────────────────────────────────────────────────
FROM debian:${DEBIAN_RELEASE}-slim AS build

# The dedicated server needs no graphics or audio libraries at all - libcurl is the mandatory HTTP
# backend of `WebRequest` (the online server list), OpenSSL is the TLS backend of the WebSocket
# transport and zlib is used for packet and asset compression
RUN apt-get update \
	&& apt-get install -y --no-install-recommends \
		build-essential \
		cmake \
		libcurl4-openssl-dev \
		libssl-dev \
		zlib1g-dev \
	&& rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Only the compiled catalogs are loaded at runtime, the sources of the translations are not needed
RUN find ./Content/ -name '*.po' -delete

# DEDICATED_SERVER builds the headless server (no window backend, no audio backend, no ImGui) and
# implies online multiplayer. The rendering backend is dead code in this build - the server never
# creates a graphics device - but the OpenGL one would still link against libGL, so the software
# rasterizer is selected to keep the runtime image free of any GL runtime. NCINE_WITH_AUDIO=OFF and
# NCINE_WITH_GLEW=OFF only stop CMake from looking for (or downloading and building) libraries that
# a headless build cannot use anyway. The two offline tools (AssetPacker, ShaderCompiler) are already
# off by default in a DEDICATED_SERVER build, so nothing here has to ask for that.
RUN cmake -B ./_build/ -S . \
		-D CMAKE_BUILD_TYPE=Release \
		-D DEDICATED_SERVER=ON \
		-D NCINE_PREFERRED_RHI=Software \
		-D NCINE_WITH_AUDIO=OFF \
		-D NCINE_WITH_GLEW=OFF \
		-D NCINE_VERSION_FROM_GIT=OFF \
		-D NCINE_STRIP_BINARIES=ON \
	&& cmake --build ./_build/ --parallel "$(nproc)"

# ─── Runtime stage ────────────────────────────────────────────────────────────────────────────────
FROM debian:${DEBIAN_RELEASE}-slim

RUN apt-get update \
	&& apt-get install -y --no-install-recommends \
		ca-certificates \
		libcurl4 \
		libssl3 \
		zlib1g \
	&& rm -rf /var/lib/apt/lists/* \
	&& groupadd --gid 1000 jazz2 \
	&& useradd --uid 1000 --gid 1000 --no-create-home --shell /usr/sbin/nologin jazz2

WORKDIR /app

COPY --from=build /src/_build/jazz2 ./jazz2
COPY --from=build /src/Content/ ./Content/
COPY --from=build /src/LICENSE ./LICENSE

# "Source" holds the original game files (mounted read-only), "Config" the server configuration and
# the preferences file, "Cache" receives the converted assets on the first start and has to survive
# restarts. A named volume inherits the ownership of the directory it shadows, so the unprivileged
# user can write into both writable ones out of the box; a bind-mounted host directory has to be
# owned by uid 1000 instead.
RUN mkdir -p ./Source/ ./Config/ ./Cache/ && chown jazz2:jazz2 ./Config/ ./Cache/

# "ServerPort" (enet transport) is UDP, "WsPort" (WebSocket transport) is TCP; both default to 7438
EXPOSE 7438/udp 7438/tcp

USER jazz2

# The first argument is the server configuration, "/config" is the path of the preferences file. Both
# are absolute, because a relative path (and every "$include" inside the configuration) is resolved
# against the directory of the preferences file rather than against the working directory. Keep both
# arguments when overriding the command.
ENTRYPOINT ["/app/jazz2"]
CMD ["/app/Config/Jazz2.Server.config", "/config", "/app/Config/Jazz2.config"]
