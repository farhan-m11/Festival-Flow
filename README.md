# FestivalFlow — C Full-Stack Hackathon Project

FestivalFlow creates a task order while ensuring:
1. Every prerequisite appears before the task that depends on it.
2. Every department occupies one uninterrupted block.
3. Impossible plans return `IMPOSSIBLE`.

## Technology
- Backend: C (portable socket-based HTTP server)
- Frontend: HTML, CSS and JavaScript
- Algorithm: two-level topological sorting (department graph + per-department task graph)

## Requirements
- GCC or MinGW-w64
- Windows: MinGW-w64 GCC (or another GCC-compatible compiler)
- Linux/macOS: GCC or Clang

No external libraries are required.

## Run on Windows (MinGW)
Open Command Prompt in this folder:

```bat
gcc server.c -o festivalflow.exe -lws2_32
festivalflow.exe
```

Open http://localhost:8080

If your compiler uses a different socket library, use its normal Windows socket link option.

## Run on Linux/macOS

```bash
gcc server.c -o festivalflow
./festivalflow
```

Open http://localhost:8080

The server serves the `public` folder and accepts `POST /api/solve`.

## Use the application
1. Add each task with a unique task ID and a department.
2. Add dependencies as `Prerequisite → Dependent task`.
3. Click **Generate schedule**.
4. The app displays either a valid grouped order or `IMPOSSIBLE`.

## API

`POST /api/solve`

Example request:

```json
{
  "tasks": [
    {"id":"S1","department":"Sound"},
    {"id":"S2","department":"Sound"},
    {"id":"L1","department":"Lights"},
    {"id":"L2","department":"Lights"},
    {"id":"P","department":"Prep"}
  ],
  "dependencies": [
    {"from":"P","to":"S1"},
    {"from":"S1","to":"S2"},
    {"from":"S2","to":"L1"},
    {"from":"L1","to":"L2"}
  ]
}
```

Example successful response:

```json
{
  "status":"VALID",
  "order":[
    {"task":"P","department":"Prep"},
    {"task":"S1","department":"Sound"},
    {"task":"S2","department":"Sound"},
    {"task":"L1","department":"Lights"},
    {"task":"L2","department":"Lights"}
  ],
  "message":"A valid grouped schedule was found."
}
```

Impossible response:

```json
{
  "status":"IMPOSSIBLE",
  "order":[],
  "message":"No schedule satisfies both dependency and department grouping rules."
}
```

## Algorithm and correctness
For every cross-department dependency `u → v`, add an edge from `department(u)` to `department(v)` in a department graph. A department must be entirely before another department if one of its tasks is a prerequisite of a task in that other department; otherwise the two departments would have to be split around one another.

Topologically sort this department graph. If it contains a cycle, no contiguous department-block ordering can satisfy all dependencies.

For each department, separately topologically sort its tasks using only dependencies whose endpoints are both in that department. If an internal task cycle exists, no valid task order exists.

Concatenate the internally sorted task lists in department topological order. Every cross-department dependency is satisfied by the department order, every internal dependency is satisfied by its local task order, and each department appears as exactly one contiguous block. Therefore the produced order is valid.

The implementation uses adjacency matrices. With at most 300 tasks, 15 departments and 600 dependencies, this is comfortably within the stated limits.

## Complexity
Let `T` be tasks, `D` departments and `E` dependencies:
- Department topological sort: `O(D² + E)`
- Internal task sorts combined: `O(T² + E)`
- Memory: `O(T² + D²)`

## Included tests
Run the C regression suite from the project root:

```bash
gcc tests/test_scheduler.c -o tests/test_scheduler
./tests/test_scheduler
```

On Windows with MinGW, build with `gcc tests/test_scheduler.c -o tests/test_scheduler.exe -lws2_32` and run `tests\\test_scheduler.exe`. The suite checks a valid plan with internal and cross-department dependencies, a task cycle, a department-level conflict, and independent one-task departments. Valid schedules are also checked for prerequisite ordering and uninterrupted department blocks. You can use the browser Test Centre for a live demonstration.

1. Supplied example — VALID
2. Dependency cycle inside one department — IMPOSSIBLE
3. Department-level cycle — IMPOSSIBLE
4. Independent one-task departments — VALID
5. No prerequisites — VALID
6. Within-department chain — VALID

## Important scope notes
- Every task must have exactly one department.
- Task IDs must be unique.
- Dependencies must reference existing tasks.
- Self-dependencies are rejected as impossible.
- Duplicate dependencies are safely ignored.
- No dates, durations or resource-capacity optimization are included, as they are outside the problem scope.
