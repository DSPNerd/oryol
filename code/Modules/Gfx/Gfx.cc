//------------------------------------------------------------------------------
//  Gfx.cc
//------------------------------------------------------------------------------
#include "Pre.h"
#include "Gfx.h"
#include "Core/Core.h"
#include "Gfx/Core/gfxPointers.h"

namespace Oryol {

using namespace _priv;

Gfx::_state* Gfx::state = nullptr;

//------------------------------------------------------------------------------
void
Gfx::Setup(const class GfxSetup& setup) {
    o_assert_dbg(!IsValid());
    state = Memory::New<_state>();
    state->gfxSetup = setup;

    gfxPointers pointers;
    pointers.displayMgr = &state->displayManager;
    pointers.renderer = &state->renderer;
    pointers.resContainer = &state->resourceContainer;
    pointers.meshPool = &state->resourceContainer.meshPool;
    pointers.shaderPool = &state->resourceContainer.shaderPool;
    pointers.texturePool = &state->resourceContainer.texturePool;
    pointers.pipelinePool = &state->resourceContainer.pipelinePool;
    pointers.renderPassPool = &state->resourceContainer.renderPassPool;
    
    state->displayManager.SetupDisplay(setup, pointers);
    state->renderer.setup(setup, pointers);
    state->resourceContainer.setup(setup, pointers);
    state->runLoopId = Core::PreRunLoop()->Add([] {
        state->displayManager.ProcessSystemEvents();
    });
    state->gfxFrameInfo = GfxFrameInfo();
}

//------------------------------------------------------------------------------
void
Gfx::Discard() {
    o_assert_dbg(IsValid());
    o_assert_dbg(!state->inPass);
    state->resourceContainer.Destroy(ResourceLabel::All);
    Core::PreRunLoop()->Remove(state->runLoopId);
    state->renderer.discard();
    state->resourceContainer.discard();
    state->displayManager.DiscardDisplay();
    Memory::Delete(state);
    state = nullptr;
}

//------------------------------------------------------------------------------
bool
Gfx::IsValid() {
    return nullptr != state;
}

//------------------------------------------------------------------------------
bool
Gfx::QuitRequested() {
    o_assert_dbg(IsValid());
    return state->displayManager.QuitRequested();
}

//------------------------------------------------------------------------------
Gfx::EventHandlerId
Gfx::Subscribe(EventHandler handler) {
    o_assert_dbg(IsValid());
    return state->displayManager.Subscribe(handler);
}

//------------------------------------------------------------------------------
void
Gfx::Unsubscribe(EventHandlerId id) {
    o_assert_dbg(IsValid());
    state->displayManager.Unsubscribe(id);
}

//------------------------------------------------------------------------------
const GfxSetup&
Gfx::GfxSetup() {
    o_assert_dbg(IsValid());
    return state->gfxSetup;
}

//------------------------------------------------------------------------------
const DisplayAttrs&
Gfx::DisplayAttrs() {
    o_assert_dbg(IsValid());
    return state->displayManager.GetDisplayAttrs();
}

//------------------------------------------------------------------------------
const DisplayAttrs&
Gfx::RenderPassAttrs() {
    o_assert_dbg(IsValid());
    return state->renderer.renderPassAttrs();
}

//------------------------------------------------------------------------------
const GfxFrameInfo&
Gfx::FrameInfo() {
    o_assert_dbg(IsValid());
    return state->gfxFrameInfo;
}

//------------------------------------------------------------------------------
void
Gfx::BeginPass() {
    o_assert_dbg(IsValid());
    o_assert_dbg(!state->inPass);
    state->inPass = true;
    state->gfxFrameInfo.NumPasses++;
    state->renderer.beginPass(nullptr, nullptr);
}

//------------------------------------------------------------------------------
void
Gfx::BeginPass(const PassState& passState) {
    o_assert_dbg(IsValid());
    o_assert_dbg(!state->inPass);
    state->inPass = true;
    state->gfxFrameInfo.NumPasses++;
    state->renderer.beginPass(nullptr, &passState);
}

//------------------------------------------------------------------------------
void
Gfx::BeginPass(const Id& id) {
    o_assert_dbg(IsValid());
    o_assert_dbg(!state->inPass);
    state->inPass = true;
    state->gfxFrameInfo.NumPasses++;
    renderPass* pass = state->resourceContainer.lookupRenderPass(id);
    o_assert_dbg(pass);
    state->renderer.beginPass(pass, nullptr);
}

//------------------------------------------------------------------------------
void
Gfx::BeginPass(const Id& id, const PassState& passState) {
    o_assert_dbg(IsValid());
    o_assert_dbg(!state->inPass);
    state->inPass = true;
    state->gfxFrameInfo.NumPasses++;
    renderPass* pass = state->resourceContainer.lookupRenderPass(id);
    o_assert_dbg(pass);
    state->renderer.beginPass(pass, &passState);
}

//------------------------------------------------------------------------------
void
Gfx::EndPass() {
    o_assert_dbg(IsValid());
    o_assert_dbg(state->inPass);
    state->inPass = false;
    state->renderer.endPass();
}

//------------------------------------------------------------------------------
void
Gfx::ApplyDrawState(const DrawState& drawState) {
    o_trace_scoped(Gfx_ApplyDrawState);
    o_assert_dbg(IsValid());
    o_assert_dbg(state->inPass);
    o_assert_dbg(drawState.Pipeline.Type == GfxResourceType::Pipeline);
    state->gfxFrameInfo.NumApplyDrawState++;

    // apply pipeline and meshes
    pipeline* pip = state->resourceContainer.lookupPipeline(drawState.Pipeline);
    o_assert_dbg(pip);
    mesh* meshes[GfxConfig::MaxNumInputMeshes] = { };
    int numMeshes = 0;
    for (; numMeshes < GfxConfig::MaxNumInputMeshes; numMeshes++) {
        if (drawState.Mesh[numMeshes].IsValid()) {
            meshes[numMeshes] = state->resourceContainer.lookupMesh(drawState.Mesh[numMeshes]);
        }
        else {
            break;
        }
    }
    #if ORYOL_DEBUG
    validateMeshes(pip, meshes, numMeshes);
    #endif
    state->renderer.applyDrawState(pip, meshes, numMeshes);

    // apply vertex textures if any
    texture* vsTextures[GfxConfig::MaxNumVertexTextures] = { };
    int numVSTextures = 0;
    for (; numVSTextures < GfxConfig::MaxNumVertexTextures; numVSTextures++) {
        const Id& texId = drawState.VSTexture[numVSTextures];
        if (texId.IsValid()) {
            vsTextures[numVSTextures] = state->resourceContainer.lookupTexture(texId);
        }
        else {
            break;
        }
    }
    if (numVSTextures > 0) {
        #if ORYOL_DEBUG
        validateTextures(ShaderStage::VS, pip, vsTextures, numVSTextures);
        #endif
        state->renderer.applyTextures(ShaderStage::VS, vsTextures, numVSTextures);
    }

    // apply fragment textures if any
    texture* fsTextures[GfxConfig::MaxNumFragmentTextures] = { };
    int numFSTextures = 0;
    for (; numFSTextures < GfxConfig::MaxNumFragmentTextures; numFSTextures++) {
        const Id& texId = drawState.FSTexture[numFSTextures];
        if (texId.IsValid()) {
            fsTextures[numFSTextures] = state->resourceContainer.lookupTexture(texId);
        }
        else {
            break;
        }
    }
    if (numFSTextures > 0) {
        #if ORYOL_DEBUG
        validateTextures(ShaderStage::FS, pip, fsTextures, numFSTextures);
        #endif
        state->renderer.applyTextures(ShaderStage::FS, fsTextures, numFSTextures);
    }
}

//------------------------------------------------------------------------------
bool
Gfx::QueryFeature(GfxFeature::Code feat) {
    o_assert_dbg(IsValid());
    return state->renderer.queryFeature(feat);
}

//------------------------------------------------------------------------------
ResourceLabel
Gfx::PushResourceLabel() {
    o_assert_dbg(IsValid());
    return state->resourceContainer.PushLabel();
}

//------------------------------------------------------------------------------
void
Gfx::PushResourceLabel(ResourceLabel label) {
    o_assert_dbg(IsValid());
    state->resourceContainer.PushLabel(label);
}

//------------------------------------------------------------------------------
ResourceLabel
Gfx::PopResourceLabel() {
    o_assert_dbg(IsValid());
    return state->resourceContainer.PopLabel();
}

//------------------------------------------------------------------------------
Id
Gfx::LoadResource(const Ptr<ResourceLoader>& loader) {
    o_assert_dbg(IsValid());
    return state->resourceContainer.Load(loader);
}

//------------------------------------------------------------------------------
Id
Gfx::LookupResource(const Locator& locator) {
    o_assert_dbg(IsValid());
    return state->resourceContainer.Lookup(locator);
}

//------------------------------------------------------------------------------
int
Gfx::QueryFreeResourceSlots(GfxResourceType::Code resourceType) {
    o_assert_dbg(IsValid());
    return state->resourceContainer.QueryFreeSlots(resourceType);
}

//------------------------------------------------------------------------------
ResourceInfo
Gfx::QueryResourceInfo(const Id& id) {
    o_assert_dbg(IsValid());
    return state->resourceContainer.QueryResourceInfo(id);
}

//------------------------------------------------------------------------------
ResourcePoolInfo
Gfx::QueryResourcePoolInfo(GfxResourceType::Code resType) {
    o_assert_dbg(IsValid());
    return state->resourceContainer.QueryPoolInfo(resType);
}

//------------------------------------------------------------------------------
void
Gfx::DestroyResources(ResourceLabel label) {
    o_assert_dbg(IsValid());
    return state->resourceContainer.DestroyDeferred(label);
}

//------------------------------------------------------------------------------
_priv::gfxResourceContainer&
Gfx::resource() {
    o_assert_dbg(IsValid());
    return state->resourceContainer;
}

//------------------------------------------------------------------------------
void
Gfx::ApplyViewPort(int x, int y, int width, int height, bool originTopLeft) {
    o_assert_dbg(IsValid());
    o_assert_dbg(state->inPass);
    state->gfxFrameInfo.NumApplyViewPort++;
    state->renderer.applyViewPort(x, y, width, height, originTopLeft);
}

//------------------------------------------------------------------------------
void
Gfx::ApplyScissorRect(int x, int y, int width, int height, bool originTopLeft) {
    o_assert_dbg(IsValid());
    o_assert_dbg(state->inPass);
    state->gfxFrameInfo.NumApplyScissorRect++;
    state->renderer.applyScissorRect(x, y, width, height, originTopLeft);
}

//------------------------------------------------------------------------------
void
Gfx::CommitFrame() {
    o_trace_scoped(Gfx_CommitFrame);
    o_assert_dbg(IsValid());
    o_assert_dbg(!state->inPass);
    state->renderer.commitFrame();
    state->displayManager.Present();
    state->resourceContainer.GarbageCollect();
    state->gfxFrameInfo = GfxFrameInfo();
}

//------------------------------------------------------------------------------
void
Gfx::ResetStateCache() {
    o_trace_scoped(Gfx_ResetStateCache);
    o_assert_dbg(IsValid());
    state->renderer.resetStateCache();
}

//------------------------------------------------------------------------------
void
Gfx::UpdateVertices(const Id& id, const void* data, int numBytes) {
    o_trace_scoped(Gfx_UpdateVertices);
    o_assert_dbg(IsValid());
    state->gfxFrameInfo.NumUpdateVertices++;
    mesh* msh = state->resourceContainer.lookupMesh(id);
    state->renderer.updateVertices(msh, data, numBytes);
}

//------------------------------------------------------------------------------
void
Gfx::UpdateIndices(const Id& id, const void* data, int numBytes) {
    o_trace_scoped(Gfx_UpdateIndices);
    o_assert_dbg(IsValid());
    state->gfxFrameInfo.NumUpdateIndices++;
    mesh* msh = state->resourceContainer.lookupMesh(id);
    state->renderer.updateIndices(msh, data, numBytes);
}

//------------------------------------------------------------------------------
void
Gfx::UpdateTexture(const Id& id, const void* data, const ImageDataAttrs& offsetsAndSizes) {
    o_trace_scoped(Gfx_UpdateTexture);
    o_assert_dbg(IsValid());
    state->gfxFrameInfo.NumUpdateTextures++;
    texture* tex = state->resourceContainer.lookupTexture(id);
    state->renderer.updateTexture(tex, data, offsetsAndSizes);
}

//------------------------------------------------------------------------------
void
Gfx::GenerateMipmaps(const Id& id) {
    o_trace_scoped(Gfx_GenerateMipmaps);
    o_assert_dbg(IsValid());
    o_assert_dbg(!state->inPass);
    state->gfxFrameInfo.NumGenerateMipmaps++;
    texture* tex = state->resourceContainer.lookupTexture(id);
    o_assert_dbg(tex->Setup.NumMipMaps > 1);
    state->renderer.generateMipmaps(tex);
}

//------------------------------------------------------------------------------
void
Gfx::Draw(int primGroupIndex, int numInstances) {
    o_trace_scoped(Gfx_Draw);
    o_assert_dbg(IsValid());
    o_assert_dbg(state->inPass);
    state->gfxFrameInfo.NumDraw++;
    state->renderer.draw(primGroupIndex, numInstances);
}

//------------------------------------------------------------------------------
void
Gfx::Draw(const PrimitiveGroup& primGroup, int numInstances) {
    o_trace_scoped(Gfx_Draw);
    o_assert_dbg(IsValid());
    o_assert_dbg(state->inPass);
    state->gfxFrameInfo.NumDraw++;
    state->renderer.draw(primGroup.BaseElement, primGroup.NumElements, numInstances);
}

//------------------------------------------------------------------------------
#if ORYOL_DEBUG
void
Gfx::validateMeshes(pipeline* pip, mesh** meshes, int num) {

    // checks that:
    //  - at least one input mesh must be attached, and it must be in slot 0
    //  - all attached input meshes must be valid
    //  - PrimitivesGroups may only be attached to the mesh in slot 0
    //  - if indexed rendering is used, the mesh in slot 0 must have an index buffer
    //  - the mesh in slot 0 cannot contain per-instance-data
    //  - no colliding vertex attributes across all input meshes
    //
    // this method should only be called in debug mode

    // FIXME FIXME FIXME: check for matching vertex layout!!!

    o_assert_dbg(meshes && (num > 0) && (num < GfxConfig::MaxNumInputMeshes));
    if (nullptr == meshes[0]) {
        o_error("invalid mesh block: at least one input mesh must be provided, in slot 0!\n");
    }
    if ((meshes[0]->indexBufferAttrs.Type != IndexType::None) &&
        (meshes[0]->indexBufferAttrs.NumIndices == 0)) {
        o_error("invalid mesh block: the input mesh at slot 0 uses indexed rendering, but has no indices!\n");
    }

    StaticArray<int, VertexAttr::NumVertexAttrs> vertexAttrCounts;
    vertexAttrCounts.Fill(0);
    for (int mshIndex = 0; mshIndex < GfxConfig::MaxNumInputMeshes; mshIndex++) {
        const meshBase* msh = meshes[mshIndex];
        if (msh) {
            if (ResourceState::Valid != msh->State) {
                o_error("invalid mesh block: input mesh at slot '%d' not valid!\n", mshIndex);
            }
            if ((mshIndex > 0) && (msh->indexBufferAttrs.Type != IndexType::None)) {
                o_error("invalid drawState: input mesh at slot '%d' has indices, only allowed for slot 0!\n", mshIndex);
            }
            if ((mshIndex > 0) && (msh->numPrimGroups > 0)) {
                o_error("invalid mesh block: input mesh at slot '%d' has primitive groups, only allowed for slot 0!\n", mshIndex);
            }
            const int numComps = msh->vertexBufferAttrs.Layout.NumComponents();
            for (int compIndex = 0; compIndex < numComps; compIndex++) {
                const auto& comp = msh->vertexBufferAttrs.Layout.ComponentAt(compIndex);
                vertexAttrCounts[comp.Attr]++;
                if (vertexAttrCounts[comp.Attr] > 1) {
                    o_error("invalid mesh block: same vertex attribute declared in multiple input meshes!\n");
                }
            }
        }
    }
}
#endif

//------------------------------------------------------------------------------
#if ORYOL_DEBUG
void
Gfx::validateTextures(ShaderStage::Code stage, pipeline* pip, texture** textures, int numTextures) {
    o_assert_dbg(pip);

    // check if provided texture types are compatible with the expections shader
    const shader* shd = pip->shd;
    o_assert_dbg(shd);
    int texBlockIndex = shd->Setup.TextureBlockIndexByStage(stage);
    o_assert_dbg(InvalidIndex != texBlockIndex);
    const TextureBlockLayout& layout = shd->Setup.TextureBlockLayout(texBlockIndex);
    for (int i = 0; i < numTextures; i++) {
        if (textures[i]) {
            const auto& texBlockComp = layout.ComponentAt(layout.ComponentIndexForBindSlot(i));
            if (texBlockComp.Type != textures[i]->textureAttrs.Type) {
                o_error("Texture type mismatch at slot '%s'\n", texBlockComp.Name.AsCStr());
            }
        }
    }
}
#endif

//------------------------------------------------------------------------------
#if ORYOL_DEBUG
void
Gfx::validateTextureSetup(const TextureSetup& setup, const void* data, int size) {
    o_assert((setup.NumMipMaps > 0) && (setup.NumMipMaps <= GfxConfig::MaxNumTextureMipMaps));
    o_assert((setup.Width >= 1) && (setup.Height >= 1) && (setup.Depth >= 1));
    if (data) {
        o_assert(size > 0);
        o_assert(setup.TextureUsage == Usage::Immutable);
        o_assert(setup.ImageData.NumMipMaps > 0);
        o_assert(setup.ImageData.NumFaces > 0);
    }
    if (setup.Type == TextureType::Texture2D) {
        o_assert(setup.Depth == 1);
    }
    if (setup.Type == TextureType::TextureArray) {
        o_assert(setup.Depth <= GfxConfig::MaxNumTextureArraySlices);
    }
    if (setup.Type == TextureType::Texture3D) {
        o_assert(!setup.RenderTarget);
    }
    if (setup.RenderTarget) {
        o_assert(setup.TextureUsage == Usage::Immutable);
        o_assert(PixelFormat::IsValidRenderTargetColorFormat(setup.ColorFormat));
        if (setup.DepthFormat != PixelFormat::InvalidPixelFormat) {
            o_assert(PixelFormat::IsValidRenderTargetDepthFormat(setup.DepthFormat));
        }
    }
    else {
        o_assert(setup.SampleCount == 1);
        o_assert(setup.DepthFormat == PixelFormat::InvalidPixelFormat);
    }
}
#endif

//------------------------------------------------------------------------------
#if ORYOL_DEBUG
void
Gfx::validateMeshSetup(const MeshSetup& setup, const void* data, int size) {
    o_assert(setup.ShouldSetupFullScreenQuad() || (setup.VertexUsage != Usage::InvalidUsage) || (setup.IndexUsage != Usage::InvalidUsage));
    if (setup.NumVertices > 0) {
        o_assert(!setup.Layout.Empty());
        if (setup.VertexUsage == Usage::Immutable) {
            o_assert(data && (size > 0));
            o_assert((setup.DataVertexOffset >= 0) && (setup.DataVertexOffset < size));
        }
    }
    if (setup.NumIndices > 0) {
        o_assert((setup.IndicesType == IndexType::Index16) || (setup.IndicesType == IndexType::Index32));
        if (setup.IndexUsage == Usage::Immutable) {
            o_assert(data && (size > 0));
            o_assert((setup.DataIndexOffset >= 0) && (setup.DataIndexOffset < size));
        }
    }
}
#endif

//------------------------------------------------------------------------------
#if ORYOL_DEBUG
void
Gfx::validatePipelineSetup(const PipelineSetup& setup) {
    o_assert(setup.PrimType != PrimitiveType::InvalidPrimitiveType);
    o_assert(setup.Shader.IsValid());
    bool anyLayoutValid = false;
    for (const auto& layout : setup.Layouts) {
        if (!layout.Empty()) {
            anyLayoutValid = true;
            break;
        }
    }
    o_assert(anyLayoutValid);
}
#endif

//------------------------------------------------------------------------------
#if ORYOL_DEBUG
void
Gfx::validateRenderPassSetup(const RenderPassSetup& setup) {
    // check that at least one color attachment texture is defined
    // and that there are no 'holes' if there are multiple attachments
    bool continuous = true;
    for (int i = 0; i < GfxConfig::MaxNumColorAttachments; i++) {
        if (setup.ColorAttachments[i].Texture.IsValid()) {
            if (!continuous) {
                o_error("invalid render pass: must have continuous color attachments!\n");
            }
        }
        else {
            if (0 == i) {
                o_error("invalid render pass: must have color attachment at slot 0!\n");
            }
            continuous = false;
        }
    }

    // check that all render targets have the required params
    const texture* t0 = state->resourceContainer.lookupTexture(setup.ColorAttachments[0].Texture);
    o_assert(t0);
    const int w = t0->textureAttrs.Width;
    const int h = t0->textureAttrs.Height;
    const int sampleCount = t0->textureAttrs.SampleCount;
    for (int i = 0; i < GfxConfig::MaxNumColorAttachments; i++) {
        const texture* tex = state->resourceContainer.lookupTexture(setup.ColorAttachments[i].Texture);
        if (tex) {
            const auto& attrs = tex->textureAttrs;
            if ((attrs.Width != w) || (attrs.Height != h)) {
                o_error("invalid render pass: all color attachments must have the same size!\n");
            }
            if (attrs.SampleCount != sampleCount) {
                o_error("invalid render pass: all color attachments must have same sample-count!\n");
            }
            if (attrs.TextureUsage != Usage::Immutable) {
                o_error("invalid render pass: color attachments must have immutable usage!\n");
            }
            if (!tex->Setup.RenderTarget) {
                o_error("invalid render pass: color attachment must have been setup as render target!\n");
            }
        }
    }
    const texture* dsTex = state->resourceContainer.lookupTexture(setup.DepthStencilAttachment.Texture);
    if (dsTex) {
        const auto& attrs = dsTex->textureAttrs;
        if ((attrs.Width != w) || (attrs.Height != h)) {
            o_error("invalid render pass: depth-stencil attachment must have same size as color attachments!\n");
        }
        if (attrs.SampleCount != sampleCount) {
            o_error("invalid render pass: depth-stencil attachment must have sample sample-count as color attachments!\n");
        }
        if (attrs.TextureUsage != Usage::Immutable) {
            o_error("invalid render pass: depth attachment must have immutable usage!\n");
        }
        if (!dsTex->Setup.RenderTarget) {
            o_error("invalid render pass: depth attachment must have been setup as render target!\n");
        }
    }
}
#endif

//------------------------------------------------------------------------------
#if ORYOL_DEBUG
void
Gfx::validateShaderSetup(const ShaderSetup& setup) {
    // hmm, FIXME
}
#endif

//------------------------------------------------------------------------------
template<> Id
Gfx::CreateResource(const TextureSetup& setup, const void* data, int size) {
    o_assert_dbg(IsValid());
    #if ORYOL_DEBUG
    validateTextureSetup(setup, data, size);
    #endif
    return state->resourceContainer.Create(setup, data, size);
}

//------------------------------------------------------------------------------
template<> Id
Gfx::CreateResource(const MeshSetup& setup, const void* data, int size) {
    o_assert_dbg(IsValid());
    #if ORYOL_DEBUG
    validateMeshSetup(setup, data, size);
    #endif
    return state->resourceContainer.Create(setup, data, size);
}

//------------------------------------------------------------------------------
template<> Id
Gfx::CreateResource(const ShaderSetup& setup, const void* data, int size) {
    o_assert_dbg(IsValid());
    #if ORYOL_DEBUG
    validateShaderSetup(setup);
    #endif
    return state->resourceContainer.Create(setup, nullptr, 0);
}

//------------------------------------------------------------------------------
template<> Id
Gfx::CreateResource(const PipelineSetup& setup, const void* data, int size) {
    o_assert_dbg(IsValid());
    #if ORYOL_DEBUG
    validatePipelineSetup(setup);
    #endif
    return state->resourceContainer.Create(setup, nullptr, 0);
}

//------------------------------------------------------------------------------
template<> Id
Gfx::CreateResource(const RenderPassSetup& setup, const void* data, int size) {
    o_assert_dbg(IsValid());
    #if ORYOL_DEBUG
    validateRenderPassSetup(setup);
    #endif
    return state->resourceContainer.Create(setup, nullptr, 0);
}

} // namespace Oryol
