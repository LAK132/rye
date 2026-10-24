struct VSInput
{
	float2 position : position;
	float2 texCoord : texCoord;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

uniform row_major float4x4 view_proj;
uniform float4 image_uv;

VSOutput main(VSInput IN)
{
	VSOutput OUT;

	OUT.position = mul(float4(IN.position, 0.0f, 1.0f), view_proj);
	OUT.texCoord = lerp(image_uv.xy, image_uv.zw, IN.texCoord);

	return OUT;
}
