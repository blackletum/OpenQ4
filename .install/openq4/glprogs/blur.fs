uniform sampler2D Scene;
uniform sampler2D DepthTex;
uniform vec2 invTexSize;
uniform float range;
uniform float focus;
uniform vec4 approachColor;
uniform float approachPercent;
uniform float distanceScale;

float LinearizeDepth( float depth, float zNear ) {
	float ndcDepth = depth * 2.0 - 1.0;
	float denom = 0.999 - ndcDepth;
	if ( denom < 0.001 ) {
		denom = 0.001;
	}

	return ( 2.0 * zNear ) / denom;
}

void main() {
	vec2 uv = gl_TexCoord[0].st;
	vec4 scene = texture2D( Scene, uv );
	float blurRadius = max( range, 0.0 );
	float focusBand = max( focus, 1.0 );
	float blurStrength = clamp( approachPercent, 0.0, 1.0 );
	float zNear = max( distanceScale, 0.25 );
	float focusDepth = texture2D( DepthTex, vec2( 0.5, 0.5 ) ).x;
	float focusDistance = ( focusDepth < 0.99999 ) ? LinearizeDepth( focusDepth, zNear ) : ( zNear + focusBand * 0.5 );
	float depth = texture2D( DepthTex, uv ).x;
	float viewDistance = ( depth < 0.99999 ) ? LinearizeDepth( depth, zNear ) : 4096.0;
	float centerDistance = distance( uv, vec2( 0.5, 0.5 ) );
	float edgeFallback = smoothstep( 0.25, 0.95, centerDistance ) * 0.18;
	float blurFactor = smoothstep( focusBand * 0.35, focusBand, abs( viewDistance - focusDistance ) );
	float blurAmount = clamp( max( blurFactor, edgeFallback ) * blurStrength, 0.0, 1.0 );

	vec2 tap1 = invTexSize * blurRadius;
	vec2 tap2 = tap1 * 2.0;
	vec4 blur = scene * 0.16;
	blur += texture2D( Scene, uv + vec2( tap1.x, 0.0 ) ) * 0.12;
	blur += texture2D( Scene, uv - vec2( tap1.x, 0.0 ) ) * 0.12;
	blur += texture2D( Scene, uv + vec2( 0.0, tap1.y ) ) * 0.12;
	blur += texture2D( Scene, uv - vec2( 0.0, tap1.y ) ) * 0.12;
	blur += texture2D( Scene, uv + vec2( tap1.x, tap1.y ) ) * 0.07;
	blur += texture2D( Scene, uv + vec2( tap1.x, -tap1.y ) ) * 0.07;
	blur += texture2D( Scene, uv + vec2( -tap1.x, tap1.y ) ) * 0.07;
	blur += texture2D( Scene, uv + vec2( -tap1.x, -tap1.y ) ) * 0.07;
	blur += texture2D( Scene, uv + vec2( tap2.x, 0.0 ) ) * 0.02;
	blur += texture2D( Scene, uv - vec2( tap2.x, 0.0 ) ) * 0.02;
	blur += texture2D( Scene, uv + vec2( 0.0, tap2.y ) ) * 0.02;
	blur += texture2D( Scene, uv - vec2( 0.0, tap2.y ) ) * 0.02;

	vec4 mixed = mix( scene, blur, blurAmount );
	float tintAmount = blurAmount * clamp( approachColor.a, 0.0, 1.0 ) * 0.18;
	mixed.rgb = mix( mixed.rgb, approachColor.rgb, tintAmount );
	mixed.a = scene.a;

	gl_FragColor = mixed;
}
