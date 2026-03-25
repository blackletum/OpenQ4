#version 110

uniform sampler2D uAlphaMap;
uniform float uAlphaScale;
uniform float uOpacitySourceMode;
uniform float uTranslucentMinAlpha;

varying vec2 vAlphaTexCoord;
varying float vVertexAlpha;

float OpticalDepth( float alpha ) {
	return -log( max( 1.0 - clamp( alpha, 0.0, 0.999 ), 1.0e-4 ) );
}

float StageOpacity() {
	if ( uOpacitySourceMode > 1.5 ) {
		return clamp( uAlphaScale * vVertexAlpha, 0.0, 0.999 );
	}

	vec4 sampleColor = texture2D( uAlphaMap, vAlphaTexCoord );
	float opacity = sampleColor.a;
	if ( uOpacitySourceMode > 0.5 ) {
		opacity = max( opacity, dot( sampleColor.rgb, vec3( 0.2126, 0.7152, 0.0722 ) ) );
	}

	return clamp( opacity * uAlphaScale * vVertexAlpha, 0.0, 0.999 );
}

void main() {
	float alpha = StageOpacity();
	if ( alpha <= uTranslucentMinAlpha ) {
		discard;
	}

	float tau = OpticalDepth( alpha );
	float depth = clamp( gl_FragCoord.z, 0.0, 1.0 );
	float depth2 = depth * depth;
	gl_FragColor = vec4( tau, tau * depth, tau * depth2, tau * depth2 * depth );
}
