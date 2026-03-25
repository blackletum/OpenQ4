#version 110

uniform vec4 uAlphaTexCoordS;
uniform vec4 uAlphaTexCoordT;
uniform vec2 uVertexAlphaParams;

varying vec2 vAlphaTexCoord;
varying float vVertexAlpha;

void main() {
	vec4 alphaTexCoord = vec4( gl_MultiTexCoord0.xy, 0.0, 1.0 );
	vAlphaTexCoord = vec2( dot( alphaTexCoord, uAlphaTexCoordS ), dot( alphaTexCoord, uAlphaTexCoordT ) );
	vVertexAlpha = clamp( gl_Color.a * uVertexAlphaParams.x + uVertexAlphaParams.y, 0.0, 1.0 );
	gl_Position = ftransform();
}
