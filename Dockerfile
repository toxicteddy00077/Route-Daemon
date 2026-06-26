FROM alpine:latest

# Install only the core utilities needed to inspect and alter network interfaces
RUN apk add --no-cache \
    iw \
    wireless-tools \
    iproute2 \
    iptables \
    bash

# Keep the container running in the background so we can exec into it
CMD ["tail", "-f", "/dev/null"]
