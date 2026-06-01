#version 110

uniform sampler2D uAlphaMap;
uniform float uAlphaTestEnabled;
uniform float uAlphaRef;
uniform float uAlphaTestMode;
uniform float uAlphaScale;
uniform float uAlphaHashEnabled;
uniform float uAlphaHashStable;

varying vec2 vAlphaTexCoord;
varying vec3 vAlphaHashCoord;
varying float vShadowDepth;

float AlphaHashThreshold( vec2 fragmentCoord ) {
	vec2 texel = floor( fragmentCoord );
	return fract( 52.9829189 * fract( texel.x * 0.06711056 + texel.y * 0.00583715 ) );
}

float StableAlphaHashThreshold( vec3 hashCoord ) {
	vec3 texel = floor( hashCoord * 0.5 );
	return fract( 52.9829189 * fract( texel.x * 0.06711056 + texel.y * 0.00583715 + texel.z * 0.01327111 ) );
}

float AlphaCoverage( float alpha ) {
	float alphaRef = clamp( uAlphaRef, 0.0, 0.9999 );
	float scaledAlpha = clamp( alpha, 0.0, 1.0 );
	return clamp( ( scaledAlpha - alphaRef ) / max( 1.0 - alphaRef, 1.0e-4 ), 0.0, 1.0 );
}

bool AlphaTestPass( float alpha ) {
	if ( uAlphaTestMode < -0.5 ) {
		return alpha < uAlphaRef;
	}
	if ( abs( uAlphaTestMode ) <= 0.5 ) {
		return abs( alpha - uAlphaRef ) <= ( 0.5 / 255.0 );
	}
	return alpha > uAlphaRef;
}

void main() {
	if ( vShadowDepth <= 0.0 || vShadowDepth >= 1.0 ) {
		discard;
	}

	if ( uAlphaTestEnabled > 0.5 ) {
		float alpha = texture2D( uAlphaMap, vAlphaTexCoord ).a * uAlphaScale;
		if ( uAlphaHashEnabled > 0.5 && uAlphaTestMode > 0.5 ) {
			float coverage = AlphaCoverage( alpha );
			float threshold = ( uAlphaHashStable > 0.5 ) ? StableAlphaHashThreshold( vAlphaHashCoord ) : AlphaHashThreshold( gl_FragCoord.xy );
			if ( coverage <= 0.0 || coverage <= threshold ) {
				discard;
			}
		} else if ( !AlphaTestPass( alpha ) ) {
			discard;
		}
	}

	gl_FragDepth = clamp( vShadowDepth, 0.0, 1.0 );
	gl_FragColor = vec4( 0.0 );
}
