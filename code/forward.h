#pragma once

//#define STANDARD_SHADOW
//#define PCF_SHADOW
#define VARIANCE_SHADOW

struct standard_shadow_data
{
    vk_linear_arena Arena;
    
    u32 Width;
    u32 Height;
    VkSampler Sampler;
    VkImage ShadowImage;
    render_target_entry ShadowEntry;
    render_target RenderTarget;
    vk_pipeline* ShadowPipeline;
    vk_pipeline* ForwardPipeline;
};

struct variance_shadow_data
{
    vk_linear_arena Arena;

    u32 Width;
    u32 Height;
    VkSampler Sampler;

    VkImage DepthImage;
    render_target_entry DepthEntry;
    VkImage VarianceImage;
    render_target_entry VarianceEntry;
    // NOTE: For blurring to ping pong with
    VkImage VarianceImage2;
    render_target_entry VarianceEntry2; 
    render_target RenderTarget;
    vk_pipeline* ShadowPipeline;
    vk_pipeline* ForwardPipeline;

    VkDescriptorSetLayout BlurDescLayout;
    VkDescriptorSet BlurXDescriptor;
    VkDescriptorSet BlurYDescriptor;
    render_target BlurXTarget;
    render_target BlurYTarget;
    vk_pipeline* BlurXPipeline;
    vk_pipeline* BlurYPipeline;
};

struct forward_state
{
    vk_linear_arena RenderTargetArena;

    standard_shadow_data StandardShadow;
    standard_shadow_data PcfShadow;
    variance_shadow_data VarianceShadow;

    VkImage ColorImage;
    render_target_entry ColorEntry;
    VkImage DepthImage;
    render_target_entry DepthEntry;
    render_target ForwardRenderTarget;

    VkDescriptorSetLayout ShadowDescLayout;
    VkDescriptorSet ShadowDescriptor;
};
