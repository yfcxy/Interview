#include "Shader.h"

IMPLEMENT_MATERIAL_SHADER_TYPE(, FDepthOnlyVS, TEXT("/Plugin/CustomMeshPass/Private/DepthOnlyVertexShader.usf"), TEXT("Main"), SF_Vertex);

IMPLEMENT_MATERIAL_SHADER_TYPE(, FDepthOnlyPS, TEXT("/Plugin/CustomMeshPass/Private/DepthOnlyPixelShader.usf"), TEXT("Main"), SF_Pixel);

IMPLEMENT_MATERIAL_SHADER_TYPE(, FCMPPS, TEXT("/Plugin/CustomMeshPass/Private/CMPPixelShader.usf"), TEXT("Main"), SF_Pixel);