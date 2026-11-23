@vs rect_vs
in vec2 position;
in vec4 color;
out vec4 frag_color;
out vec2 Position;

layout(binding = 0) uniform rect_proj_uniform { 
    mat4 proj;
};

void main() {
    gl_Position = proj * vec4(vec2(position), 0.0, 1.0);
    frag_color = color;
    Position = vec2(position);
}
@end

@fs rect_fs
in vec4 frag_color;
in vec2 Position;
out vec4 out_color;

layout(binding = 1) uniform rect_radius_uniform {
    vec2 center;
    vec2 half_size;
    float radius;
};

void main() {
    vec2 local_pos = Position - center;
    vec2 q = abs(local_pos) - half_size + radius;
    float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
    float px = fwidth(dist) * 0.5;
    float alpha = 1.0 - smoothstep(-px, px, dist);
    out_color = vec4(frag_color.rgb, frag_color.a * alpha);
}
@end

@program rectangle rect_vs rect_fs

@vs img_vs
in vec2 position;
in vec2 coord;

out vec2 Position;
out vec2 uv;

layout(binding = 2) uniform img_proj {
    mat4 proj;
};

void main() {
    uv = coord;
    gl_Position = proj * vec4(vec2(position),0.0,1.0);
    Position = vec2(position);
}
@end

@fs img_fs
precision mediump float;

in vec2 Position;
in vec2 uv;

layout(binding = 0) uniform texture2D tex;
layout(binding = 0) uniform sampler smp;

layout(binding = 3) uniform img_radius_uniform {
    vec2 center;
    vec2 half_size;
    float radius;
};

layout(binding = 4) uniform img_params {
    vec4 tint;
};

out vec4 frag_color;

void main() {
    vec2 local_pos = Position - center;
    vec2 q = abs(local_pos) - half_size + radius;
    float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
    float px = fwidth(dist) * 0.5;
    float alpha = 1.0 - smoothstep(-px, px, dist);
    vec4 color = texture(sampler2D(tex, smp), uv);
    frag_color = vec4(color.rgb, color.a * tint.a * alpha);
}
@end

@program image img_vs img_fs

@fs text_fs
precision mediump float;

in vec2 Position;
in vec2 uv;

layout(binding = 0) uniform texture2D tex;
layout(binding = 0) uniform sampler smp;
layout(binding = 0) uniform text_params {
    vec4 color;
};

out vec4 frag_color;

void main() {
    float coverage = texture(sampler2D(tex, smp), uv).r;
    frag_color = vec4(color.rgb, color.a * coverage);
}
@end

@program text img_vs text_fs


@vs ring_vs
in vec2 position;

out vec2 Position;

layout(binding = 2) uniform img_proj {
    mat4 proj;
};

void main() {
    gl_Position = proj * vec4(position, 0.0, 1.0);
    Position = position;
}
@end

@fs ring_fs
in vec2 Position;

layout(binding = 0) uniform ring_params {
    vec4 color;
    vec4 track;
    vec4 geometry; // center.xy, radius of the stroke's centerline, half thickness
    vec4 arc;      // x: progress in [0, 1]
};

out vec4 frag_color;

const float TAU = 6.28318530718;

float coverage(float dist) {
    float px = max(fwidth(dist) * 0.5, 1e-4);
    return 1.0 - smoothstep(-px, px, dist);
}

void main() {
    vec2  p      = Position - geometry.xy;
    float radius = geometry.z;
    float half_t = geometry.w;
    float ring   = abs(length(p) - radius) - half_t;

    // Clockwise from 12 o'clock in y-down screen space.
    float angle = atan(p.x, -p.y);
    if (angle < 0.0)
        angle += TAU;

    float progress = arc.x;
    float arc_dist = 1e4;
    if (progress >= 1.0) {
        arc_dist = ring;
    } else if (progress > 0.0) {
        float end = progress * TAU;
        if (angle <= end) {
            arc_dist = ring;
        } else {
            // Round caps: past the end, distance falls back to the two cap discs.
            vec2 start_cap = vec2(0.0, -radius);
            vec2 end_cap   = radius * vec2(sin(end), -cos(end));
            arc_dist = min(length(p - start_cap), length(p - end_cap)) - half_t;
        }
    }

    float a_arc   = coverage(arc_dist) * color.a;
    float a_track = coverage(ring) * track.a;
    float alpha   = a_arc + a_track * (1.0 - a_arc);
    vec3  rgb     = alpha > 0.0 ? (color.rgb * a_arc + track.rgb * a_track * (1.0 - a_arc)) / alpha : vec3(0.0);
    frag_color = vec4(rgb, alpha);
}
@end

@program ring ring_vs ring_fs
