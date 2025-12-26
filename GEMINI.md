# Urbis - Project Context

**Urbis** is a high-performance spatial indexing system designed for urban environments, implemented in C. It focuses on organizing spatial data efficiently using Quadtrees and "Urban Block" concepts.

## 📂 Project Structure

The project follows a standard C library layout:

- **`src/`**: Contains the source code for the core library.
    - `quadtree.c`: **Core Indexing Engine**. Implements a pointer-based Quadtree for spatial partitioning. Handles recursive insertion, node splitting, and range/point queries.
    - `geometry.c`: **Geometric Operations**. Implements primitives (Points, LineStrings, Polygons) and algorithms for distance, area, centroids, and MBR (Minimum Bounding Rectangle) calculations.
    - `page.c`: *Storage Management*. (Likely) Handles memory pagination or disk serialization logic.
    - `kdtree.c`: *Secondary Index*. Alternative spatial index, possibly for nearest-neighbor optimization.
- **`include/`**: Public API headers.
    - `quadtree.h`: Defines the `QuadTree` struct, `QTNode`, `QTItem`, and error codes (`QTError`).
    - `geometry.h`: Defines geometric types (`Point`, `MBR`, `Polygon`, `SpatialObject`) and manipulation functions.
    - `page.h`, `kdtree.h`: Corresponding headers for other modules.
- **`examples/`**: (Empty) Placeholder for usage examples.
- **`tests/`**: (Empty) Placeholder for unit tests.

## 🛠 Building and Running

**Status:** No build system (`Makefile`, `CMakeLists.txt`) currently exists.

### Compilation
To compile code using this library, you must link the source files directly.
**Standard compilation command:**
```bash
gcc -std=c99 -I./include src/*.c -lm -o output_binary
```

*   `-I./include`: Adds the header directory to the include path.
*   `-lm`: Links the math library (required for `geometry.c` operations like `sqrt`).

## 💻 Development Conventions

### Language Standards
- **Language:** C99
- **Dependencies:** Standard C Library (`stdlib.h`, `string.h`, `math.h`, `stdint.h`, `stdbool.h`).

### Coding Style
- **Naming:** `snake_case` for all functions and variables.
- **Namespacing:** Functions are prefixed with their module name (e.g., `quadtree_insert`, `mbr_intersects`, `point_create`).
- **Memory Management:**
    - Explicit `_create` functions allocate memory (e.g., `quadtree_create`).
    - Explicit `_free` or `_destroy` functions release memory (e.g., `quadtree_destroy`).
    - **Ownership:** The library generally assumes ownership of structs it creates, but user `data` pointers (void*) are managed by the user.
- **Error Handling:**
    - Functions returning `int` typically return `0` (or `enum_OK`) on success and negative values for errors.
    - Error enums are defined in headers (e.g., `QTError`, `GeomError`).

### Key Data Structures
- **MBR (Minimum Bounding Rectangle):** The fundamental unit for spatial queries. `{min_x, min_y, max_x, max_y}`.
- **QuadTree:** The main index. Configurable `node_capacity` (default 8) and `max_depth` (default 20).
- **SpatialObject:** A wrapper struct capable of holding Points, LineStrings, or Polygons along with an ID and custom properties.

## 📝 Immediate Tasks & Roadmap
1.  **Build System:** Create a `Makefile` to automate compilation of the static library (`liburbis.a`) and tests.
2.  **Testing:** Populate `tests/` with unit tests using a framework like Unity or custom assertions to verify `quadtree` and `geometry` logic.
3.  **Persistence:** Implement the disk-based storage logic hinted at in `page.c`.
4.  **Bindings:** Develop the Node.js/Python bindings mentioned in the high-level goals.