
/*

  NOTE: Shadow Mapping Techniques

    - http://developer.download.nvidia.com/presentations/2008/GDC/GDC08_SoftShadowMapping.pdf
    - https://mynameismjp.wordpress.com/2013/09/10/shadow-maps/
  
  NOTE: Variance Shadow Maps

    - https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-8-summed-area-variance-shadow-maps
    - https://http.download.nvidia.com/developer/presentations/2006/gdc/2006-GDC-Variance-Shadow-Maps.pdf

    The goal is to store in the shadow map data related to % coverage. If we just wanted to store coverage though, we could do that with
    a regluar alpha value, but this would also be incorrect since it wouldn't store for which depth values that shadow starts (the range),
    nor how it changes in that range.

    Can we interpolate regular depth values? Sorta but not really, it gives incorrect results in the shadow map. Interpolating depth
    gives us depth < average(occluder_depth) but we really want average(depth < occluder_depth) (how does the depth value distribute
    inside of the pixel).

    What we want is a CDF (cumulative distribution function), which stores CDF(t) = P(x <= t) where t is a fragment at distance t from
    light.
    
    Variance shadow maps store depth and depth^2 in a 2 component shadow map. When we filter over a region of the shadow map,
    we reconstruct its moments:

    M1 = E(x) = Integral(-inf, inf, x*p(x)*dx)
    M2 = E(x^2) = Integral(-inf, inf, x^2*p(x)*dx)

    In this case, x is the depth value stored in the depth map. We then can reconstruct the mean and variance of this surface depth value
    using the following:

    Mean = E(x) = M1
    Variance^2 = E(x^2) - E(x)^2 = M2 - M1^2

    We then apply Chebyshevs inequality to compute an upper bound on whether our world depth position is occluded (% coverage):

    P(x >= t) <= Pmax(t) = Variance^2 / (Variance^2 + (t - Mean)^2)

    ^ The above will give us the % coverage of the sample, t is the depth value from our fragment and x is the depth value of our
    surface in the depth map. This inequality is only valid for t > Mean, otherwise Pmax = 1 and the surface is fully lit.

    Apparently this bound becomes exact when we have a single planar occluder and a single planar reciever which occurs frequently in game
    scenes.

    A huge benefit of this is that our shadow map is linearly filterable now, since filtering in depth and depth^2 give us the moments
    and we reconstruct them correctly to get our distribution of depth values per pixel so the test remains unchanged.

    IMPORTANT: When we say above that we filter a region of the shadow map, that just means we use a sampler that isn't a point sampler.
    We can use a aniso sampler/mipmapping/MSAA/etc with this technique and we can do blurs on the shadow map to get softer shadows.
    So in code, you aren't explicitly calculating the moment integral, that comes from sample calls.
  
 */

inline vk_pipeline* ForwardPipelineCreate(char* VertFileName, char* FragFileName, renderer_create_info CreateInfo,
                                          render_target RenderTarget, VkDescriptorSetLayout ShadowDescLayout)
{
    vk_pipeline* Result = 0;
    
    vk_pipeline_builder Builder = VkPipelineBuilderBegin(&DemoState->TempArena);

    // NOTE: Shaders
    VkPipelineShaderAdd(&Builder, VertFileName, "main", VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderAdd(&Builder, FragFileName, "main", VK_SHADER_STAGE_FRAGMENT_BIT);

    // NOTE: Specify input vertex data format
    VkPipelineVertexBindingBegin(&Builder);
    VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
    VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
    VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32_SFLOAT, sizeof(v2));
    VkPipelineVertexBindingEnd(&Builder);

    VkPipelineInputAssemblyAdd(&Builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
    VkPipelineDepthStateAdd(&Builder, VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER);
                
    // NOTE: Set the blending state
    VkPipelineColorAttachmentAdd(&Builder, VK_FALSE, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                 VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);

    VkDescriptorSetLayout DescriptorLayouts[] =
        {
            CreateInfo.MaterialDescLayout,
            CreateInfo.SceneDescLayout,
            ShadowDescLayout,
        };
            
    Result = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager, RenderTarget.RenderPass, 0,
                                  DescriptorLayouts, ArrayCount(DescriptorLayouts));

    return Result;
}

//
// NOTE: Standard Shadow Data
//

inline void StandardShadowCreate(u32 Width, u32 Height, renderer_create_info CreateInfo, render_target ForwardRenderTarget,
                                 VkDescriptorSetLayout ShadowDescLayout, b32 Pcf, standard_shadow_data* Result)
{
    // TODO: Add PCF controls
    *Result = {};

    u64 HeapSize = MegaBytes(64);
    Result->Arena = VkLinearArenaCreate(VkMemoryAllocate(RenderState->Device, RenderState->LocalMemoryId, HeapSize), HeapSize);

    {
        VkSamplerCreateInfo SamplerCreateInfo = {};
        SamplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        SamplerCreateInfo.magFilter = VK_FILTER_NEAREST;
        SamplerCreateInfo.minFilter = VK_FILTER_NEAREST;
        SamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        SamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        SamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        SamplerCreateInfo.anisotropyEnable = VK_FALSE;
        SamplerCreateInfo.maxAnisotropy = 0.0f;
        SamplerCreateInfo.compareEnable = VK_FALSE;
        SamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        SamplerCreateInfo.mipLodBias = 0;
        SamplerCreateInfo.minLod = 0;
        SamplerCreateInfo.maxLod = 0;
        SamplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        SamplerCreateInfo.unnormalizedCoordinates = VK_FALSE;

        VkCheckResult(vkCreateSampler(RenderState->Device, &SamplerCreateInfo, 0, &Result->Sampler));
    }
    
    Result->ShadowEntry = RenderTargetEntryCreate(&Result->Arena, Width, Height, VK_FORMAT_D32_SFLOAT,
                                                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

    // NOTE: Shadow RT
    {
        render_target_builder Builder = RenderTargetBuilderBegin(&DemoState->Arena, &DemoState->TempArena, Width, Height);
        RenderTargetAddTarget(&Builder, &Result->ShadowEntry, VkClearDepthStencilCreate(1, 0));
                            
        vk_render_pass_builder RpBuilder = VkRenderPassBuilderBegin(&DemoState->TempArena);

        u32 DepthId = VkRenderPassAttachmentAdd(&RpBuilder, Result->ShadowEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
        VkRenderPassDepthRefAdd(&RpBuilder, DepthId, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        VkRenderPassSubPassEnd(&RpBuilder);

        VkRenderPassDependency(&RpBuilder, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT);
                
        Result->RenderTarget = RenderTargetBuilderEnd(&Builder, VkRenderPassBuilderEnd(&RpBuilder, RenderState->Device));
    }
    
    // NOTE: Shadow PSO
    {
        vk_pipeline_builder Builder = VkPipelineBuilderBegin(&DemoState->TempArena);

        // NOTE: Shaders
        VkPipelineShaderAdd(&Builder, "shader_shadow_vert.spv", "main", VK_SHADER_STAGE_VERTEX_BIT);
                
        // NOTE: Specify input vertex data format
        VkPipelineVertexBindingBegin(&Builder);
        VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
        VkPipelineVertexAttributeAddOffset(&Builder, sizeof(v3));
        VkPipelineVertexAttributeAddOffset(&Builder, sizeof(v2));
        VkPipelineVertexBindingEnd(&Builder);

        VkPipelineInputAssemblyAdd(&Builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
        VkPipelineDepthStateAdd(&Builder, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS);
        //VkPipelineDepthOffsetAdd(&Builder, 4.0f, 100.0f, 1.5f);

        VkDescriptorSetLayout DescriptorLayouts[] =
            {
                CreateInfo.MaterialDescLayout,
                CreateInfo.SceneDescLayout,
            };
            
        Result->ShadowPipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                      Result->RenderTarget.RenderPass, 0, DescriptorLayouts, ArrayCount(DescriptorLayouts));
    }

    if (Pcf)
    {
        Result->ForwardPipeline = ForwardPipelineCreate("shader_forward_pcf_vert.spv", "shader_forward_pcf_frag.spv", CreateInfo,
                                                        ForwardRenderTarget, ShadowDescLayout);
    }
    else
    {
        Result->ForwardPipeline = ForwardPipelineCreate("shader_forward_standard_vert.spv", "shader_forward_standard_frag.spv",
                                                        CreateInfo, ForwardRenderTarget, ShadowDescLayout);
    }
}

//
// NOTE: Variance Shadow Data
//

inline void VarianceShadowCreate(u32 Width, u32 Height, renderer_create_info CreateInfo, render_target ForwardRenderTarget,
                                 VkDescriptorSetLayout ShadowDescLayout, variance_shadow_data* Result)
{
    *Result = {};

    u64 HeapSize = MegaBytes(64);
    Result->Arena = VkLinearArenaCreate(VkMemoryAllocate(RenderState->Device, RenderState->LocalMemoryId, HeapSize), HeapSize);

    Result->Sampler = VkSamplerCreate(RenderState->Device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 16.0f);    
    Result->VarianceEntry = RenderTargetEntryCreate(&Result->Arena, Width, Height, VK_FORMAT_R32G32_SFLOAT,
                                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    Result->VarianceEntry2 = RenderTargetEntryCreate(&Result->Arena, Width, Height, VK_FORMAT_R32G32_SFLOAT,
                                                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    Result->DepthEntry = RenderTargetEntryCreate(&Result->Arena, Width, Height, VK_FORMAT_D32_SFLOAT,
                                                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

    // NOTE: Shadow RT
    {
        render_target_builder Builder = RenderTargetBuilderBegin(&DemoState->Arena, &DemoState->TempArena, Width, Height);
        RenderTargetAddTarget(&Builder, &Result->VarianceEntry, VkClearColorCreate(1, 1, 0, 0));
        RenderTargetAddTarget(&Builder, &Result->DepthEntry, VkClearDepthStencilCreate(1, 0));
                            
        vk_render_pass_builder RpBuilder = VkRenderPassBuilderBegin(&DemoState->TempArena);

        u32 VarianceId = VkRenderPassAttachmentAdd(&RpBuilder, Result->VarianceEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        u32 DepthId = VkRenderPassAttachmentAdd(&RpBuilder, Result->DepthEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
        VkRenderPassColorRefAdd(&RpBuilder, VarianceId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderPassDepthRefAdd(&RpBuilder, DepthId, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        VkRenderPassSubPassEnd(&RpBuilder);

        VkRenderPassDependency(&RpBuilder, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT);
                
        Result->RenderTarget = RenderTargetBuilderEnd(&Builder, VkRenderPassBuilderEnd(&RpBuilder, RenderState->Device));
    }
    
    // NOTE: Shadow PSO
    {
        vk_pipeline_builder Builder = VkPipelineBuilderBegin(&DemoState->TempArena);

        // NOTE: Shaders
        VkPipelineShaderAdd(&Builder, "shader_shadow_vert.spv", "main", VK_SHADER_STAGE_VERTEX_BIT);
        VkPipelineShaderAdd(&Builder, "shader_shadow_variance_frag.spv", "main", VK_SHADER_STAGE_FRAGMENT_BIT);
                
        // NOTE: Specify input vertex data format
        VkPipelineVertexBindingBegin(&Builder);
        VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
        VkPipelineVertexAttributeAddOffset(&Builder, sizeof(v3));
        VkPipelineVertexAttributeAddOffset(&Builder, sizeof(v2));
        VkPipelineVertexBindingEnd(&Builder);

        VkPipelineInputAssemblyAdd(&Builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
        VkPipelineDepthStateAdd(&Builder, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS);
                
        // NOTE: Set the blending state
        VkPipelineColorAttachmentAdd(&Builder, VK_FALSE, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                     VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);

        VkDescriptorSetLayout DescriptorLayouts[] =
            {
                CreateInfo.MaterialDescLayout,
                CreateInfo.SceneDescLayout,
            };
            
        Result->ShadowPipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                      Result->RenderTarget.RenderPass, 0, DescriptorLayouts, ArrayCount(DescriptorLayouts));
    }
    
    Result->ForwardPipeline = ForwardPipelineCreate("shader_forward_variance_vert.spv", "shader_forward_variance_frag.spv", CreateInfo,
                                                    ForwardRenderTarget, ShadowDescLayout);

    // NOTE: Blur Passes
    {
        {
            vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&Result->BlurDescLayout);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutEnd(RenderState->Device, &Builder);
        }
        
        // NOTE: Blur Render Target
        {
            vk_render_pass_builder RpBuilder = VkRenderPassBuilderBegin(&DemoState->TempArena);

            u32 OutputId = VkRenderPassAttachmentAdd(&RpBuilder, Result->VarianceEntry.Format, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                     VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
            VkRenderPassColorRefAdd(&RpBuilder, OutputId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            VkRenderPassSubPassEnd(&RpBuilder);

            VkRenderPassDependency(&RpBuilder, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT);

            VkRenderPass RenderPass = VkRenderPassBuilderEnd(&RpBuilder, RenderState->Device);

            // NOTE: Blur X Target
            {
                render_target_builder Builder = RenderTargetBuilderBegin(&DemoState->Arena, &DemoState->TempArena, Width, Height);
                RenderTargetAddTarget(&Builder, &Result->VarianceEntry2, VkClearColorCreate(0, 0, 0, 0));
                Result->BlurXTarget = RenderTargetBuilderEnd(&Builder, RenderPass);

                Result->BlurXDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, Result->BlurDescLayout);
                VkDescriptorImageWrite(&RenderState->DescriptorManager, Result->BlurXDescriptor, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                       Result->VarianceEntry.View, DemoState->PointSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            
            // NOTE: Blur Y Target
            {
                render_target_builder Builder = RenderTargetBuilderBegin(&DemoState->Arena, &DemoState->TempArena, Width, Height);
                RenderTargetAddTarget(&Builder, &Result->VarianceEntry, VkClearColorCreate(0, 0, 0, 0));
                Result->BlurYTarget = RenderTargetBuilderEnd(&Builder, RenderPass);

                Result->BlurYDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, Result->BlurDescLayout);
                VkDescriptorImageWrite(&RenderState->DescriptorManager, Result->BlurYDescriptor, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                       Result->VarianceEntry2.View, DemoState->PointSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }
        
        VkDescriptorSetLayout Layouts[] =
        {
            Result->BlurDescLayout,
        };
        Result->BlurXPass = FullScreenPassCreate("shader_gaussian_x_frag.spv", "main", &Result->BlurXTarget, ArrayCount(Layouts),
                                                 Layouts, 1, &Result->BlurXDescriptor);
        Result->BlurYPass = FullScreenPassCreate("shader_gaussian_y_frag.spv", "main", &Result->BlurYTarget, ArrayCount(Layouts),
                                                 Layouts, 1, &Result->BlurYDescriptor);
    }
}

//
// NOTE: Forward Render Data
//

inline void ForwardSwapChainChange(forward_state* State, u32 Width, u32 Height, VkFormat ColorFormat, render_scene* Scene,
                                   VkDescriptorSet* OutputRtSet)
{
    b32 ReCreate = State->RenderTargetArena.Used != 0;
    VkArenaClear(&State->RenderTargetArena);

    // NOTE: Render Target Data
    {
        RenderTargetEntryReCreate(&State->RenderTargetArena, Width, Height, ColorFormat,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                                  &State->ColorEntry);
        RenderTargetEntryReCreate(&State->RenderTargetArena, Width, Height, VK_FORMAT_D32_SFLOAT,
                                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, &State->DepthEntry);

        if (ReCreate)
        {
            RenderTargetUpdateEntries(&DemoState->TempArena, &State->ForwardRenderTarget);
        }

        VkDescriptorImageWrite(&RenderState->DescriptorManager, *OutputRtSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               State->ColorEntry.View, DemoState->LinearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
        
    VkDescriptorManagerFlush(RenderState->Device, &RenderState->DescriptorManager);
}

inline void ForwardCreate(renderer_create_info CreateInfo, VkDescriptorSet* OutputRtSet, forward_state* Result)
{
    *Result = {};

    u64 HeapSize = MegaBytes(256);
    Result->RenderTargetArena = VkLinearArenaCreate(VkMemoryAllocate(RenderState->Device, RenderState->LocalMemoryId, HeapSize), HeapSize);
    
    ForwardSwapChainChange(Result, CreateInfo.Width, CreateInfo.Height, CreateInfo.ColorFormat, CreateInfo.Scene, OutputRtSet);

    {
        vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&Result->ShadowDescLayout);
        VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
        VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
        VkDescriptorLayoutEnd(RenderState->Device, &Builder);
    }
    
    // NOTE: Forward RT
    {
        render_target_builder Builder = RenderTargetBuilderBegin(&DemoState->Arena, &DemoState->TempArena, CreateInfo.Width, CreateInfo.Height);
        RenderTargetAddTarget(&Builder, &Result->ColorEntry, VkClearColorCreate(0, 0, 0, 1));
        RenderTargetAddTarget(&Builder, &Result->DepthEntry, VkClearDepthStencilCreate(0, 0));
                            
        vk_render_pass_builder RpBuilder = VkRenderPassBuilderBegin(&DemoState->TempArena);

        u32 ColorId = VkRenderPassAttachmentAdd(&RpBuilder, Result->ColorEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        u32 DepthId = VkRenderPassAttachmentAdd(&RpBuilder, Result->DepthEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
        VkRenderPassColorRefAdd(&RpBuilder, ColorId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderPassDepthRefAdd(&RpBuilder, DepthId, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        VkRenderPassSubPassEnd(&RpBuilder);

        Result->ForwardRenderTarget = RenderTargetBuilderEnd(&Builder, VkRenderPassBuilderEnd(&RpBuilder, RenderState->Device));
    }

    u32 ShadowWidth = 1024;
    u32 ShadowHeight = 1024;
    StandardShadowCreate(ShadowWidth, ShadowHeight, CreateInfo, Result->ForwardRenderTarget, Result->ShadowDescLayout, false, &Result->StandardShadow);
    StandardShadowCreate(ShadowWidth, ShadowHeight, CreateInfo, Result->ForwardRenderTarget, Result->ShadowDescLayout, true, &Result->PcfShadow);
    VarianceShadowCreate(ShadowWidth, ShadowHeight, CreateInfo, Result->ForwardRenderTarget, Result->ShadowDescLayout, &Result->VarianceShadow);

    // TODO: Choose our shadow algo cleaner
    Result->ShadowDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, Result->ShadowDescLayout);
#ifdef STANDARD_SHADOW
    VkDescriptorImageWrite(&RenderState->DescriptorManager, Result->ShadowDescriptor, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           Result->StandardShadow.ShadowEntry.View, Result->StandardShadow.Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
#endif
#ifdef PCF_SHADOW
    VkDescriptorImageWrite(&RenderState->DescriptorManager, Result->ShadowDescriptor, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           Result->PcfShadow.ShadowEntry.View, Result->PcfShadow.Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
#endif
#ifdef VARIANCE_SHADOW
    VkDescriptorImageWrite(&RenderState->DescriptorManager, Result->ShadowDescriptor, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           Result->VarianceShadow.VarianceEntry.View, Result->VarianceShadow.Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
#endif
    
    VkDescriptorManagerFlush(RenderState->Device, &RenderState->DescriptorManager);
}

inline void ForwardRender(vk_commands Commands, forward_state* State, render_scene* Scene)
{
    render_target ShadowRenderTarget = {};
    vk_pipeline* ShadowPipeline = {};
    vk_pipeline* ForwardPipeline = {};

#ifdef STANDARD_SHADOW
    ShadowRenderTarget = State->StandardShadow.RenderTarget;
    ShadowPipeline = State->StandardShadow.ShadowPipeline;
    ForwardPipeline = State->StandardShadow.ForwardPipeline;
#endif
#ifdef PCF_SHADOW
    ShadowRenderTarget = State->PcfShadow.RenderTarget;
    ShadowPipeline = State->PcfShadow.ShadowPipeline;
    ForwardPipeline = State->PcfShadow.ForwardPipeline;
#endif
#ifdef VARIANCE_SHADOW
    ShadowRenderTarget = State->VarianceShadow.RenderTarget;
    ShadowPipeline = State->VarianceShadow.ShadowPipeline;
    ForwardPipeline = State->VarianceShadow.ForwardPipeline;
#endif
    
    // NOTE: Generate Directional Shadow Map
    RenderTargetRenderPassBegin(&ShadowRenderTarget, Commands, RenderTargetRenderPass_SetViewPort | RenderTargetRenderPass_SetScissor);
    {
        vkCmdBindPipeline(Commands.Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ShadowPipeline->Handle);
        {
            VkDescriptorSet DescriptorSets[] =
                {
                    Scene->SceneDescriptor,
                };
            vkCmdBindDescriptorSets(Commands.Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ShadowPipeline->Layout, 1,
                                    ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
        }
        
        for (u32 InstanceId = 0; InstanceId < Scene->NumOpaqueInstances; ++InstanceId)
        {
            instance_entry* CurrInstance = Scene->OpaqueInstances + InstanceId;
            render_mesh* CurrMesh = Scene->RenderMeshes + CurrInstance->MeshId;
            
            VkDeviceSize Offset = 0;
            vkCmdBindVertexBuffers(Commands.Buffer, 0, 1, &CurrMesh->VertexBuffer, &Offset);
            vkCmdBindIndexBuffer(Commands.Buffer, CurrMesh->IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(Commands.Buffer, CurrMesh->NumIndices, 1, 0, 0, InstanceId);
        }
    }
    RenderTargetRenderPassEnd(Commands);        

#ifdef VARIANCE_SHADOW
    FullScreenPassRender(Commands, &State->VarianceShadow.BlurXPass);
    FullScreenPassRender(Commands, &State->VarianceShadow.BlurYPass);
#endif
    
    // NOTE: Draw Meshes
    RenderTargetRenderPassBegin(&State->ForwardRenderTarget, Commands, RenderTargetRenderPass_SetViewPort | RenderTargetRenderPass_SetScissor);
    {
        vkCmdBindPipeline(Commands.Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ForwardPipeline->Handle);
        {
            VkDescriptorSet DescriptorSets[] =
                {
                    Scene->SceneDescriptor,
                    State->ShadowDescriptor,
                };
            vkCmdBindDescriptorSets(Commands.Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ForwardPipeline->Layout, 1,
                                    ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
        }
        
        for (u32 InstanceId = 0; InstanceId < Scene->NumOpaqueInstances; ++InstanceId)
        {
            instance_entry* CurrInstance = Scene->OpaqueInstances + InstanceId;
            render_mesh* CurrMesh = Scene->RenderMeshes + CurrInstance->MeshId;

            {
                VkDescriptorSet DescriptorSets[] =
                    {
                        CurrMesh->MaterialDescriptor,
                    };
                vkCmdBindDescriptorSets(Commands.Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ForwardPipeline->Layout, 0,
                                        ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
            }
            
            VkDeviceSize Offset = 0;
            vkCmdBindVertexBuffers(Commands.Buffer, 0, 1, &CurrMesh->VertexBuffer, &Offset);
            vkCmdBindIndexBuffer(Commands.Buffer, CurrMesh->IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(Commands.Buffer, CurrMesh->NumIndices, 1, 0, 0, InstanceId);
        }
    }
    RenderTargetRenderPassEnd(Commands);        
}
