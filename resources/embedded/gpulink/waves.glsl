// Original procedural wave workload, distributed under the project's GPLv3.
// Fixed work per pixel; this is a link activation aid, not a benchmark.
void mainImage(out vec4 color, in vec2 coord) {
    vec2 uv = (coord * 2.0 - iResolution.xy) / iResolution.y;
    float wave = 0.0;
    vec2 p = uv;
    for (int i = 0; i < 48; ++i) {
        float f = float(i) + 1.0;
        p += 0.012 * vec2(sin(p.y * 7.0 + iTime), cos(p.x * 6.0 - iTime));
        wave += sin(p.x * f * 0.7 + sin(p.y * f + iTime * 0.4) + iTime) / f;
    }
    float shine = pow(0.5 + 0.5 * sin(wave * 2.0), 6.0);
    color = vec4(mix(vec3(0.01, 0.08, 0.15), vec3(0.25, 0.65, 0.8), shine), 1.0);
}
