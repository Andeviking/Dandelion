// Toolchain acceptance material, independent of the solver architecture.
export module dandelion.toolchain;

import std;

export namespace dandelion::toolchain {

int SumValues(std::span<const int> values);

}  // namespace dandelion::toolchain
