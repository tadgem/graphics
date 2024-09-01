#version 450 

#define VertexData \
	_VertexData { \
		noperspective float m_edgeDistance; \
		noperspective float m_size; \
		smooth vec4 m_color; \
	}

#define kAntialiasing 2.0

uniform mat4 uViewProjMatrix;
	
layout(location=0) in vec4 aPositionSize;
layout(location=1) in vec4 aColor;

out VertexData vData;

void main() 
{
    vData.m_color = aColor.abgr; // swizzle to correct endianness
	vData.m_color.a *= smoothstep(0.0, 1.0, aPositionSize.w / kAntialiasing);
    vData.m_size = max(aPositionSize.w, kAntialiasing);
    gl_Position = uViewProjMatrix * vec4(aPositionSize.xyz, 1.0);
}