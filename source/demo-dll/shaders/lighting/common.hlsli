#ifndef __LIGHTING_COMMON_HLSLI_
#define __LIGHTING_COMMON_HLSLI_

#include "pbr.hlsli"
#include "mesh-material.h"

#if RAY_TRACING
	#define TEX_SAMPLE(t,s,uv) (t.SampleLevel(s, uv, 0))
#else
	#define TEX_SAMPLE(t,s,uv) (t.Sample(s,uv))
#endif

namespace Light
{
	enum Type
	{
		Directional,
		Point,
		Spot
	};
}

struct FMaterialProperties
{
	float3 emissive;
	float3 basecolor;
	bool bHasNormalmap;
	float3 normalmap;
	float metallic;
	float roughness;
	float ao;
	float aoblend;
	float transmission;
	float clearcoat;
	float clearcoatRoughness;
	bool bHasClearcoatNormalmap;
	float3 clearcoatNormalmap;
};

struct FLight
{
	Light::Type type;
	float3 positionOrDirection;
	float intensity;
	bool shadowcasting;
};

FMaterialProperties EvaluateMaterialProperties(FMaterial mat, float2 uv, SamplerState s)
{
	FMaterialProperties output;

#if !RAY_TRACING
	// Alpha clip
	if (mat.m_baseColorTextureIndex != -1)
	{
		Texture2D baseColorTex = ResourceDescriptorHeap[mat.m_baseColorTextureIndex];
		float alpha = TEX_SAMPLE(baseColorTex, s, uv).a;
		clip(alpha - 0.5);
	}
#endif

	// Emissive
	output.emissive = mat.m_emissiveFactor;
	if (mat.m_emissiveTextureIndex != -1)
	{
		Texture2D emissiveTex = ResourceDescriptorHeap[mat.m_emissiveTextureIndex];
		output.emissive *= TEX_SAMPLE(emissiveTex, s, uv).rgb;
	}

	// Base Color
	output.basecolor = mat.m_baseColorFactor;
	if (mat.m_baseColorTextureIndex != -1)
	{
		Texture2D baseColorTex = ResourceDescriptorHeap[mat.m_baseColorTextureIndex];
		output.basecolor *= TEX_SAMPLE(baseColorTex, s, uv).rgb;
	}

	// Normalmap
	output.bHasNormalmap = mat.m_normalTextureIndex != -1;
	if (output.bHasNormalmap)
	{
		Texture2D normalmapTex = ResourceDescriptorHeap[mat.m_normalTextureIndex];
		float2 normalXY = TEX_SAMPLE(normalmapTex, s, uv).rg;
		float normalZ = sqrt(1.f - dot(normalXY, normalXY));
		output.normalmap = float3(normalXY, normalZ);
	}

	// Metallic/Roughness
	// GLTF specifies metalness in blue channel and roughness in green channel but we swizzle them on import and
	// use a BC5 texture. So, metalness ends up in the red channel and roughness stays on the green channel.
	output.metallic = mat.m_metallicFactor;
	output.roughness = mat.m_roughnessFactor;
	if (mat.m_metallicRoughnessTextureIndex != -1)
	{
		Texture2D metallicRoughnessTex = ResourceDescriptorHeap[mat.m_metallicRoughnessTextureIndex];
		float2 metallicRoughnessMap = TEX_SAMPLE(metallicRoughnessTex, s, uv).rg;
		output.metallic = metallicRoughnessMap.x;
		output.roughness = metallicRoughnessMap.y;
	}

	// Ambient Occlusion
	output.aoblend = mat.m_aoStrength;
	output.ao = 1.f;
	if (mat.m_aoTextureIndex != -1)
	{
		Texture2D aoTex = ResourceDescriptorHeap[mat.m_aoTextureIndex];
		output.ao = TEX_SAMPLE(aoTex, s, uv).r;
	}

	// Transmission
	output.transmission = mat.m_transmissionFactor;
	if (mat.m_transmissionTextureIndex != -1)
	{
		Texture2D transmissionTex = ResourceDescriptorHeap[mat.m_transmissionTextureIndex];
		output.transmission *= TEX_SAMPLE(transmissionTex, s, uv).r;
	}

	// Clearcoat
	output.clearcoat = mat.m_clearcoatFactor;
	if (mat.m_clearcoatTextureIndex != -1)
	{
		Texture2D clearcoatTex = ResourceDescriptorHeap[mat.m_clearcoatTextureIndex];
		output.clearcoat *= TEX_SAMPLE(clearcoatTex, s, uv).r;
	}

	output.clearcoatRoughness = mat.m_clearcoatRoughnessFactor;
	if (mat.m_clearcoatRoughnessTextureIndex != -1)
	{
		Texture2D clearcoatRoughnessTex = ResourceDescriptorHeap[mat.m_clearcoatRoughnessTextureIndex];
		output.clearcoatRoughness *= TEX_SAMPLE(clearcoatRoughnessTex, s, uv).r;
	}

	output.bHasClearcoatNormalmap = mat.m_clearcoatNormalTextureIndex != -1;
	if (output.bHasClearcoatNormalmap)
	{
		Texture2D normalmapTex = ResourceDescriptorHeap[mat.m_clearcoatNormalTextureIndex];
		float2 normalXY = TEX_SAMPLE(normalmapTex, s, uv).rg;
		float normalZ = sqrt(1.f - dot(normalXY, normalXY));
		output.clearcoatNormalmap = float3(normalXY, normalZ);
	}

	return output;
}

float3 GetDirectRadiance(FLight light, float3 worldPos, FMaterialProperties matInfo, float3 N, float NoV, float NoH, float VoH, RaytracingAccelerationStructure sceneBvh)
{
	float3 F0 = matInfo.metallic * matInfo.basecolor + (1.f - matInfo.metallic) * 0.04;
	float3 albedo = (1.f - matInfo.metallic) * matInfo.basecolor;
	float D = D_GGX(NoH, matInfo.roughness);
	float3 F = F_Schlick(VoH, F0);

	const float3 L = light.positionOrDirection;
	float lightVisibility = 1.f;
	float3 radiance = 0.f;

	if (light.shadowcasting)
	{
		RayDesc ray;
		ray.Origin = worldPos;
		ray.Direction = L;
		ray.TMin = 0.1f;
		ray.TMax = 1000.f;

		RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES> q;
		q.TraceRayInline(sceneBvh, RAY_FLAG_NONE, 0xff, ray);

		if (!q.Proceed())
		{
			// If Proceed() returns false, it means that traversal is complete. Check status and update light visibility.
			lightVisibility = (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.f : 1.f;
		}
		else
		{
			// This means that further evaluation is needed. For now, set light visibility to 0.f for non-opaque
			// triangles. In future, evaluate alpha map for non-opaque geo before setting visibility.
			lightVisibility = q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE ? 0.f : 1.f;
		}
	}

	if (lightVisibility > 0.f)
	{
		float NoL = saturate(dot(N, L));
		if (NoL > 0.f && NoV > 0.f)
		{
			float G = G_Smith_Direct(NoV, NoL, matInfo.roughness);

			// Diffuse & Specular BRDF
			float3 Fd = albedo * Fd_Lambert();
			float3 Fr = (D * F * G) / (4.f * NoV * NoL);

			float irradiance = light.intensity * NoL;
			radiance += (Fr + (1.f - F) * Fd) * irradiance * lightVisibility;
		}
	}

	return radiance;
}

#endif