#pragma once

#define VALIDATION 1

#include "framework_vulkan\framework_vulkan.h"

/*

  NOTE: The goal for this demo is to try various shadow mapping techniques. The goals are the following:

    - 

  References:

    - http://ogldev.atspace.co.uk/www/tutorial23/tutorial23.html
    - http://ogldev.atspace.co.uk/www/tutorial24/tutorial24.html
    - http://ogldev.atspace.co.uk/www/tutorial42/tutorial42.html
    - https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping
    - http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-16-shadow-mapping/
    
    
 */

struct directional_light_gpu
{
    v3 Color;
    u32 Pad0;
    v3 Dir;
    u32 Pad1;
    v3 AmbientColor;
    u32 Pad2;
    m4 VPTransform;
};

struct shadow_directional_light
{
    directional_light_gpu GpuData;
    VkBuffer Globals;
    VkBuffer ShadowTransforms;
};

struct point_light
{
    v3 Color;
    u32 Pad0;
    v3 Pos;
    f32 MaxDistance;
};

struct scene_globals
{
    v3 CameraPos;
    u32 NumPointLights;
};

struct instance_entry
{
    u32 MeshId;
    m4 ShadowWVP;
    m4 WTransform;
    m4 WVPTransform;
};

struct gpu_instance_entry
{
    m4 WTransform;
    m4 WVPTransform;
};

struct render_mesh
{
    vk_image Color;
    vk_image Normal;
    VkDescriptorSet MaterialDescriptor;
    
    VkBuffer VertexBuffer;
    VkBuffer IndexBuffer;
    u32 NumIndices;
};

struct render_scene;
struct renderer_create_info
{
    u32 Width;
    u32 Height;
    VkFormat ColorFormat;

    VkDescriptorSetLayout MaterialDescLayout;
    VkDescriptorSetLayout SceneDescLayout;
    render_scene* Scene;
};

#include "forward.h"

struct render_scene
{
    // NOTE: General Render Data
    camera Camera;
    VkDescriptorSetLayout MaterialDescLayout;
    VkDescriptorSetLayout SceneDescLayout;
    VkBuffer SceneBuffer;
    VkDescriptorSet SceneDescriptor;

    shadow_directional_light DirectionalLight;
    
    // NOTE: Scene Lights
    u32 MaxNumPointLights;
    u32 NumPointLights;
    point_light* PointLights;
    VkBuffer PointLightBuffer;
    VkBuffer PointLightTransforms;
    
    // NOTE: Scene Meshes
    u32 MaxNumRenderMeshes;
    u32 NumRenderMeshes;
    render_mesh* RenderMeshes;
    
    // NOTE: Opaque Instances
    u32 MaxNumOpaqueInstances;
    u32 NumOpaqueInstances;
    instance_entry* OpaqueInstances;
    VkBuffer OpaqueInstanceBuffer;
};

struct demo_state
{
    linear_arena Arena;
    linear_arena TempArena;

    // NOTE: Samplers
    VkSampler PointSampler;
    VkSampler LinearSampler;
    VkSampler AnisoSampler;

    // NOTE: Render Target Entries
    VkFormat SwapChainFormat;
    render_target_entry SwapChainEntry;
    render_target CopyToSwapTarget;
    VkDescriptorSet CopyToSwapDesc;
    vk_pipeline* CopyToSwapPipeline;

    render_scene Scene;

    // NOTE: Saved model ids
    u32 Quad;
    u32 Cube;
    u32 Sphere;

    forward_state ForwardState;
};

global demo_state* DemoState;
