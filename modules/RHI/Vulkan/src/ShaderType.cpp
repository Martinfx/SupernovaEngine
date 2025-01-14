#include "rhi/ShaderType.hpp"
#include <utility> // to_underlying
#include <bitset>
#include <cassert>

namespace rhi {

const char *toString(const ShaderType shaderType) {
#define CASE(Value)                                                            \
  case Value:                                                                  \
    return #Value

  switch (shaderType) {
    using enum ShaderType;

    CASE(Vertex);
    CASE(Geometry);
    CASE(Fragment);
    CASE(Compute);
  }
#undef CASE

  assert(false);
  return "Undefined";
}

ShaderStages getStage(const ShaderType shaderType) {
#define CASE(Value)                                                            \
  case ShaderType::Value:                                                      \
    return ShaderStages::Value

  switch (shaderType) {
    CASE(Vertex);
    CASE(Geometry);
    CASE(Fragment);
    CASE(Compute);
  }
#undef CASE

  assert(false);
  return ShaderStages{0};
}
uint8_t countStages(const ShaderStages flags) {
  return static_cast<uint8_t>(
    std::bitset<sizeof(ShaderStages) * 8>{std::to_underlying(flags)}.count());
}

} // namespace rhi
