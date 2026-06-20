#version 110

uniform float uPointShadowFar;
uniform sampler2D uAlphaMap;
uniform float uAlphaRef;
uniform float uAlphaTestMode;
uniform float uAlphaScale;
uniform float uAlphaTestEnabled;
uniform float uAlphaHashEnabled;
uniform float uAlphaHashStable;
uniform float uPointShadowDepthMode;
uniform float uPointShadowDepthCompare;

varying vec3 vPointShadowVector;
varying vec2 vAlphaTexCoord;
varying vec3 vAlphaHashCoord;

vec2 PackDepth16( float depth ) {
	vec2 enc = fract( vec2( 1.0, 255.0 ) * clamp( depth, 0.0, 1.0 ) );
	enc.x -= enc.y * ( 1.0 / 255.0 );
	return enc;
}

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

	if ( uPointShadowFar <= 0.0 ) {
		discard;
	}
	float rawDepth = length( vPointShadowVector ) / uPointShadowFar;
	if ( rawDepth <= 0.0 || rawDepth >= 1.0 ) {
		discard;
	}
	float depth = clamp( rawDepth, 0.0, 1.0 );
	if ( uPointShadowDepthCompare > 0.5 ) {
		gl_FragDepth = depth;
	}
	if ( uPointShadowDepthMode > 0.5 ) {
		gl_FragColor = vec4( depth, depth, depth, 1.0 );
		return;
	}
	vec2 packedDepth = PackDepth16( depth );
	gl_FragColor = vec4( packedDepth, 0.0, 1.0 );
}
