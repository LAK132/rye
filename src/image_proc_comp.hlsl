uniform Texture2D<float4> tex;
RWBuffer<float4> image_buffer;
RWBuffer<float4> display_image_buffer;
// RWTexture2D<float4> display_image_buffer;
uniform uint2 image_size;

uniform row_major float3x3 camera_to_XYZ;
uniform row_major float3x3 scene_to_XYZ;
uniform row_major float3x3 XYZ_to_scene;
uniform row_major float3x3 XYZ_to_display;
uniform row_major float3x3 camera_XYZ_to_scene_XYZ;
uniform row_major float3x3 user_adaptation_XYZ;
uniform row_major float3x3 scene_XYZ_to_display_XYZ;
uniform row_major float3x3 raw_white_balance;
uniform row_major float4x4 user_matrix;
uniform float3 scene_white_XYZ;
uniform float display_gamma;
uniform float exposure;
uniform float contrast;
uniform float lightness;
uniform float saturation;
uniform float hue;

float3 XYZ_to_RGB(float3 colour)
{
	static const float3x3 _XYZ_to_RGB = {
		 2.36461384f, -0.89654057f, -0.46807328f,
		-0.51516621f,  1.4264081f,   0.0887581f,
		 0.0052037f,  -0.01440816f,  1.00920446f};
	return mul(colour, _XYZ_to_RGB);
}

float3 xyY_to_XYZ(float3 colour)
{
	float Yy = colour.z / colour.y;
	float z = 1.0f - (colour.x + colour.y);
	return float3(Yy * colour.x, colour.z, Yy * z);
}

float3 XYZ_to_xyY(float3 colour)
{
	float sum = colour.x + colour.y + colour.z;
	return float3(colour.x / sum, colour.y / sum, colour.y);
}

float2 uv_to_xy(float2 colour)
{
	float divisor = (6.0f * colour.x) + (-16.0f * colour.y) + 12.0f;
	return float2(
		(9.0f * colour.x) / divisor,
		(4.0f * colour.y) / divisor);
}

float2 xy_to_uv(float2 colour)
{
	float divisor = (-2.0f * colour.x) + (12.0f * colour.y) + 3.0f;
	return float2(
		(4.0f * colour.x) / divisor,
		(9.0f * colour.y) / divisor);
}

float3 XYZ_to_uvY(float3 colour)
{
	return float3(xy_to_uv(XYZ_to_xyY(colour).xy), colour.y);
}

float3 uvY_to_XYZ(float3 colour)
{
	return xyY_to_XYZ(float3(uv_to_xy(colour.xy), colour.z));
}

static const float2 D50_xy = float2(0.34567f, 0.35850f);
static const float3 D50_XYZ = xyY_to_XYZ(float3(D50_xy, 1.0f));
static const float3 D50_uvY = XYZ_to_uvY(D50_XYZ);
static const float3 white_point_uvY = float3(0.2009f, 0.4610f, 1.0f);

float3 XYZ_to_Luv(float3 colour, float3 white_uvY)
{
	// http://www.brucelindbloom.com/index.html?Eqn_XYZ_to_Luv.html
	static const float e = 0.008856f;
	static const float k = 903.3f;

	colour = float3(xy_to_uv(XYZ_to_xyY(colour).xy), colour.y);

	// uvY_to_Luv
	float YYn = colour.z / white_uvY.z;
	float L = (YYn <= e)
		? (k * YYn)
		: ((116.0f * pow(YYn, 1.0f / 3.0f)) - 16.0f);
	float L13 = 13.0f * L;
	return float3(
		L,
		L13 * (colour.x - white_uvY.x),
		L13 * (colour.y - white_uvY.y));
}

float3 Luv_to_XYZ(float3 colour, float3 white_uvY)
{
	float u = ((colour.y / colour.x) / 13.0f) + white_uvY.x;
	float v = ((colour.z / colour.x) / 13.0f) + white_uvY.y;
	float Y = (colour.x <= 8.0f)
		? (pow(3.0f / 29.0f, 3.0f) * colour.x * white_uvY.z)
		: (pow((colour.x + 16.0f) / 116.0f, 3.0f) * white_uvY.z);
	return float3(
		((9.0f * u) / (4.0f * v)) * Y,
		Y,
		(((-3.0f * u) + (-20.0f * v) + 12.0f) / (4.0f * v)) * Y);
}

float3 LCh_to_Luv(float3 colour)
{
	return float3(
		colour.x,
		colour.y * cos(colour.z),
		colour.y * sin(colour.z));
}

float3 Luv_to_LCh(float3 colour)
{
	return float3(
		colour.x,
		sqrt(dot(colour.yz, colour.yz)),
		atan2(colour.z, colour.y));
}

float _half_sigmoid(float k, float t)
{
	return (k * t) / (1.0f + k - t);
}

float _sigmoid(float k, float t)
{
	return _half_sigmoid(k, min(abs(t), 1.0f)) * sign(t);
}

float sigmoid(float k, float t)
{
	k = (k < 0.0f) ? min(k, -1.0001f) : max(k, 0.0001f);
	return _sigmoid(k, t);
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
	if (any(thread_id.xy >= image_size)) return;

	float3 colour = tex.Load(int3(thread_id.xy, 0)).xyz;
	colour = mul(colour, raw_white_balance);
	float4 colour4 = mul(float4(colour, 1.0f), user_matrix);
	colour = colour4.xyz / colour4.w;
	colour = mul(colour, camera_to_XYZ);
	colour = mul(colour, user_adaptation_XYZ);
	colour = mul(colour, camera_XYZ_to_scene_XYZ);

	colour *= exp(exposure);

	colour = XYZ_to_Luv(colour, XYZ_to_uvY(scene_white_XYZ));
	colour = Luv_to_LCh(colour);

	colour.x /= 100.0f;
	// colour.x *= exp(exposure);
	colour.x = sigmoid(-10.0f / ((lightness / 10.0f) + 1.0f), colour.x);
	colour.x = (sigmoid(-10.0f / ((contrast / 10.0f) + 1.0f),
	                    (colour.x * 2.0f) - 1.0f) + 1.0f) / 2.0f;
	// colour.x = smoothstep(0.0f, 1.0f, colour.x);
	colour.x *= 100.0f;

	colour.y *= exp(saturation / 100.0f);

	// colour.z += radians(hue);

	colour = LCh_to_Luv(colour);
	colour = Luv_to_XYZ(colour, XYZ_to_uvY(scene_white_XYZ));

	image_buffer[(thread_id.y * image_size.x) + thread_id.x] =
		float4(mul(colour, XYZ_to_scene), 1.0f);

	colour = mul(colour, scene_XYZ_to_display_XYZ);

	colour = mul(colour, XYZ_to_display);

	// display_image_buffer[thread_id.xy] =
	display_image_buffer[(thread_id.y * image_size.x) + thread_id.x] =
		float4(pow(colour, 1.0f / display_gamma), 1.0f);
}
