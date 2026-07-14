#version 110

uniform sampler2D uBumpMap;
uniform sampler2D uLightFalloffMap;
uniform sampler2D uLightProjectionMap;
uniform sampler2D uDiffuseMap;
uniform sampler2D uSpecularMap;

uniform vec4 uDiffuseColor;
uniform vec4 uSpecularColor;
uniform float uMaterialNormalScale;
uniform float uMaterialSpecularBoost;
uniform float uMaterialFresnel;
uniform float uStockInteraction;
uniform float uAmbientLight;
uniform samplerCube uAmbientNormalMap;

varying vec2 vBumpTexCoord;
varying vec2 vDiffuseTexCoord;
varying vec2 vSpecularTexCoord;
varying vec4 vLightFalloffTexCoord;
varying vec4 vLightProjectionTexCoord;
varying vec3 vLightVector;
varying vec3 vHalfAngleVector;
varying vec3 vViewVector;
varying vec3 vVertexColor;

vec3 SafeNormalize( vec3 value ) {
	return value * inversesqrt( max( dot( value, value ), 1.0e-8 ) );
}

vec3 DecodeLocalNormal( vec4 bumpSample ) {
	vec2 localNormalXY = vec2( bumpSample.a, bumpSample.g ) * 2.0 - 1.0;
	localNormalXY *= max( uMaterialNormalScale, 0.0 );

	float xyLengthSq = dot( localNormalXY, localNormalXY );
	if ( xyLengthSq > 1.0 ) {
		localNormalXY *= inversesqrt( xyLengthSq );
		xyLengthSq = 1.0;
	}

	float encodedZ = max( bumpSample.b * 2.0 - 1.0, 0.0 );
	float reconstructedZ = sqrt( max( 1.0 - xyLengthSq, 0.0 ) );
	return SafeNormalize( vec3( localNormalXY, mix( encodedZ, reconstructedZ, 0.75 ) ) );
}

vec3 DecodeStockLocalNormal( vec4 bumpSample ) {
	return vec3( bumpSample.a, bumpSample.g, bumpSample.b ) * 2.0 - 1.0;
}

float EnhancedSpecularTerm( vec3 halfAngle, vec3 viewDir, vec3 localNormal, vec3 specularSample ) {
	float ndoth = max( dot( halfAngle, localNormal ), 0.0 );
	float ndotv = max( dot( viewDir, localNormal ), 0.0 );
	float gloss = clamp( max( max( specularSample.r, specularSample.g ), specularSample.b ), 0.0, 1.0 );
	float specularPower = mix( 10.0, 40.0, gloss );
	float fresnel = 1.0 + ( pow( 1.0 - ndotv, 5.0 ) * 2.0 * clamp( uMaterialFresnel, 0.0, 1.0 ) );
	return pow( ndoth, specularPower ) * max( uMaterialSpecularBoost, 0.0 ) * fresnel;
}

float StockSpecularTerm( vec3 halfAngle, vec3 localNormal ) {
	float specular = clamp( dot( halfAngle, localNormal ) * 4.0 - 3.0, 0.0, 1.0 );
	return specular * specular * 2.0;
}

void main() {
	vec4 bumpSample = texture2D( uBumpMap, vBumpTexCoord );
	vec3 localNormal = ( uStockInteraction > 0.5 )
		? DecodeStockLocalNormal( bumpSample )
		: DecodeLocalNormal( bumpSample );

	// Stock ambient lights replace the normalization cube with the generated
	// ambient cube. Sampling the actual image preserves its historical channel
	// packing and quantization instead of reconstructing a subtly different
	// direction from the renderer-side float.
	vec3 ambientLightDir = textureCube( uAmbientNormalMap, vec3( 0.0, 0.0, 1.0 ) ).rgb * 2.0 - 1.0;
	vec3 lightDir = ( uAmbientLight > 0.5 ) ? ambientLightDir : SafeNormalize( vLightVector );
	float ndotl = max( dot( lightDir, localNormal ), 0.0 );

	vec3 light = vec3( ndotl );
	light *= texture2DProj( uLightFalloffMap, vLightFalloffTexCoord ).rgb;
	light *= texture2DProj( uLightProjectionMap, vLightProjectionTexCoord ).rgb;

	vec3 diffuse = texture2D( uDiffuseMap, vDiffuseTexCoord ).rgb * uDiffuseColor.rgb;

	vec3 specularSample = texture2D( uSpecularMap, vSpecularTexCoord ).rgb;
	vec3 halfAngle = ( uStockInteraction > 0.5 ) ? vHalfAngleVector : SafeNormalize( vHalfAngleVector );
	vec3 viewDir = SafeNormalize( vViewVector );
	float specularTerm = ( uStockInteraction > 0.5 )
		? StockSpecularTerm( halfAngle, localNormal )
		: EnhancedSpecularTerm( halfAngle, viewDir, localNormal, specularSample );
	vec3 specular = specularSample * uSpecularColor.rgb * specularTerm;

	vec3 color = ( diffuse + specular ) * light * vVertexColor;
	gl_FragColor = vec4( color, 0.0 );
}
