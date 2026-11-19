module dandelion.toolchain;

import std;

namespace dandelion::toolchain {

int SumValues(std::span<const int> values) {
  return std::accumulate(values.begin(), values.end(), 0);
}

}  // namespace dandelion::toolchain
