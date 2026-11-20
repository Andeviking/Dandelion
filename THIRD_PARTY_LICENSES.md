# Third-party open-source licenses

Dandelion's own code is licensed under the [MIT License](LICENSE). The
components below are covered by their respective upstream licenses. The
versions and commits identify the project-managed dependencies and the pinned
cvc5 reference. For dependencies supplied by the system, a parent project, or a
local source tree, use the notices for the versions actually redistributed.

| Component | Version and pin | Use | License and archived texts |
| --- | --- | --- | --- |
| GMP | 6.3.0 source archive | Arithmetic library | `LGPL-3.0-or-later OR GPL-2.0-or-later`. [LGPLv3](LICENSES/gmp/COPYING.LESSERv3), [GPLv2](LICENSES/gmp/COPYINGv2), [GPLv3](LICENSES/gmp/COPYINGv3) |
| GoogleTest | 1.18.0, `063de7e9578f82b369302001269680b4b1553359` | Tests | [`BSD-3-Clause`](LICENSES/googletest/LICENSE) |
| Google Benchmark | 1.10.0, `a6b4a9f00b36c19af9997d68a1cf6e0b5b3d032c` | Benchmarks | [`Apache-2.0`](LICENSES/google-benchmark/LICENSE) |
| cvc5 | 1.4.2, `282d5e0c84baf49c780704c532f87ba66d8b513d` | Reference solver and benchmark data collection | [`BSD-3-Clause`](LICENSES/cvc5/COPYING) for cvc5 itself |

## GMP distribution

Dandelion uses GMP. Dandelion selects LGPLv3 for GMP and its use in the
combined work because LGPLv3 §4 permits distribution with Dandelion under its
MIT terms when the conditions below are met. The GMP distribution also includes
separate programs, such as its demos and test suite, under GPLv3 or later.
[GMP copying conditions](https://gmplib.org/manual/Copying)

When redistributing a combined work under LGPLv3 §4, give prominent notice
with each copy that it uses GMP and that LGPLv3 covers GMP and its use. Include
copies of both LGPLv3 and GPLv3. If the combined work displays copyright
notices at runtime, include GMP's copyright notice and a reference to both
license texts. The distribution terms must permit modification of the GMP
portions and reverse engineering to debug those modifications.
[LGPLv3 §4](https://www.gnu.org/licenses/lgpl-3.0-standalone.html)

- **Dynamic linking (§4(d)(1)).** Use a shared-library mechanism that loads a
  copy of GMP already present on the user's system and works with a modified,
  interface-compatible GMP. If the distribution includes a GMP shared-library
  binary, also satisfy the source distribution requirements for that binary,
  including any GMP changes.
- **Static linking (§4(d)(0)).** Provide the Minimal Corresponding Source under
  LGPLv3, including the source for the linked GMP and any changes, and provide
  the Corresponding Application Code in source or object form with the
  materials and permissions needed to relink Dandelion with a modified GMP.
  Convey these materials as specified by GPLv3 §6. Dandelion's CMake GMP source
  build produces the static library `libgmp.a`.

For either route, provide the information needed to install and run the
modified combined work when LGPLv3 §4(e) and GPLv3 §6 require it.
[LGPLv3 §4](https://www.gnu.org/licenses/lgpl-3.0-standalone.html),
[GPLv3 §6](LICENSES/gmp/COPYINGv3),
[GMP build configuration](cmake/DandelionGMP.cmake)

## cvc5 reference patches

- [cvc5-282d5e0c.patch](benchmarks/reference/cvc5-282d5e0c.patch) modifies the
  pinned cvc5 source to capture bounded expression traces for benchmarks.
- [cvc5-282d5e0c-snapshot.patch](benchmarks/reference/cvc5-282d5e0c-snapshot.patch)
  adds a standalone adapter that uses the pinned cvc5 public API to export
  typed Boolean and bit-vector assertion graphs.

The cvc5 copying notice identifies separately licensed components and optional
GPL dependencies. For cvc5 binaries, the applicable notices and redistribution
terms depend on the libraries included in the build. [cvc5 COPYING](LICENSES/cvc5/COPYING)

## Build, CI, and reference tools

CMake and Ninja configure and build the project. GCC with libstdc++ and
LLVM/Clang are toolchain components. Python supports scripts and benchmark
workflows, and Doxygen generates documentation. Z3, Bitwuzla, and Yices are
external solvers used as research or comparison references. These tools have
their own license terms. Use the applicable upstream notices for any tool or
toolchain runtime redistributed with Dandelion.
