vec4 ProcessTexel()
{
	return uObjectColor * getTexel(vTexCoord.st);
}
