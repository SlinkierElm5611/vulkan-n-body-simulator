#version 450

layout (location = 0) in vec2 inVertex;
layout (location = 1) in vec2 inPosition;

#define RADIUS 0.1f

void main() {
    gl_Position = vec4((inVertex + (RADIUS * inPosition)), 0.0, 1.0);
    }
