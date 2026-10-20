// Original 16-unit procedural glyphs, shared at all DPI scales.
float segmentDistance(float2 p, float2 a, float2 b) {
    float2 v=b-a; return length(p-a-v*saturate(dot(p-a,v)/dot(v,v)));
}
float triangleDistance(float2 p, float2 a, float2 b, float2 c) {
    float d=min(segmentDistance(p,a,b),min(segmentDistance(p,b,c),segmentDistance(p,c,a)));
    float2 v0=b-a, v1=c-b, v2=a-c;
    float x=v0.x*(p-a).y-v0.y*(p-a).x;
    float y=v1.x*(p-b).y-v1.y*(p-b).x;
    float z=v2.x*(p-c).y-v2.y*(p-c).x;
    return (min(x,min(y,z))>=0 || max(x,max(y,z))<=0) ? -d : d;
}
float lineIcon(float2 p,float2 a,float2 b) { return segmentDistance(p,a,b)-0.7; }
float iconDistance(float2 p, int icon) {
    float d=100;
    if (icon==0) return triangleDistance(p,float2(5,3),float2(13,8),float2(5,13));
    if (icon==1) return min(max(abs(p.x-5)-1.5,abs(p.y-8)-5),max(abs(p.x-11)-1.5,abs(p.y-8)-5));
    if (icon==2 || icon==3) {
        if(icon==2) p.x=16-p.x;
        return min(triangleDistance(p,float2(3,3),float2(11,8),float2(3,13)),
            max(abs(p.x-12.5)-1,abs(p.y-8)-5));
    }
    if (icon==4) {
        d=min(lineIcon(p,float2(2,4),float2(5,4)),lineIcon(p,float2(5,4),float2(11,12)));
        d=min(d,lineIcon(p,float2(11,12),float2(14,12)));
        d=min(d,lineIcon(p,float2(2,12),float2(5,12)));
        d=min(d,lineIcon(p,float2(5,12),float2(11,4)));
        d=min(d,lineIcon(p,float2(11,4),float2(14,4)));
        d=min(d,lineIcon(p,float2(12,2),float2(14,4)));
        d=min(d,lineIcon(p,float2(12,6),float2(14,4)));
        d=min(d,lineIcon(p,float2(12,10),float2(14,12)));
        return min(d,lineIcon(p,float2(12,14),float2(14,12)));
    }
    if(icon==5 || icon==6) {
        d=min(lineIcon(p,float2(4,4),float2(13,4)),lineIcon(p,float2(13,4),float2(13,9)));
        d=min(d,lineIcon(p,float2(12,12),float2(3,12)));
        d=min(d,lineIcon(p,float2(3,12),float2(3,7)));
        d=min(d,lineIcon(p,float2(4,2),float2(2,4)));
        d=min(d,lineIcon(p,float2(4,6),float2(2,4)));
        d=min(d,lineIcon(p,float2(12,10),float2(14,12)));
        d=min(d,lineIcon(p,float2(12,14),float2(14,12)));
        if(icon==6) d=min(d,lineIcon(p,float2(8,6),float2(8,10)));
        return d;
    }
    if(icon==7 || icon==8) {
        d=min(max(abs(p.x-4)-2,abs(p.y-8)-2),triangleDistance(p,float2(5,6),float2(9,3),float2(9,13)));
        if(icon==8) return min(d,min(lineIcon(p,float2(11,6),float2(15,10)),lineIcon(p,float2(11,10),float2(15,6))));
        float r=length(p-float2(8,8));
        if(p.x>=10) d=min(d,abs(r-5.5)-0.65);
        return d;
    }
    if(icon==9) {
        float2 q=abs(p-float2(8,6))-float2(2,3);
        d=abs(length(max(q,0))+min(max(q.x,q.y),0))-0.6;
        if(p.y>=7) d=min(d,abs(length(p-float2(8,7))-4)-0.6);
        d=min(d,lineIcon(p,float2(8,11),float2(8,14)));
        return min(d,lineIcon(p,float2(5,14),float2(11,14)));
    }
    if(icon==10) {
        d=min(lineIcon(p,float2(2,3),float2(14,3)),lineIcon(p,float2(7,7),float2(14,7)));
        d=min(d,lineIcon(p,float2(7,11),float2(14,11)));
        return min(d,triangleDistance(p,float2(2,7),float2(5,10),float2(2,13)));
    }
    if(icon==11 || icon==12) {
        float2 q=abs(p-float2(7,7))-float2(5,4);
        d=abs(max(q.x,q.y))-0.6;
        if(icon==11) {
            d=min(d,lineIcon(p,float2(6,12),float2(9,12)));
            d=min(d,max(abs(p.x-13)-1.5,abs(p.y-10)-4));
        } else { d=min(d,max(abs(p.x-10)-3,abs(p.y-9)-2)); }
        return d;
    }
    d=min(lineIcon(p,float2(2,6),float2(2,2)),lineIcon(p,float2(2,2),float2(6,2)));
    d=min(d,min(lineIcon(p,float2(10,2),float2(14,2)),lineIcon(p,float2(14,2),float2(14,6))));
    d=min(d,min(lineIcon(p,float2(2,10),float2(2,14)),lineIcon(p,float2(2,14),float2(6,14))));
    return min(d,min(lineIcon(p,float2(10,14),float2(14,14)),lineIcon(p,float2(14,14),float2(14,10))));
}
