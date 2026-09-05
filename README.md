# Communication Planner

**Communication Planner is an open-source, vendor-neutral C++20 runtime for planning topology-aware point-to-point, staged, multicast, and collective communication across accelerators, CPUs, NICs, storage, and distributed AI infrastructure using bandwidth, congestion, locality, capability, and failure-domain constraints.**

It answers one systems question:

**How should this data move from source to destination now, through which sequence of links and stages, under which current topology, capacity, congestion, compatibility, and authority constraints, and which communication plan remains valid as infrastructure state changes?**

Communication planning is not shortest-path routing. A nominally short path may be invalid or expensive because of stale topology, incompatible endpoints, insufficient bandwidth, congestion, reservation conflicts, unavailable staging memory, PCIe-root constraints, NUMA locality, device capability mismatch, storage/network bottlenecks, collective participation constraints, link asymmetry, failure domains, required host staging, process-local endpoint loss, state residency, security/policy restrictions, and topology generation changes between planning and execution. Communication Planner turns those constraints into an explicit, inspectable, generation-bound plan.

## Systems boundary

- [Fabric Scheduler](https://github.com/pngen) chooses workload placement.
- [Congestion Fabric](https://github.com/pngen) measures and governs live congestion.
- [Capacity Fabric](https://github.com/pngen) models usable and future capacity.
- [Reservation Fabric](https://github.com/pngen) governs future commitments.
- [Resource Broker](https://github.com/pngen) owns scarce-resource and staging claims.
- topology/NUMA/PCIe/fleet runtimes provide physical topology.
- [Runtime Registry](https://github.com/pngen) provides live endpoint/service discovery.
- [Collective Fabric](https://github.com/pngen) executes collective semantics.
- [Collective Scheduler](https://github.com/pngen) decides when competing collectives run.
- Communication Planner constructs and ranks feasible communication plans and governs their authority, revalidation, and supersession.

Communication Planner owns communication-request identity, endpoint identity, source/destination generation binding, path/stage feasibility, hard path constraints, point-to-point and staged planning, relay and multicast planning, collective communication-shape planning, topology-aware route construction, locality-aware staging, path bandwidth/capacity evaluation, congestion- and reservation-aware path feasibility, failure-domain-aware path construction, deterministic plan ranking, fallback plans, plan revalidation, plan supersession, plan explanations, plan persistence/history, and bounded execution intent for lower-level movement runtimes.

The defining thesis:

**Communication planning is not shortest-path routing. It is selecting a generation-valid sequence of endpoints, links, staging resources, and transport semantics whose topology, capability, capacity, congestion, locality, reservation, and failure-domain constraints make the movement safe and worthwhile now.**

## Core doctrine

A path existing does not make it valid. A valid path does not make it optimal. A communication plan is not completed movement. A transfer request is not a route. A low-hop route is not necessarily low-cost. Advertised link bandwidth is not guaranteed service bandwidth. A stale path generation must not carry fresh traffic. A recovered plan must not silently retain dynamic link evidence. A collective participant set is not a collective schedule. Planning is therefore **constraint-correct first, deterministic second, cost-aware third, authority-bound always.**

- Hard feasibility filtering runs before ranking; a hard-invalid path never survives because of a good score.
- Only feasible plans are ranked, with explicit, named factors and per-factor provenance (MEASURED / REPORTED / DERIVED / ESTIMATED / FORECAST / SYNTHETIC / UNKNOWN). UNKNOWN never ranks favorable by default.
- Ranking is fully deterministic with stable tie-breaking (fewer unknowns, stronger evidence, fewer hops, lower congestion, higher residual bandwidth, stable strong-ID ordering) independent of insertion/iteration order.
- A plan is not resource ownership; it records the commit it requires. Resource commitment is all-or-nothing through a narrow Resource Broker interface.
- Revalidation is mandatory before execution handoff and fallback activation; a recovered executable plan becomes REVALIDATION_REQUIRED unless all current evidence is proven fresh.
- A newer request/plan supersedes older unresolved generations for the same scope; old plans cannot commit staging, execute, publish success, or become fallback-active.
- Persistence uses a versioned binary format with strong integrity checks and rejects bad magic, unsupported versions, truncation, checksum mismatch, malformed/oversized lengths, invalid enums, duplicate IDs, generation regression, impossible lifecycle, payload mismatch, NaN/Inf cost, and trailing garbage.

## Building

Requires CMake (>= 3.20) and a C++20 compiler. On MSVC the library, tests, examples, and tools build with /W4 /WX.

    cmake -S . -B build -G "Visual Studio 17 2022"
    cmake --build build --config Release
    ctest --test-dir build -C Release --output-on-failure

Debug is supported as well. No test uses a timeout; a hanging test is treated as a lifecycle defect.

The CUDA proofs are built directly with nvcc (the Visual Studio generator has no registered CUDA MSBuild toolset on this host), via the documented script:

    scripts\build_cuda.bat

This produces build\cuda\cp_cuda_worker.exe and build\cuda\cp_cuda_plan_gated_proof.exe. The `cuda_worker_death` CTest runs the integrated worker-death proof against a live CUDA worker when that executable is present, and is otherwise SKIPPED (return code 77) with a clear message — never a false pass.

## Proof of the runtime

The repository exercises the real runtime:

- **Real multiprocess proof** (independent OS processes + framed checksummed TCP): a Communication Planner coordinator, Worker A, and Worker B register and publish real endpoint/link evidence; a direct path is initially ranked first; congestion generation advance makes the plan REVALIDATION_REQUIRED and a staged fallback wins; killing Worker A fences its WorkerBootId so its endpoint/link evidence becomes stale and no path remains until Worker A' restarts with a fresh boot; a coordinator restart recovers durable history conservatively and marks every recovered executable plan REVALIDATION_REQUIRED.
- **Real CUDA proof** on an NVIDIA RTX 5090 (sm_120): discovers the device over CUDA, publishes real host/GPU capability evidence, plans a host<->device path, commits device and pinned host staging, executes real cudaMalloc / H2D / kernel / D2H / synchronize with CPU parity, and verifies device memory returns to baseline. Stale endpoint/link generation rejects an old plan before any allocation. A hard reservation conflict forces a staged host path even when a direct path is nominally cheaper.
- **Real CUDA worker-death communication proof** (integrated): a coordinator OS process, a real CUDA Worker A OS process (fresh WorkerBootId) that discovers the RTX 5090 and publishes real host/GPU evidence, a plan-gated handoff where Worker A performs real cudaMalloc / pinned host / H2D / kernel / D2H with CPU parity and returns device memory to measured baseline; then Worker A is terminated as a real OS process, its old WorkerBootId and endpoint/link generations are fenced, the old plan becomes non-executable, stale endpoint/link/result replay is rejected, and a reincarnated Worker A' (fresh PID, fresh WorkerBootId) rediscover the RTX 5090, republishes, produces a fresh CommunicationPlanGeneration, and re-executes real CUDA movement/cleanup. The proof carries CoordinatorEpoch, WorkerId, WorkerBootId, EndpointGeneration, LinkGeneration, CommunicationPlanGeneration, and SourceBoot/Authority authority.
- **Real host-memory, storage-staging, and loopback-TCP proofs** measure real completed copies, file staging, and framed loopback transfer bytes/latency (labelled REAL / MEASURED).
- **Deterministic SYNTHETIC scenarios** (labelled SYNTHETIC) cover two-GPU direct-vs-staged, four-GPU collective shape, multicast relay tree, congested-direct alternate route, reservation-blocked preferred path, and failure-domain-aware routing.

No physical multi-GPU, NVLink/NVSwitch, RDMA, GPUDirect, or multi-node network is fabricated; those are represented only as clearly labelled SYNTHETIC evidence. On this host only a single RTX 5090 is present.

## Layout

- include/communication_planner — public headers (identities/generations, model, feasibility, ranking, revalidation, lifecycle, persistence, protocol, planner, coordinator, adapters, multicast, collective).
- src — implementation (planner, feasibility, cost, ranking, revalidation, lifecycle, persistence, protocol, coordinator, multicast, collective).
- tests — unit, property, concurrency, live, and adversarial suites.
- apps — coordinator, worker, and the multiprocess proof executables.
- cuda — the plan-gated CUDA proof (built with nvcc when the CUDA toolset is available).
- examples, benchmarks, tools — runnable examples, a scale benchmark, and a CLI inspection tool.
- downstream — an independent consumer using find_package(CommunicationPlanner CONFIG REQUIRED).

## Install / package

    cmake -S . -B build -G "Visual Studio 17 2022"
    cmake --install build --config Release --prefix <install-prefix>

This installs the library, headers, exported target CommunicationPlanner::CommunicationPlanner, and package config/version files. The downstream consumer in downstream/ is validated against the installed package, not the source tree.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
