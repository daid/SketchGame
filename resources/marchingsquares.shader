[VERTEX]
attribute vec3 a_vertex;
attribute vec3 a_normal;
attribute vec2 a_uv;

uniform mat4 projection_matrix;
uniform mat4 camera_matrix;
uniform mat4 object_matrix;
uniform vec3 object_scale;

varying vec2 v_uv;

void main()
{
    gl_Position = projection_matrix * camera_matrix * object_matrix * vec4(a_vertex.xyz * object_scale, 1.0);
    v_uv = a_uv.xy;
}

[FRAGMENT]
uniform sampler2D texture_map;
uniform sampler2D tile_texture;
uniform vec4 color;

varying vec2 v_uv;

float m(float a, float b) {
    return a / (a - b + 1.0/255.0);
}

float lineSide(vec2 p, vec2 a, vec2 b)
{
    float d = (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);
    return d*-1000;
}

void main()
{
    const float TEXTURE_SIZE = 9.0;
    const float OFF = 1.0/TEXTURE_SIZE;
    vec4 ca = texture2D(texture_map, v_uv + vec2(0.0, 0.0));
    vec4 cb = texture2D(texture_map, v_uv + vec2(OFF, 0.0));
    vec4 cc = texture2D(texture_map, v_uv + vec2(0.0,-OFF));
    vec4 cd = texture2D(texture_map, v_uv + vec2(OFF,-OFF));
    float a = ca.r - 0.5;
    float b = cb.r - 0.5;
    float c = cc.r - 0.5;
    float d = cd.r - 0.5;
    vec2 p = fract(v_uv * TEXTURE_SIZE);
    p.y = 1.0 - p.y;

    int flags = 0;
    if (a >= 0.0) flags += 1;
    if (b >= 0.0) flags += 2;
    if (c >= 0.0) flags += 4;
    if (d >= 0.0) flags += 8;
    if(flags == 0) {
        gl_FragColor.a = 0.0;
    } else if (flags == 1) {
        gl_FragColor.a = lineSide(p, vec2(0.0,m(a,c)), vec2(m(a,b),0.0));
    } else if (flags == 2) {
        gl_FragColor.a = lineSide(p, vec2(1.0-m(b,a),0.0),vec2(1.0,m(b,d)));
    } else if (flags == 3) {
        gl_FragColor.a = lineSide(p, vec2(0.0,m(a,c)),vec2(1.0,m(b,d)));
    } else if (flags == 4) {
        gl_FragColor.a = lineSide(p, vec2(m(c,d),1.0),vec2(0.0,1.0-m(c,a)));
    } else if (flags == 5) {
        gl_FragColor.a = lineSide(p, vec2(m(c,d),1.0),vec2(m(a,b),0.0));
    } else if (flags == 6) {
        if (b*c >= a*d) {
            gl_FragColor.a = min(
                lineSide(p, vec2(1.0-m(b,a),0.0),vec2(0.0,1.0-m(c,a))),
                lineSide(p, vec2(m(c,d),1.0),vec2(1.0,m(b,d)))
            );
        } else {
            gl_FragColor.a = max(
                lineSide(p, vec2(1.0-m(b,a),0.0),vec2(1.0,m(b,d))),
                lineSide(p, vec2(m(c,d),1.0),vec2(0.0,1.0-m(c,a)))
            );
        }
    } else if (flags == 7) {
        gl_FragColor.a = lineSide(p, vec2(m(c,d),1.0),vec2(1.0,m(b,d)));
    } else if (flags == 8) {
        gl_FragColor.a = lineSide(p, vec2(1.0,1.0-m(d,b)),vec2(1.0-m(d,c),1.0));
    } else if (flags == 9) {
        if (a*d >= b*c) {
            gl_FragColor.a = min(
                lineSide(p, vec2(0.0,m(a,c)),vec2(1.0-m(d,c),1.0)),
                lineSide(p, vec2(1.0,1.0-m(d,b)),vec2(m(a,b),0.0))
            );
        } else {
            gl_FragColor.a = max(
                lineSide(p, vec2(0.0,m(a,c)),vec2(m(a,b),0.0)),
                lineSide(p, vec2(1.0,1.0-m(d,b)),vec2(1.0-m(d,c),1.0))
            );
        }
    } else if (flags == 10) {
        gl_FragColor.a = lineSide(p, vec2(1.0-m(b,a),0.0),vec2(1.0-m(d,c),1.0));
    } else if (flags == 11) {
        gl_FragColor.a = lineSide(p, vec2(0.0,m(a,c)),vec2(1.0-m(d,c),1.0));
    } else if (flags == 12) {
        gl_FragColor.a = lineSide(p, vec2(1.0,1.0-m(d,b)),vec2(0.0,1.0-m(c,a)));
    } else if (flags == 13) {
        gl_FragColor.a = lineSide(p, vec2(1.0,1.0-m(d,b)),vec2(m(a,b),0.0));
    } else if (flags == 14) {
        gl_FragColor.a = lineSide(p, vec2(1.0-m(b,a),0.0),vec2(0.0,1.0-m(c,a)));
    } else if (flags == 15) {
        gl_FragColor.a = 1.0;
    } else {
        gl_FragColor.rgb = vec3(1,0,0);
        gl_FragColor.a = 1.0;
    }
    float q = ca.g * (1.0-p.x) + cb.g * p.x;
    float r = cc.g * (1.0-p.x) + cd.g * p.x;
    float s = q * (1.0-p.y) + r * p.y;
    float tile_type = round(s * 255.0);
    q = a * (1.0-p.x) + b * p.x;
    r = c * (1.0-p.x) + d * p.x;
    s = q * (1.0-p.y) + r * p.y;
    float tile_type2 = 0.0;
    if (s > 0.25) tile_type2 = 0.5;

    vec2 texture_offset = vec2(0.5 * tile_type, tile_type2);
    gl_FragColor.rgb = texture2D(tile_texture, texture_offset + fract(v_uv * 8.0) * 0.5).rgb;
}
