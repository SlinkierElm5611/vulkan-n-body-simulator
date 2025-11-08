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

shared vec2 positionsShared[64];
shared vec2 velocitiesShared[64];

vec2 computeAttractorForce(vec2 position, vec2 position2) {
    vec2 diff = position2 - position;
    float distance = length(diff);
    if (distance > 0.01) {
        float forceMagnitude = ATTRACTION_CONSTANT / (distance * distance);
        return normalize(diff) * forceMagnitude;
    }
    return vec2(0.0, 0.0);
}

void main() {
    uint localIndex = gl_LocalInvocationID.x + gl_LocalInvocationID.y * gl_WorkGroupSize.x;
    uint globalIndex = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x;
    if (globalIndex >= pushConstant.particleCount) {
        return;
    }
    vec2 position = positions[globalIndex];
    vec2 velocity = velocities[globalIndex];
    vec2 force = vec2(0.0f);
    for(uint i = 0; i < pushConstant.particleCount/64 + 1; i++){
        positionsShared[localIndex] = positions[i * 64 + localIndex];
        velocitiesShared[localIndex] = velocities[i * 64 + localIndex];
        memoryBarrierShared();
        barrier();
        for(uint j = 0; j < 64; j++){
            uint index = i * 64 + j;
            if(index >= pushConstant.particleCount){
                break;
            }
            if (index == globalIndex) {
                continue;
            }
            vec2 position2 = positionsShared[j];
            force += computeAttractorForce(position, position2);
        }
    }
    velocity += force * pushConstant.deltaTime;
    position += velocity * pushConstant.deltaTime;
    NewVelocities[globalIndex] = velocity;
    NewPositions[globalIndex] = position;
}
