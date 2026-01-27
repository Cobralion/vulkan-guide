#version 450
#extension GL_EXT_buffer_reference : require // Enable buffer reference extension

layout(location = 0) out vec3 outColor;
layout(location = 1) out vec2 outUV;

struct Vertex {
	vec3 position;
	float uv_x;
	vec3 normal;
	float uv_y;
	vec4 color;
};

// defines VertexBuffer
layout(buffer_reference, std430) readonly buffer VertexBuffer // buffer_reference: device address, std430: alignement rules
{
	Vertex vertices[];
};

layout(push_constant) uniform constants
{
	mat4 render_matirx;
	VertexBuffer vertexBuffer;
} pushConstants;

void main()
{
	Vertex v = pushConstants.vertexBuffer.vertices[gl_VertexIndex]; // Access vertex data from device address, points a accessed with .
	gl_Position = pushConstants.render_matirx * vec4(v.position, 1.0);
	outColor = v.color.xyz;
	outUV = vec2(v.uv_x, v.uv_y);

}