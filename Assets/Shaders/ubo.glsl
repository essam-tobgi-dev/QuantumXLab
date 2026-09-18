// std140 mirrors of src/Graphics/Ubo.hpp. Bindings: 0 Frame, 1 Lights, 2 Material, 3 Object, 4 Selection.
layout(std140) uniform FrameUbo {
    mat4 uView;
    mat4 uProj;
    mat4 uViewProj;
    mat4 uLightViewProj;
    vec4 uCameraPos;     // xyz camera-relative origin (=0), w = time
    vec4 uViewport;      // w, h, 1/w, 1/h
    vec4 uFrameParams;   // exposure, shadow depth bias, shadow texel (uv), ortho flag
    vec4 uShadowParams;  // normal offset (world m), Poisson radius (texels), texel world size, unused
    vec4 uPostParams;    // environment intensity, IBL flag, SSAO strength, bloom strength
};
struct DirLight { vec4 direction; vec4 color; };
struct PointLight { vec4 position; vec4 color; };
layout(std140) uniform LightsUbo {
    DirLight uSun;
    PointLight uPoints[8];
    vec4 uAmbient;       // rgb, w = point count
};
layout(std140) uniform MaterialUbo {
    vec4 uBaseColor;
    vec4 uEmissive;      // rgb, w = strength
    vec4 uProps;         // metallic, roughness, ao, colormapMix
    vec4 uMisc;          // colormapValue, unlit flag
    vec4 uTex;           // uv scale, triplanar flag, normal strength, textured flag
    vec4 uTexGain;       // rgb 1/mean(albedo), w 1/mean(roughness)
};
layout(std140) uniform ObjectUbo {
    mat4 uModel;
    mat4 uNormal;
    uvec4 uObjectId;     // x = id, y = flags (bit0 receiveShadow)
};
layout(std140) uniform SelectionUbo {
    uvec4 uSelected;     // x selected, y hovered
    vec4 uOutlineColor;
    vec4 uHoverColor;
    vec4 uSelParams;     // x outline width px
};
