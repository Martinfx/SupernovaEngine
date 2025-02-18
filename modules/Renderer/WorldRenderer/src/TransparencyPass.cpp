#include "renderer/TransparencyPass.hpp"
#include "rhi/RenderDevice.hpp"

#include "renderer/ViewInfo.hpp"
#include "renderer/VertexFormat.hpp"
#include "renderer/MeshInstance.hpp"

#include "ForwardPassInfo.hpp"
#include "LightingSettings.hpp"

#include "FrameGraphData/UserData.hpp"

#include "FrameGraphResourceAccess.hpp"
#include "FrameGraphCommon.hpp"
#include "FrameGraphForwardPass.hpp"
#include "renderer/FrameGraphTexture.hpp"
#include "UploadInstances.hpp"

#include "MaterialShader.hpp"
#include "BatchBuilder.hpp"

#include "RenderContext.hpp"
#include "ShaderCodeBuilder.hpp"

namespace gfx {

namespace {

[[nodiscard]] auto canDraw(const Renderable *renderable) {
  assert(renderable);

  const auto &surface =
    *renderable->subMeshInstance.material->getBlueprint().surface;
  switch (surface.blendMode) {
    using enum BlendMode;

  case Transparent:
  case Add:
  case Modulate:
    return true;

  default:
    return false;
  }
};

[[nodiscard]] auto batchCompatible(const Batch &b, const Renderable &r) {
  return sameGeometry(b, r) && sameMaterial(b, r) && sameTextures(b, r);
}

[[nodiscard]] rhi::BlendState getBlendState(BlendMode blendMode) {
  switch (blendMode) {
  case BlendMode::Transparent:
    return {
      .enabled = true,
      .srcColor = rhi::BlendFactor::SrcAlpha,
      .dstColor = rhi::BlendFactor::OneMinusSrcAlpha,
      .colorOp = rhi::BlendOp::Add,
      .srcAlpha = rhi::BlendFactor::One,
      .dstAlpha = rhi::BlendFactor::Zero,
      .alphaOp = rhi::BlendOp::Add,
    };
  case BlendMode::Add:
    return {
      .enabled = true,
      .srcColor = rhi::BlendFactor::One,
      .dstColor = rhi::BlendFactor::One,
      .colorOp = rhi::BlendOp::Add,
      .srcAlpha = rhi::BlendFactor::One,
      .dstAlpha = rhi::BlendFactor::One,
      .alphaOp = rhi::BlendOp::Add,
    };

  case BlendMode::Modulate:
    return {
      .enabled = true,
      .srcColor = rhi::BlendFactor::One,
      .dstColor = rhi::BlendFactor::Zero,
      .colorOp = rhi::BlendOp::Add,
      .srcAlpha = rhi::BlendFactor::One,
      .dstAlpha = rhi::BlendFactor::Zero,
      .alphaOp = rhi::BlendOp::Add,
    };

  default:
    return {.enabled = false};
  }
}

} // namespace

//
// TransparencyPass class:
//

TransparencyPass::TransparencyPass(rhi::RenderDevice &rd)
    : rhi::RenderPass<TransparencyPass>{rd} {}

uint32_t TransparencyPass::count(const PipelineGroups flags) const {
  return bool(flags & PipelineGroups::SurfaceMaterial) ? BasePass::count() : 0;
}
void TransparencyPass::clear(const PipelineGroups flags) {
  if (bool(flags & PipelineGroups::SurfaceMaterial)) BasePass::clear();
}

std::optional<FrameGraphResource> TransparencyPass::addGeometryPass(
  FrameGraph &fg, FrameGraphBlackboard &blackboard,
  const FrameGraphResource sceneColor, const ViewInfo &viewData,
  const PropertyGroupOffsets &propertyGroupOffsets,
  const LightingSettings &lightingSettings, const bool softShadows) {
  static constexpr auto kPassName = "ForwardTransparency";
  ZoneScopedN(kPassName);

  std::vector<const Renderable *> transparentRenderables;
  transparentRenderables.reserve(viewData.visibleRenderables.size());
  std::ranges::copy_if(viewData.visibleRenderables,
                       std::back_inserter(transparentRenderables), canDraw);
  if (transparentRenderables.empty()) return std::nullopt;

  sortByMaterial(transparentRenderables);

  std::vector<GPUInstance> gpuInstances;
  auto batches = buildBatches(gpuInstances, transparentRenderables,
                              propertyGroupOffsets, batchCompatible);
  if (batches.empty()) return std::nullopt;

  const auto instances = *uploadInstances(fg, std::move(gpuInstances));

  LightingPassFeatures features{.softShadows = softShadows};
  getLightingPassFeatures(features, blackboard);

  struct Data {
    FrameGraphResource output;
  };
  const auto [output] = fg.addCallbackPass<Data>(
    kPassName,
    [&fg, &blackboard, sceneColor, instances](FrameGraph::Builder &builder,
                                              Data &data) {
      PASS_SETUP_ZONE;

      read(builder, blackboard, instances);

      const auto &sceneColorDesc =
        fg.getDescriptor<FrameGraphTexture>(sceneColor);
      data.output = builder.create<FrameGraphTexture>(
        "SceneColor w/ Transparency", sceneColorDesc);
      data.output =
        builder.write(data.output, Attachment{
                                     .index = 0,
                                     .imageAspect = rhi::ImageAspect::Color,
                                     .clearValue = ClearValue::TransparentBlack,
                                   });

      if (auto *d = blackboard.try_get<UserData>(); d) {
        d->target =
          builder.write(d->target, Attachment{
                                     .index = 1,
                                     .imageAspect = rhi::ImageAspect::Color,
                                   });
      }
    },
    [this, writeUserData = blackboard.has<UserData>(), lightingSettings,
     features, batches = std::move(batches)](
      const Data &, const FrameGraphPassResources &, void *ctx) {
      auto &rc = *static_cast<RenderContext *>(ctx);
      auto &[cb, commonSamplers, framebufferInfo, sets] = rc;
      RHI_GPU_ZONE(cb, kPassName);

      auto &samplerBindings = sets[0];
      samplerBindings[4] =
        rhi::bindings::SeparateSampler{commonSamplers.shadow};
      samplerBindings[5] =
        rhi::bindings::SeparateSampler{commonSamplers.omniShadow};

      overrideSampler(sets[1][5], commonSamplers.bilinear);
      overrideSampler(sets[1][11], commonSamplers.bilinear);

      const BaseGeometryPassInfo passInfo{
        .depthFormat = rhi::getDepthFormat(*framebufferInfo),
        .colorFormats = rhi::getColorFormats(*framebufferInfo),
        .writeUserData = writeUserData,
      };

      cb.beginRendering(*framebufferInfo);
      for (const auto &batch : batches) {
        const auto &pipeline =
          _getPipeline(ForwardPassInfo{adjust(passInfo, batch), features});
        if (pipeline) {
          bindBatch(rc, batch);
          cb.bindPipeline(*pipeline);
          bindDescriptorSets(rc, *pipeline);
          cb.pushConstants(rhi::ShaderStages::Fragment, 16, &lightingSettings);
          drawBatch(rc, batch);
        }
      }
      endRendering(rc);
    });

  return output;
}

CodePair TransparencyPass::buildShaderCode(const rhi::RenderDevice &rd,
                                           const VertexFormat *vertexFormat,
                                           const Material &material,
                                           const LightingPassFeatures &features,
                                           const bool writeUserData) {
  const auto offsetAlignment =
    rd.getDeviceLimits().minStorageBufferOffsetAlignment;

  CodePair code;

  const auto commonDefines = buildDefines(*vertexFormat);

  ShaderCodeBuilder shaderCodeBuilder;

  // -- VertexShader:

  shaderCodeBuilder.setDefines(commonDefines);
  addMaterial(shaderCodeBuilder, material, rhi::ShaderType::Vertex,
              offsetAlignment);
  code.vert = shaderCodeBuilder.buildFromFile("Mesh.vert");

  // -- FragmentShader:

  shaderCodeBuilder.setDefines(commonDefines)
    .addDefine("HAS_SCENE_DEPTH", 1)
    .addDefine("HAS_SCENE_COLOR", 1)
    .addDefine<int32_t>("WRITE_USERDATA", writeUserData);
  addMaterial(shaderCodeBuilder, material, rhi::ShaderType::Fragment,
              offsetAlignment);
  addLighting(shaderCodeBuilder, features);
  code.frag = shaderCodeBuilder.buildFromFile("ForwardPass.frag");

  return code;
}

//
// (private):
//

rhi::GraphicsPipeline
TransparencyPass::_createPipeline(const ForwardPassInfo &passInfo) const {
  assert(passInfo.vertexFormat && passInfo.material);

  auto &rd = getRenderDevice();

  const auto &material = *passInfo.material;
  const auto [vertCode, fragCode] =
    buildShaderCode(rd, passInfo.vertexFormat, material, passInfo.features,
                    passInfo.writeUserData);

  const auto &surface = getSurface(material);

  rhi::GraphicsPipeline::Builder builder;
  builder.setDepthFormat(passInfo.depthFormat)
    .setColorFormats(passInfo.colorFormats)
    .setInputAssembly(passInfo.vertexFormat->getAttributes())
    .setTopology(passInfo.topology)
    .addShader(rhi::ShaderType::Vertex, vertCode)
    .addShader(rhi::ShaderType::Fragment, fragCode)

    .setDepthStencil({
      .depthTest = true,
      .depthWrite = false,
      .depthCompareOp = rhi::CompareOp::LessOrEqual,
    })
    .setRasterizer({
      .polygonMode = rhi::PolygonMode::Fill,
      .cullMode = surface.cullMode,
    })
    .setBlending(0, getBlendState(surface.blendMode));
  if (passInfo.writeUserData) {
    builder.setBlending(1, {.enabled = false});
  }
  return builder.build(rd);
}

} // namespace gfx
