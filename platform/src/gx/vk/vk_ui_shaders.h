#pragma once

namespace gpu {

inline const char* kUiVsSrc = R"(#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aParams;

layout(push_constant) uniform PushConsts {
    mat4 uProj;
};

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec4 vParams;

void main() {
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
    vUv = aUv;
    vColor = aColor;
    vParams = aParams;
}
)";

inline const char* kUiFsSrc = R"(#version 450 core
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec4 vParams;

layout(set = 0, binding = 0) uniform sampler2D uFontTex;

layout(location = 0) out vec4 FragColor;

void main() {
    float mode = vParams.x;
    float param = vParams.y;
    float alpha = 1.0;

    if (mode > 0.5 && mode < 1.5) {
        float d = length(vUv);
        float fw = fwidth(d);
        if (fw < 0.001) fw = 0.005;
        float outer = smoothstep(1.0, 1.0 - fw, d);
        if (param > 0.0) {
            float inner = smoothstep(param - fw, param, d);
            alpha = outer * inner;
        } else {
            alpha = outer;
        }
    } else if (mode > 1.5 && mode < 2.5) {
        alpha = texture(uFontTex, vUv).r;
    } else if (mode > 2.5 && mode < 3.5) {
        float d = length(vUv);
        float fw = fwidth(d);
        if (fw < 0.001) fw = 0.005;
        float angle = atan(vUv.y, vUv.x) + param;
        float teeth = cos(8.0 * angle);
        float rMax = 0.74 + 0.22 * smoothstep(-0.25, 0.25, teeth);
        float outer = smoothstep(rMax, rMax - fw * 1.5, d);
        float inner = smoothstep(0.33 - fw * 1.5, 0.33, d);
        alpha = outer * inner;
    } else if (mode > 3.5 && mode < 4.5) {
        vec2 p = abs(vUv);
        float lineDist = abs(p.x - p.y) * 0.7071;
        float capDist = max(p.x, p.y);
        float fw = fwidth(lineDist);
        if (fw < 0.001) fw = 0.005;
        float line = smoothstep(0.16, 0.16 - fw, lineDist);
        float box = smoothstep(0.65, 0.65 - fw, capDist);
        alpha = line * box;
    } else if (mode > 4.5 && mode < 5.5) {
        float d = length(vUv);
        float a = atan(vUv.y, vUv.x) + param;
        float aFold = mod(a, 3.14159265);
        float normAngle = clamp((aFold - 0.25) / 2.35, 0.0, 1.0);
        float arcR = mix(0.42, 0.78, normAngle);
        float dArc = abs(d - arcR);
        float thick = mix(0.06, 0.16, normAngle);
        float fw = fwidth(dArc);
        if (fw < 0.001) fw = 0.005;
        float arcAlpha = smoothstep(thick, thick - fw * 1.5, dArc);
        float angleMask = smoothstep(0.12, 0.30, aFold) * smoothstep(2.80, 2.60, aFold);
        alpha = arcAlpha * angleMask;
    } else if (mode > 5.5 && mode < 6.5) {
        float ca = cos(param);
        float sa = sin(param);
        vec2 p = vec2(ca * vUv.x - sa * vUv.y, sa * vUv.x + ca * vUv.y);
        vec2 tip = vec2(0.30, 0.0);
        vec2 wing = vec2(-0.30, 0.52);
        vec2 seg = wing - tip;
        vec2 pt = vec2(p.x, abs(p.y)) - tip;
        float t = clamp(dot(pt, seg) / dot(seg, seg), 0.0, 1.0);
        float dLine = length(pt - seg * t);
        float fw = fwidth(dLine);
        if (fw < 0.001) fw = 0.005;
        float strokeW = 0.16;
        alpha = smoothstep(strokeW, strokeW - fw * 1.5, dLine);
    } else if (mode > 6.5 && mode < 7.5) {
        vec2 p = vUv;
        vec2 bBody = vec2(0.48, 0.32);
        vec2 pBody = p - vec2(0.0, 0.06);
        vec2 dBody = abs(pBody) - bBody;
        float distBody = length(max(dBody, 0.0)) + min(max(dBody.x, dBody.y), 0.0) - 0.10;

        vec2 bTop = vec2(0.18, 0.08);
        vec2 pTop = p - vec2(-0.16, -0.32);
        vec2 dTop = abs(pTop) - bTop;
        float distTop = length(max(dTop, 0.0)) + min(max(dTop.x, dTop.y), 0.0) - 0.05;

        float bodyUnion = min(distBody, distTop);
        float fw = fwidth(length(p));
        if (fw < 0.001) fw = 0.005;
        float fillBody = smoothstep(0.0, -fw * 1.5, bodyUnion);

        float dLens = length(p - vec2(0.0, 0.08));
        float lensInner = smoothstep(0.16 - fw, 0.16, dLens);
        float lensOuter = smoothstep(0.24, 0.24 - fw, dLens);
        float lensRing = lensInner * lensOuter;
        alpha = fillBody + lensRing;
    } else {
        if (param > 0.0) {
            vec2 p = abs(vUv);
            float aspect = vParams.z;
            vec2 b = (vParams.w > 0.5) ? vec2(1.0 - param, aspect - param) : vec2(aspect - param, 1.0 - param);
            vec2 d = p - b;
            float dist = length(max(d, 0.0)) - param;
            float fw = fwidth(dist);
            if (fw < 0.001) fw = 0.005;
            alpha = smoothstep(0.0, -fw, dist);
        }
    }

    if (alpha <= 0.002) discard;
    FragColor = vec4(vColor.rgb, vColor.a * alpha);
}
)";

}  // namespace gpu
