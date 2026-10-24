struct VSOutput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

Buffer<float4> display_image_buffer;
uniform uint2 image_size;

float3 get_pixel(int2 pixel)
{
	pixel = clamp(pixel, int2(0, 0), int2(image_size) - 1);
	return display_image_buffer[pixel.x + (pixel.y * image_size.x)].xyz;
}

float3 sample_pixel_linear(float2 uv)
{
	float2 coord = (uv * float2(image_size)) - 0.5f;
	int2 index = int2(floor(coord));
	float2 weight = frac(coord);

	float3 p00 = get_pixel(index);
	float3 p01 = get_pixel(index + int2(0, 1));
	float3 p10 = get_pixel(index + int2(1, 0));
	float3 p11 = get_pixel(index + int2(1, 1));

	float3 p0 = lerp(p00, p01, weight.y);
	float3 p1 = lerp(p10, p11, weight.y);
	float3 p = lerp(p0, p1, weight.x);

	return p;
}

float3 sample_pixel_nearest(float2 uv)
{
	return get_pixel(int2(floor(uv * float2(image_size))));
}

float4 main(VSOutput IN) : SV_Target
{
	float2 uv = IN.texCoord;
	if ((uv.x < 0) | (uv.x > 1) | (uv.y < 0) | (uv.y > 1)) discard;
	float3 p = sample_pixel_nearest(IN.texCoord);
	return float4(p, 1.0f);
}
