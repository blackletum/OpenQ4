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

float EnhancedSpecularTerm( vec3 halfAngle, vec3 viewDir, vec3 localNormal, vec3 specularSample ) {
	float ndoth = max( dot( halfAngle, localNormal ), 0.0 );
	float ndotv = max( dot( viewDir, localNormal ), 0.0 );
	float gloss = clamp( max( max( specularSample.r, specularSample.g ), specularSample.b ), 0.0, 1.0 );
	float specularPower = mix( 10.0, 40.0, gloss );
	float fresnel = 1.0 + ( pow( 1.0 - ndotv, 5.0 ) * 2.0 * clamp( uMaterialFresnel, 0.0, 1.0 ) );
	return pow( ndoth, specularPower ) * max( uMaterialSpecularBoost, 0.0 ) * fresnel;
}

void main() {
	vec4 bumpSample = texture2D( uBumpMap, vBumpTexCoord );
	vec3 localNormal = DecodeLocalNormal( bumpSample );

	vec3 lightDir = SafeNormalize( vLightVector );
	float ndotl = max( dot( lightDir, localNormal ), 0.0 );

	vec3 light = vec3( ndotl );
	light *= texture2DProj( uLightFalloffMap, vLightFalloffTexCoord ).rgb;
	light *= texture2DProj( uLightProjectionMap, vLightProjectionTexCoord ).rgb;

	vec3 diffuse = texture2D( uDiffuseMap, vDiffuseTexCoord ).rgb * uDiffuseColor.rgb;

	vec3 specularSample = texture2D( uSpecularMap, vSpecularTexCoord ).rgb;
	vec3 halfAngle = SafeNormalize( vHalfAngleVector );
	vec3 viewDir = SafeNormalize( vViewVector );
	float specularTerm = EnhancedSpecularTerm( halfAngle, viewDir, localNormal, specularSample );
	vec3 specular = specularSample * uSpecularColor.rgb * specularTerm;

	vec3 color = ( diffuse + specular ) * light * vVertexColor;
	gl_FragColor = vec4( color, 0.0 );
}
