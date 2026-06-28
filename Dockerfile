# syntax=docker/dockerfile:1.7
FROM alpine:latest

# Runtime + Phase 0 build toolchain.
# NOTE: Phase 8 will split this into a multi-stage build (build layer vs slim runtime).
RUN apk add --no-cache \
    # --- build toolchain (Phase 0) ---
    build-base \
    cmake \
    make \
    musl-dev \
    linux-headers \
    pkgconf \
    git \
    # --- config/IPC parsing ---
    json-c-dev \
    # --- runtime network tooling ---
    iproute2 \
    nftables \
    iw \
    wireless-tools \
    bash \
    # --- timezone data for correct timestamps ---
    tzdata

# Keep the container running so we can exec into it.
CMD ["tail", "-f", "/dev/null"]
