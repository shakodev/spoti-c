cbuffer View : register(b0) { float2 surface; float scale; float reserved; };
Texture2D image : register(t0);
SamplerState linearClamp : register(s0);
struct Instance {
    float4 box : BOX; float4 color : COLOR; float4 uv : UV;
    float4 radii : RADII; float4 border : BORDER; float4 effect : EFFECT;
};
struct Pixel {
    float4 position : SV_Position;
    float2 local : LOCAL;
    nointerpolation float4 box : BOX;
    nointerpolation float4 color : COLOR;
    nointerpolation float4 uv : UV;
    nointerpolation float4 radii : RADII;
    nointerpolation float4 border : BORDER;
    nointerpolation float4 effect : EFFECT;
};
Pixel vs_main(Instance v, uint id : SV_VertexID) {
    const float2 corners[6] = {float2(0,0),float2(1,0),float2(0,1),
        float2(0,1),float2(1,0),float2(1,1)};
    float spread = v.effect.x == 2 && v.effect.y > 0 ? v.effect.y : 0.5 / scale;
    Pixel p;
    p.local = corners[id] * (v.box.zw + 2 * spread) - spread;
    float2 physical = (v.box.xy + p.local) * scale;
    p.position = float4(physical / surface * float2(2,-2) + float2(-1,1), 0, 1);
    p.box = v.box; p.color = v.color; p.uv = v.uv;
    p.radii = v.radii; p.border = v.border; p.effect = v.effect;
    return p;
}
float roundedDistance(float2 position, float2 size, float4 radii) {
    float2 p = position - size * 0.5;
    float r = p.y < 0 ? (p.x < 0 ? radii.x : radii.y) : (p.x < 0 ? radii.w : radii.z);
    r = min(r, min(size.x, size.y) * 0.5);
    float2 q = abs(p) - size * 0.5 + r;
    return length(max(q, 0)) + min(max(q.x,q.y),0) - r;
}
float4 ps_main(Pixel p) : SV_Target {
    float d = roundedDistance(p.local, p.box.zw, p.radii);
    float coverage = saturate(0.5 - d * scale);
    if (p.effect.x == 1) {
        float2 size = max(p.box.zw - p.border.xy - p.border.zw, 0);
        // border order: left, top, right, bottom
        float4 inset = float4(max(p.border.x,p.border.y),max(p.border.z,p.border.y),
            max(p.border.z,p.border.w),max(p.border.x,p.border.w));
        float inner = roundedDistance(p.local - p.border.xy, size, max(p.radii - inset,0));
        if (min(size.x,size.y) > 0) { coverage = max(coverage - saturate(0.5 - inner * scale),0); }
    } else if (p.effect.x == 2) {
        coverage = p.effect.y > 0 ? pow(saturate(1 - max(d,0) / p.effect.y),2) : coverage;
    }
    float4 color = p.color;
    if (p.effect.x >= 3) {
        float2 uv = lerp(p.uv.xy, p.uv.zw, p.local / p.box.zw);
        float4 texel = image.Sample(linearClamp, uv);
        if (p.effect.x == 4) { coverage *= texel.r; }
        else { color *= texel; }
    }
    color.a *= coverage;
    return float4(color.rgb * color.a, color.a);
}
