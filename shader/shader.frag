#version 330 core

in vec2 TexCoord;
in vec3 vNormal;
in vec3 FragPos;

out vec4 colour;

uniform vec3 uColor;          // color of the object (when no texture)
uniform int  useTexture;
uniform int  unlit;
uniform sampler2D texture1;

#define NUM_SPOTS 4
uniform vec3 spotPos[NUM_SPOTS];
uniform vec3 spotDir;
uniform vec3 spotColour;
uniform float spotInnerCos;
uniform float spotOuterCos;
uniform float spotRadius;
uniform vec3 ambientColour;

uniform vec3 headDir;
uniform vec3 headColour;
uniform float headScale;

uniform vec3 rugPos;
uniform vec3 rugColour;
uniform float rugInnerCos;
uniform float rugOuterCos;
uniform float rugRadius;

void main()
{
    vec3 base = (useTexture == 1) ? texture(texture1, TexCoord).rgb : uColor;
    vec3 N = normalize(vNormal);

    if(unlit == 1) {
        float k = 0.70 + 0.30 * max(dot(N, normalize(vec3(0.3, 1.0, 0.5))), 0.0);
        colour = vec4(base * k, 1.0);
        return;
    }

    vec3 total = vec3(0.0);
    for(int i=0; i<NUM_SPOTS; i++) {
        vec3 L = spotPos[i] - FragPos;
        float dist = length(L);
        L/=dist;

        float diff = max(dot(N,L), 0.0);

        float att = clamp(1.0-dist / spotRadius, 0.0, 1.0);
        att *= att;

        float theta = dot(-L , normalize(spotDir));
        float cone = smoothstep(spotOuterCos, spotInnerCos, theta);

        total += spotColour * diff * att *cone;
    }
    
    vec3 L = rugPos - FragPos;
    float dist = length(L);
    L/=dist;

    float diff = max(dot(N,L), 0.0);

    float att = clamp(1.0-dist / rugRadius, 0.0, 1.0);
    att *= att;

    float theta = dot(-L , normalize(spotDir));
    float cone = smoothstep(spotOuterCos, spotInnerCos, theta);

    total += rugColour * diff * att *cone;

    float headDiff = max(dot(N, -normalize(headDir)), 0.0);
    total += headColour * headScale * headDiff;

    
    colour = vec4(base * (ambientColour + total), 1.0);
}