#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) buffer CurrentPositionBuffer{
    vec2 positions[];
};

layout(binding = 1) buffer CurrentVelocityBuffer{
    vec2 velocities[];
};

layout(binding = 2) buffer NewPositionBuffer{
    vec2 NewPositions[];
};

layout(binding = 3) buffer NewVelocityBuffer{
    vec2 NewVelocities[];
};

layout(push_constant) uniform PushConstant {
    float deltaTime;
    uint particleCount;
} pushConstant;

#define ATTRACTION_CONSTANT 0.000001

vec2 computeAttractorForce(vec2 position, vec2 position2) {
    vec2 diff = position2 - position;
    float distance = length(diff);
    if (distance > 0.0) {
        float forceMagnitude = ATTRACTION_CONSTANT / (distance * distance);
        return normalize(diff) * forceMagnitude;
    }
    return vec2(0.0, 0.0);
}

void main() {
    uint globalIndex = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x;
    if (globalIndex >= pushConstant.particleCount) {
        return;
    }
    vec2 position = positions[globalIndex];
    vec2 velocity = velocities[globalIndex];
    vec2 force = vec2(0.0, 0.0);
    for(uint i = 0; i < pushConstant.particleCount; i++) {
        if (i == globalIndex) {
            continue;
        }
        vec2 position2 = positions[i];
        force += computeAttractorForce(position, position2);
    }
    bool hitEdge = false;
    velocity += force * pushConstant.deltaTime;
    position += velocity * pushConstant.deltaTime;
    if (position.x < -1.0) {
        position.x = -1.0;
        hitEdge = true;
    } else if (position.x > 1.0) {
        position.x = 1.0;
        hitEdge = true;
    }
    if (position.y < -1.0) {
        position.y = -1.0;
        hitEdge = true;
    } else if (position.y > 1.0) {
        position.y = 1.0;
        hitEdge = true;
    }
    if (hitEdge) {
        velocity.x *= -0.005;
        velocity.y *= -0.005;
    }
    NewVelocities[globalIndex] = velocity;
    NewPositions[globalIndex] = position;
}
