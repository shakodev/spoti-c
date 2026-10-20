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
// Original procedural icons. Coordinates use a 24-unit square; no font or
// copied vector assets. These IDs match UiIcon in icons.h.
float segmentDistance(float2 p, float2 a, float2 b) {
    float2 edge = b - a;
    return length(p - a - edge * saturate(dot(p-a,edge) / dot(edge,edge)));
}
float boxDistance(float2 p, float2 center, float2 halfSize) {
    float2 q = abs(p-center)-halfSize;
    return length(max(q,0)) + min(max(q.x,q.y),0);
}
float cross2(float2 a, float2 b) { return a.x*b.y-a.y*b.x; }
float triangleDistance(float2 p, float2 a, float2 b, float2 c) {
    float d = min(segmentDistance(p,a,b),min(segmentDistance(p,b,c),segmentDistance(p,c,a)));
    float3 sides = float3(cross2(b-a,p-a),cross2(c-b,p-b),cross2(a-c,p-c));
    return (all(sides >= 0) || all(sides <= 0)) ? -d : d;
}
float speakerDistance(float2 p) {
    float d = boxDistance(p,float2(5,12),float2(2,3));
    d = min(d,triangleDistance(p,float2(6,9),float2(12,4),float2(12,20)));
    return min(d,triangleDistance(p,float2(6,9),float2(12,20),float2(6,15)));
}
float iconDistance(float2 p, uint icon) {
    float d = 100;
    if (icon == 1) {
        d = min(segmentDistance(p,float2(3.5,10),float2(12,3)),
                segmentDistance(p,float2(12,3),float2(20.5,10)));
        d = min(d,segmentDistance(p,float2(6,8.5),float2(6,20)));
        d = min(d,segmentDistance(p,float2(6,20),float2(10,20)));
        d = min(d,segmentDistance(p,float2(10,20),float2(10,14)));
        d = min(d,segmentDistance(p,float2(10,14),float2(14,14)));
        d = min(d,segmentDistance(p,float2(14,14),float2(14,20)));
        d = min(d,segmentDistance(p,float2(14,20),float2(18,20)));
        d = min(d,segmentDistance(p,float2(18,20),float2(18,8.5))) - 0.85;
    } else if (icon == 2) {
        d = abs(length(p-float2(10,10))-6)-0.9;
        d = min(d,segmentDistance(p,float2(14.5,14.5),float2(20.5,20.5))-1);
    } else if (icon == 3) {
        d = min(segmentDistance(p,float2(4,4),float2(4,20)),
                segmentDistance(p,float2(9,4),float2(9,20)));
        d = min(d,segmentDistance(p,float2(14,4),float2(19,20)))-1;
    } else if (icon == 4) {
        d = triangleDistance(p,float2(7,4),float2(20,12),float2(7,20));
    } else if (icon == 5) {
        d = min(boxDistance(p,float2(8,12),float2(2,8)),boxDistance(p,float2(16,12),float2(2,8)));
    } else if (icon == 6 || icon == 7) {
        if (icon == 7) { p.x = 24-p.x; }
        d = min(boxDistance(p,float2(5,12),float2(1,7)),
            triangleDistance(p,float2(18,5),float2(7,12),float2(18,19)));
    } else if (icon == 8 || icon == 9) {
        d = speakerDistance(p);
        if (icon == 8) {
            float rings = min(abs(length(p-float2(11,12))-5),abs(length(p-float2(11,12))-8))-0.8;
            d = min(d,max(rings,13.5-p.x));
        } else {
            float stroke = min(segmentDistance(p,float2(16,9),float2(21,15)),
                segmentDistance(p,float2(21,9),float2(16,15)))-0.85;
            d = min(d,stroke);
        }
    } else if (icon == 10) {
        d = min(length(p-float2(5,12)),min(length(p-float2(12,12)),length(p-float2(19,12))))-1.6;
    } else if (icon == 11) {
        d = min(segmentDistance(p,float2(4,12),float2(9,17)),
            segmentDistance(p,float2(9,17),float2(20,6)))-1;
    } else if (icon == 12) {
        d = min(segmentDistance(p,float2(5,5),float2(19,19)),
            segmentDistance(p,float2(19,5),float2(5,19)))-0.9;
    } else if (icon == 13) {
        d = min(segmentDistance(p,float2(4,12),float2(20,12)),
            segmentDistance(p,float2(12,4),float2(12,20)))-0.9;
    } else if (icon == 14 || icon == 15) {
        if (icon == 15) { p.x = 24-p.x; }
        d = min(segmentDistance(p,float2(15,5),float2(8,12)),
            segmentDistance(p,float2(8,12),float2(15,19)))-0.9;
    } else if (icon == 16) {
        d = abs(length(p-float2(12,12))-9)-0.85;
        d = min(d,boxDistance(p,float2(12,14),float2(0.9,3)));
        d = min(d,length(p-float2(12,7.5))-1);
    } else if (icon == 17) {
        d = abs(triangleDistance(p,float2(12,3),float2(22,20),float2(2,20)))-0.85;
        d = min(d,boxDistance(p,float2(12,11),float2(0.9,3)));
        d = min(d,length(p-float2(12,17))-1);
    }
    else if (icon == 26) {
        d=abs(length(p-float2(12,7.5))-3.5)-0.8;
        d=min(d,max(abs(length(p-float2(12,20))-7)-0.8,13-p.y));
    } else if (icon >= 18) {
        p *= 16.0 / 24.0;
        if (icon == 18) {
            d=min(segmentDistance(p,float2(2,4),float2(5,4)),segmentDistance(p,float2(5,4),float2(11,12)));
            d=min(d,segmentDistance(p,float2(11,12),float2(14,12)));
            d=min(d,segmentDistance(p,float2(2,12),float2(5,12)));
            d=min(d,segmentDistance(p,float2(5,12),float2(11,4)));
            d=min(d,segmentDistance(p,float2(11,4),float2(14,4)));
            d=min(d,segmentDistance(p,float2(12,2),float2(14,4)));
            d=min(d,segmentDistance(p,float2(12,6),float2(14,4)));
            d=min(d,segmentDistance(p,float2(12,10),float2(14,12)));
            d=min(d,segmentDistance(p,float2(12,14),float2(14,12)))-0.7;
        } else if (icon == 19 || icon == 20) {
            d=min(segmentDistance(p,float2(4,4),float2(13,4)),segmentDistance(p,float2(13,4),float2(13,9)));
            d=min(d,segmentDistance(p,float2(12,12),float2(3,12)));
            d=min(d,segmentDistance(p,float2(3,12),float2(3,7)));
            d=min(d,segmentDistance(p,float2(4,2),float2(2,4)));
            d=min(d,segmentDistance(p,float2(4,6),float2(2,4)));
            d=min(d,segmentDistance(p,float2(12,10),float2(14,12)));
            d=min(d,segmentDistance(p,float2(12,14),float2(14,12)));
            if (icon == 20) { d=min(d,segmentDistance(p,float2(8,6),float2(8,10))); }
            d-=0.7;
        } else if (icon == 21) {
            float2 q=abs(p-float2(8,6))-float2(2,3);
            d=abs(length(max(q,0))+min(max(q.x,q.y),0))-0.6;
            if (p.y>=7) { d=min(d,abs(length(p-float2(8,7))-4)-0.6); }
            d=min(d,segmentDistance(p,float2(8,11),float2(8,14))-0.7);
            d=min(d,segmentDistance(p,float2(5,14),float2(11,14))-0.7);
        } else if (icon == 22) {
            d=min(segmentDistance(p,float2(2,3),float2(14,3)),segmentDistance(p,float2(7,7),float2(14,7)));
            d=min(d,segmentDistance(p,float2(7,11),float2(14,11)))-0.7;
            d=min(d,triangleDistance(p,float2(2,7),float2(5,10),float2(2,13)));
        } else if (icon == 23 || icon == 24) {
            float2 q=abs(p-float2(7,7))-float2(5,4);
            d=abs(max(q.x,q.y))-0.6;
            if (icon == 23) {
                d=min(d,segmentDistance(p,float2(6,12),float2(9,12))-0.7);
                d=min(d,max(abs(p.x-13)-1.5,abs(p.y-10)-4));
            } else { d=min(d,max(abs(p.x-10)-3,abs(p.y-9)-2)); }
        } else {
            d=min(segmentDistance(p,float2(2,6),float2(2,2)),segmentDistance(p,float2(2,2),float2(6,2)));
            d=min(d,min(segmentDistance(p,float2(10,2),float2(14,2)),segmentDistance(p,float2(14,2),float2(14,6))));
            d=min(d,min(segmentDistance(p,float2(2,10),float2(2,14)),segmentDistance(p,float2(2,14),float2(6,14))));
            d=min(d,min(segmentDistance(p,float2(10,14),float2(14,14)),segmentDistance(p,float2(14,14),float2(14,10))))-0.7;
        }
        d *= 24.0 / 16.0;
    }
    return d;
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
    } else if (p.effect.x == 5) {
        float2 local = p.local * (24 / p.box.z);
        if (p.effect.z == 1) { local.x = 24-local.x; }
        d = iconDistance(local,(uint)p.effect.y) * (p.box.z / 24);
        coverage = saturate(0.5 - d * scale);
    }
    float4 color = p.color;
    if (p.effect.x == 3 || p.effect.x == 4) {
        float2 uv = lerp(p.uv.xy, p.uv.zw, p.local / p.box.zw);
        float4 texel = image.Sample(linearClamp, uv);
        if (p.effect.x == 4) { coverage *= texel.r; }
        else { color *= texel; }
    }
    color.a *= coverage;
    return float4(color.rgb * color.a, color.a);
}
