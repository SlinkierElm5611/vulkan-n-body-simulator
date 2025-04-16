#version 450

layout (location = 0) in vec2 inVertex;
layout (location = 1) in vec2 inPosition;

#define RADIUS 0.005f

void main() {
    gl_Position = vec4(((RADIUS * inVertex) + inPosition), 0.0, 1.0);
    }
