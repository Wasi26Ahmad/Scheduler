# infsched: High-Performance CPU Inference Scheduler

`infsched` is a Linux daemon and C++ library designed for multi-tenant machine learning inference on multi-core CPUs without requiring GPUs. It maximizes CPU throughput and keeps tail latency low by enforcing physical core isolation, cache-aware dynamic batching, and supervisor-based fault tolerance.

---

## Key Features

- **Whole-Physical-Core Affinity**: Dedicated physical cores are leased to each model. All hyperthread siblings on a core run together for the same model, eliminating L1/L2 cache contention and thread thrashing.
- **Cache-Aware Dynamic Batching**: Request batch sizes are capped so working activations fit within the model's software cache budget (private L2 + allocated share of L3). Requests are grouped dynamically only while meeting individual deadlines.
- **Dynamic On-Demand Lifecycle**: Models can be loaded and unloaded at runtime via CLI or HTTP ops API without restarting the daemon.
- **Process Isolation & Crash Supervision**: Backend runtimes execute inside isolated worker processes. If a worker fails or exceeds resource limits, the supervisor restarts it and recovers state automatically.
- **Multi-Tenant Admission**: Token bucket rate limiters reject excessive traffic early before queue buildup occurs.
- **Operational Telemetry**: Non-blocking endpoints for health checks (`/healthz`), readiness (`/readyz`), and Prometheus metrics (`/metrics`).

---

## Directory Layout

```text
├── include/infsched/      # Public C++ headers (types, topology, scheduler, etc.)
├── src/
│   ├── core/              # Resource manager, topology detection, cost model, batcher
│   ├── daemon/            # Supervisor, ops HTTP server, daemon main, infsched-ctl
│   ├── worker/            # Worker execution process & backend plugins (ONNX, GGUF, Mock)
│   └── sim/               # Trace replay simulator and benchmark validation
├── tests/                 # Unit, chaos, and integration test suites
├── bench/                 # Concurrent load benchmarks
├── configs/               # Example daemon YAML configuration
└── docker/                # Multi-stage Dockerfile and Docker Compose definition
```

---

## Building and Running

### Prerequisites

- Linux kernel 5.4+
- Modern C++20 compiler (`g++` 13+ or `clang++` 16+)
- CMake 3.20+ and Ninja

### Build Instructions

```bash
# Configure build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Compile all targets
cmake --build build --parallel
```

### Running Tests

```bash
ctest --test-dir build --output-on-failure
```

### Running the Trace Simulator

Simulates synthetic trace workloads comparing unmanaged execution versus managed `infsched` policies:

```bash
./build/src/sim/infsched-sim 2000
```

### Running Live Benchmarks

```bash
./build/bench/infsched-bench
```

---

## Running the Daemon & CLI

### Start the Daemon

```bash
./build/src/daemon/infsched-daemon --config configs/infsched.yaml --ops-port 9090
```

### Manage with `infsched-ctl`

Check service readiness and active isolation level:
```bash
./build/src/daemon/infsched-ctl ready
```

List currently loaded models and core allocations:
```bash
./build/src/daemon/infsched-ctl list
```

Dynamically load a model on-demand:
```bash
./build/src/daemon/infsched-ctl load vit-base --backend onnx --cores 1
```

Dynamically unload a model and return cores to the pool:
```bash
./build/src/daemon/infsched-ctl unload vit-base
```

Fetch Prometheus metrics:
```bash
./build/src/daemon/infsched-ctl metrics
```

---

## Configuration (`configs/infsched.yaml`)

```yaml
version: 1

server:
  grpc_port: 50051
  ops_port: 9090
  unix_socket: /run/infsched/infsched.sock

policy:
  max_batch_wait_ms: 2
  safety_margin_ms: 1
  max_queue_depth: 5000
  enable_overflow_borrowing: true

tenants:
  - name: default
    tokens_per_sec: 100.0
    burst_capacity: 200.0

models:
  - name: resnet50
    backend: onnx
    path: /models/resnet50.onnx
    min_physical_cores: 1
    max_physical_cores: 2
    max_batch_size: 16
    working_set_bytes_per_sample: 524288
```

---

## License

This project is licensed under the [MIT License](LICENSE).
