struct vertex_out {

	float4 position : SV_Position;
	float2 uv : TEXCOORD0;

};

vertex_out main ( uint id : SV_VertexID ) {

	vertex_out result;
	float2 uv = float2 ( ( id << 1 ) & 2, id & 2 );

	result.position = float4 ( uv * float2 ( 2.0, -2.0 ) + float2 ( -1.0, 1.0 ), 0.0, 1.0 );
	result.uv = uv;

	return result;

}