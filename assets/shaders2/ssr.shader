#version 450
#vert

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;

layout(location = 0) out vec2 oUV;

void main()
{
    oUV = aUV;
    gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);
}

#frag

layout (location = 0) in vec2 aUV;
layout (location = 0) out vec4 reflectionColor;

uniform sampler2D gNormal;
uniform sampler2D colorBuffer;
uniform sampler2D depthMap;
uniform sampler2D pbrMap;


uniform float SCR_WIDTH;
uniform float SCR_HEIGHT;
uniform mat4  invProjection;
uniform mat4  projection;
uniform mat4  rotation;

bool rayIsOutofScreen(vec2 ray) {
	return (ray.x > 1 || ray.y > 1 || ray.x < 0 || ray.y < 0) ? true : false;
}

vec3 TraceRay(vec3 rayPos, vec3 dir, int iterationCount){
	float sampleDepth;
	vec3 hitColor = vec3(0);
	bool hit = false;

	for(int i = 0; i < iterationCount; i++){
		rayPos += dir;
		if(rayIsOutofScreen(rayPos.xy)){
			break;
		}

		sampleDepth = texture(depthMap, rayPos.xy).r;
		float depthDif = rayPos.z - sampleDepth;
		if(depthDif >= 0 && depthDif < 0.00001){ //we have a hit
			hit = true;
			hitColor = texture(colorBuffer, rayPos.xy).rgb;
			break;
		}
	}
	return hitColor;
}

void main(){
	float maxRayDistance = 100.0;

	//View Space ray calculation
	vec3 pixelPositionTexture;
	pixelPositionTexture.xy = aUV;

	vec3 pbr_props = texture(pbrMap, pixelPositionTexture.xy).rgb;

	if(pbr_props.y > 0.75)
	{
		return;
	}

	vec3 normalView = (rotation * texture(gNormal, pixelPositionTexture.xy)).rgb;	
	float pixelDepth = texture(depthMap, pixelPositionTexture.xy).r;	// 0< <1
	pixelPositionTexture.z = pixelDepth;		
	vec4 positionView = invProjection *  vec4(pixelPositionTexture * 2 - vec3(1), 1);
	positionView /= positionView.w;
	vec3 reflectionView = normalize(reflect(positionView.xyz, normalView));
	if(reflectionView.z > 0){
		reflectionColor = vec4(0,0,0,1);
		return;
	}
	vec3 rayEndPositionView = positionView.xyz + reflectionView * maxRayDistance;


	//Texture Space ray calculation
	vec4 rayEndPositionTexture = projection * vec4(rayEndPositionView,1);
	rayEndPositionTexture /= rayEndPositionTexture.w;
	rayEndPositionTexture.xyz = (rayEndPositionTexture.xyz + vec3(1)) / 2.0f;
	vec3 rayDirectionTexture = rayEndPositionTexture.xyz - pixelPositionTexture;

	ivec2 screenSpaceStartPosition = ivec2(pixelPositionTexture.x * SCR_WIDTH, pixelPositionTexture.y * SCR_HEIGHT); 
	ivec2 screenSpaceEndPosition = ivec2(rayEndPositionTexture.x * SCR_WIDTH, rayEndPositionTexture.y * SCR_HEIGHT); 
	ivec2 screenSpaceDistance = screenSpaceEndPosition - screenSpaceStartPosition;
	int screenSpaceMaxDistance = max(abs(screenSpaceDistance.x), abs(screenSpaceDistance.y)) / 2;
	rayDirectionTexture /= max(screenSpaceMaxDistance, 0.001f);


	//trace the ray
	vec3 outColor = TraceRay(pixelPositionTexture, rayDirectionTexture, screenSpaceMaxDistance);
	reflectionColor = vec4(outColor, 1) * (1.0 - pbr_props.y);
}