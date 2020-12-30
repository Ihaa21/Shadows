#version 450

/*

   TODO: References for more techniques to implement + better versions of below:

    - https://www.trentreed.net/blog/exponential-shadow-maps/
    - https://discovery.ucl.ac.uk/id/eprint/10001/1/10001.pdf
    - https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-8-summed-area-variance-shadow-maps
    - google poisson disk sampling for PCF kernels
    - https://gamedev.stackexchange.com/questions/66030/exponential-variance-shadow-mapping-implementation
        - http://developer.download.nvidia.com/presentations/2008/GDC/GDC08_SoftShadowMapping.pdf
    - https://mynameismjp.wordpress.com/2013/09/10/shadow-maps/
    - https://cg.cs.uni-bonn.de/aigaion2root/attachments/MomentShadowMapping.pdf
  
 */

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "shader_blinn_phong_lighting.cpp"
#include "shader_descriptor_layouts.cpp"

MATERIAL_DESCRIPTOR_LAYOUT(0)
SCENE_DESCRIPTOR_LAYOUT(1)
SHADOW_DESCRIPTOR_LAYOUT(2)

#if SHADOW_VERTEX

layout(location = 0) in vec3 InPos;

layout(location = 0) out float OutDepth;

void main()
{
    vec4 Position = DirectionalTransforms[gl_InstanceIndex] * vec4(InPos, 1);
    gl_Position = Position;
    OutDepth = Position.z;
}

#endif

#if SHADOW_VARIANCE_FRAGMENT

layout(location = 0) in float InDepth;

layout(location = 0) out vec2 OutMoments;

void main()
{
    OutMoments.x = InDepth;
    OutMoments.y = InDepth * InDepth;
    //OutMoments.x = InDepth;
    //float Dx = dFdx(InDepth);
    //float Dy = dFdy(InDepth);
    //OutMoments.y = InDepth * InDepth + 0.25*(Dx*Dx + Dy*Dy);
}

#endif

#if FORWARD_VERTEX

layout(location = 0) in vec3 InPos;
layout(location = 1) in vec3 InNormal;
layout(location = 2) in vec2 InUv;

layout(location = 0) out vec3 OutWorldPos;
layout(location = 1) out vec3 OutWorldNormal;
layout(location = 2) out vec2 OutUv;
layout(location = 3) out vec3 OutDirLightPos;

void main()
{
    instance_entry Entry = InstanceBuffer[gl_InstanceIndex];
    
    gl_Position = Entry.WVPTransform * vec4(InPos, 1);
    OutWorldPos = (Entry.WTransform * vec4(InPos, 1)).xyz;
    OutWorldNormal = (Entry.WTransform * vec4(InNormal, 0)).xyz;
    OutUv = InUv;
    OutDirLightPos = (DirectionalTransforms[gl_InstanceIndex] * vec4(InPos, 1)).xyz;
}

#endif

#if FORWARD_FRAGMENT

layout(location = 0) in vec3 InWorldPos;
layout(location = 1) in vec3 InWorldNormal;
layout(location = 2) in vec2 InUv;
layout(location = 3) in vec3 InDirLightPos;

layout(location = 0) out vec4 OutColor;

float DirLightOcclusionStandardGet(vec3 SurfaceNormal, vec3 LightDir, vec3 LightPos)
{
    // NOTE: You can embedd the NDC transform in the matrix but then you need a separate set of transforms for each object
    vec2 Uv = 0.5*LightPos.xy + vec2(0.5);

    // NOTE: This bias comes from http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-16-shadow-mapping/ and
    // https://www.trentreed.net/blog/exponential-shadow-maps/ . The idea is dot is cos(theta) so acos(cos(theta)) = theta so it all
    // becomes tan(angle between vectors). Since we are scaling by the tangent of the angle between vectors, the more perpendicular the
    // angles are, the larger our bias will be. This appears to be a decent approximation
    float Bias = clamp(0.005 * tan(acos(clamp(dot(SurfaceNormal, LightDir), 0, 1))), 0, 0.005);
    float Depth = texture(StandardShadowMap, Uv).x;
    
    return step(LightPos.z - Bias, Depth);
}

float DirLightOcclusionPcfGet(vec3 SurfaceNormal, vec3 LightDir, vec3 LightPos)
{
    // NOTE: http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-16-shadow-mapping/
    vec2 PoissonDisk[4] =
        {
            vec2( -0.94201624, -0.39906216 ),
            vec2( 0.94558609, -0.76890725 ),
            vec2( -0.094184101, -0.92938870 ),
            vec2( 0.34495938, 0.29387760 ),
        };

    vec2 LightPosUv = 0.5*LightPos.xy + vec2(0.5);
    float Bias = clamp(0.005 * tan(acos(clamp(dot(SurfaceNormal, LightDir), 0, 1))), 0, 0.005);
    float Occlusion = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        // TODO: We define the spreading via /700 but its probably better to define it via pixel size in world space using derivatives?
        float Depth = texture(StandardShadowMap, LightPosUv + PoissonDisk[i]/700.0).x;
        Occlusion += (1.0f / 4.0f) * step(LightPos.z - Bias, Depth);
    }

    // TODO: Probably better to do uniform sampling of 7x7 region using gathers
    
    return Occlusion;
}

float LineStep(float Min, float Max, float Value)
{
    // NOTE: Inverse to lerp
    return clamp((Value - Min) / (Max - Min), 0, 1);
}

float DirLightOcclusionVarianceGet(vec3 SurfaceNormal, vec3 LightDir, vec3 LightPos)
{
    // NOTE: https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-8-summed-area-variance-shadow-maps
    // NOTE: https://http.download.nvidia.com/developer/presentations/2006/gdc/2006-GDC-Variance-Shadow-Maps.pdf
    float Occlusion = 0.0f;
    
    // NOTE: You can embedd the NDC transform in the matrix but then you need a separate set of transforms for each object
    vec2 Uv = 0.5*LightPos.xy + vec2(0.5);
    vec2 Moments = texture(VarianceShadowMap, Uv).xy;
    
    float Mean = Moments.x;
    float VarianceSq = Moments.y - Moments.x * Moments.x;

    // NOTE: We add this to reduce precision errors in variance
    //VarianceSq = max(VarianceSq, 0.00001);
    
    float LitFactor = float(LightPos.z <= Mean);
    float DepthDifference = LightPos.z - Mean;
    float PMax = VarianceSq / (VarianceSq + DepthDifference*DepthDifference);

    // NOTE: We add this to reduce light bleeding
    PMax = LineStep(0.4, 1, PMax);
    
    Occlusion = max(LitFactor, PMax);
    
    return Occlusion;
}

void main()
{
    vec3 CameraPos = SceneBuffer.CameraPos;
    
    vec4 TexelColor = texture(ColorTexture, InUv);
    vec3 SurfacePos = InWorldPos;
    vec3 SurfaceNormal = normalize(InWorldNormal);
    vec3 SurfaceColor = vec3(1, 1, 1); //TexelColor.rgb;
    vec3 View = normalize(CameraPos - SurfacePos);
    vec3 Color = vec3(0);

#if 0
    // NOTE: Calculate lighting for point lights
    for (int i = 0; i < SceneBuffer.NumPointLights; ++i)
    {
        point_light CurrLight = PointLights[i];
        vec3 LightDir = normalize(CurrLight.Pos - SurfacePos);
        Color += BlinnPhongLighting(View, SurfaceColor, SurfaceNormal, 32, LightDir, PointLightAttenuate(SurfacePos, CurrLight));
    }
#endif
    
    // NOTE: Calculate lighting for directional lights
    {
#if STANDARD
        float Occlusion = DirLightOcclusionStandardGet(SurfaceNormal, DirectionalLight.Dir, InDirLightPos);
#endif
#if PCF
        float Occlusion = DirLightOcclusionPcfGet(SurfaceNormal, DirectionalLight.Dir, InDirLightPos);
#endif
#if VARIANCE
        float Occlusion = DirLightOcclusionVarianceGet(SurfaceNormal, DirectionalLight.Dir, InDirLightPos);
#endif
        Color += Occlusion*BlinnPhongLighting(View, SurfaceColor, SurfaceNormal, 32, DirectionalLight.Dir, DirectionalLight.Color);
        Color += DirectionalLight.AmbientLight;
    }

    OutColor = vec4(Color, 1);
}

#endif
