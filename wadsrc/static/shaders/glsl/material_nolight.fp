
vec3 ProcessMaterialLight(Material material, vec3 color)
{
	return material.Base.rgb * clamp(color + desaturate(uDynLightColor + uLightProbeColor).rgb, 0.0, 1.4);
}
