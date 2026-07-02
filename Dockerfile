
FROM alpine:latest AS dev

RUN apk add --no-cache \
        # build toolchain
        build-base \
        cmake \
        make \
        musl-dev \
        linux-headers \
        pkgconf \
        json-c-dev \
        iproute2 \
        nftables \
        iw \
        hostapd \
        dnsmasq \
        tzdata

WORKDIR /workspace

CMD ["tail", "-f", "/dev/null"]

FROM dev AS builder

WORKDIR /src

COPY CMakeLists.txt ./
COPY include/       ./include/
COPY src/           ./src/
COPY tests/         ./tests/

RUN set -eux; \
    cp /usr/lib/pkgconfig/json-c.pc /usr/lib/pkgconfig/json-c.pc.orig; \
    printf 'prefix=/usr\nexec_prefix=/usr\nlibdir=/usr/lib\nincludedir=/usr/include\n\nName: json-c\nDescription: A JSON implementation in C\nVersion: 0.18\nRequires:\nLibs.private: -lm\nLibs: -L${libdir} -l:libjson-c.a\nCflags: -I${includedir} -I${includedir}/json-c\n' \
        > /usr/lib/pkgconfig/json-c.pc

RUN cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_EXE_LINKER_FLAGS="-static" \
 && cmake --build build -j"$(nproc)"

RUN strip build/route-daemon

FROM alpine:latest AS runtime

RUN apk add --no-cache \
        iproute2 \
        nftables \
        iw \
        hostapd \
        dnsmasq \
        tzdata

RUN mkdir -p /etc/route-daemon \
             /var/log/route-daemon \
             /var/lib/route-daemon \
             /run

COPY --from=builder /src/build/route-daemon /usr/local/sbin/route-daemon

COPY scripts/entrypoint.sh                          /usr/local/bin/entrypoint.sh
COPY config/route-daemon.example.json               /etc/route-daemon/config.example.json

RUN chmod +x /usr/local/bin/entrypoint.sh /usr/local/sbin/route-daemon

VOLUME ["/etc/route-daemon", "/var/lib/route-daemon", "/var/log/route-daemon", "/run"]


HEALTHCHECK --interval=30s --timeout=5s --start-period=30s --retries=3 \
    CMD sh -c 'pidf=/run/route-daemon.pid; \
               [ -f "$pidf" ] || exit 1; \
               pid=$(cat "$pidf" 2>/dev/null) || exit 1; \
               [ -n "$pid" ] || exit 1; \
               kill -0 "$pid" 2>/dev/null'

LABEL org.opencontainers.image.title="Route-Daemon" \
      org.opencontainers.image.description="Performant C-based software router (WiFi AP, NAT, firewall, DHCP, shaping)" \
      org.opencontainers.image.source="https://github.com/example/route-daemon" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.version="0.1.0"

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
