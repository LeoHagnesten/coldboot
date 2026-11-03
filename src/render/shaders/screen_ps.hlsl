cbuffer frame : register ( b0 ) {

	float4 source;
	float4 target;

};

Texture2D picture : register ( t0 );
SamplerState smooth : register ( s0 );

struct vertex_out {

	float4 position : SV_Position;
	float2 uv : TEXCOORD0;

};

float3 sharp ( float2 uv ) {

	float2 texel = uv * source.xy;
	float2 scale = max ( target.xy / source.xy, 1.0 );
	float2 base = floor ( texel );
	float2 inside = texel - base - 0.5;
	float2 region = 0.5 - 0.5 / scale;
	float2 blend = ( inside - clamp ( inside, -region, region ) ) * scale + 0.5;

	return picture.SampleLevel ( smooth, ( base + blend ) * source.zw, 0.0 ).rgb;

}

float2 curve ( float2 uv ) {

	float2 centered = uv * 2.0 - 1.0;

	centered *= 1.0 + centered.yx * centered.yx * float2 ( 0.028, 0.036 );

	return centered * 0.5 + 0.5;

}

float4 main ( vertex_out input ) : SV_Target {

	if ( target.w > 0.5 )
		return float4 ( picture.SampleLevel ( smooth, input.uv * source.xy * source.zw, 0.0 ).rgb, 1.0 );

	if ( target.z < 0.5 )
		return float4 ( sharp ( input.uv ), 1.0 );

	float2 uv = curve ( input.uv );

	if ( any ( uv < 0.0 ) || any ( uv > 1.0 ) )
		return float4 ( 0.0, 0.0, 0.0, 1.0 );

	float3 color = sharp ( uv );
	float row = frac ( uv.y * source.y ) - 0.5;
	float light = dot ( color, float3 ( 0.299, 0.587, 0.114 ) );
	float width = lerp ( 11.0, 5.0, light );
	float beam = lerp ( 0.30, 1.0, exp ( -row * row * width * 2.0 ) );
	uint stripe = ( uint ) input.position.x % 3u;
	float3 mask = float3 ( 0.88, 0.88, 0.88 );

	if ( stripe == 0 )
		mask.r = 1.08;
	else if ( stripe == 1 )
		mask.g = 1.08;
	else
		mask.b = 1.08;

	float edge = uv.x * uv.y * ( 1.0 - uv.x ) * ( 1.0 - uv.y ) * 16.0;
	float vignette = pow ( saturate ( edge ), 0.14 );
	float3 glow = color * color * 0.18;

	return float4 ( saturate ( ( color * beam * mask + glow ) * vignette * 1.32 ), 1.0 );

}