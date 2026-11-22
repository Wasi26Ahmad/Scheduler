# Multi-stage build for infsched daemon and tools
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git pkg-config \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build --parallel

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 10001 --no-create-home infsched

WORKDIR /opt/infsched
COPY --from=builder /src/build/src/daemon/infsched-daemon /usr/local/bin/infsched-daemon
COPY --from=builder /src/build/src/daemon/infsched-ctl /usr/local/bin/infsched-ctl
COPY --from=builder /src/build/src/worker/infsched-worker /usr/local/bin/infsched-worker
COPY --from=builder /src/build/src/sim/infsched-sim /usr/local/bin/infsched-sim
COPY configs/infsched.yaml /etc/infsched/infsched.yaml

USER infsched

EXPOSE 50051 9090

HEALTHCHECK --interval=10s --timeout=3s --start-period=30s --retries=3 \
  CMD infsched-ctl ready --port 9090

ENTRYPOINT ["infsched-daemon", "--config", "/etc/infsched/infsched.yaml"]
