#include "renderer/Bloom.hpp"

#include "fg/FrameGraph.hpp"
#include "fg/Blackboard.hpp"

#include "FrameGraphData/SceneColor.hpp"
#include "FrameGraphData/BrightColor.hpp"

#include "tracy/Tracy.hpp"

namespace gfx {

Bloom::Bloom(rhi::RenderDevice &rd) : m_downsampler{rd}, m_upsampler{rd} {}

uint32_t Bloom::count(const PipelineGroups groups) const {
  return bool(groups & PipelineGroups::BuiltIn)
           ? m_downsampler.count() + m_upsampler.count()
           : 0;
}
void Bloom::clear(const PipelineGroups groups) {
  if (bool(groups & PipelineGroups::BuiltIn)) {
    m_downsampler.clear();
    m_upsampler.clear();
  }
}

void Bloom::resample(FrameGraph &fg, FrameGraphBlackboard &blackboard,
                     const float radius) {
  ZoneScopedN("Bloom::Resample");

  auto color = blackboard.get<SceneColorData>().HDR;

  constexpr auto kNumSteps = 6;
  for (auto i = 0; i < kNumSteps; ++i)
    color = m_downsampler.addPass(fg, color, i);
  for (auto i = 0; i < kNumSteps - 1; ++i)
    color = m_upsampler.addPass(fg, color, radius);

  blackboard.add<BrightColorData>(color);
}

} // namespace gfx
