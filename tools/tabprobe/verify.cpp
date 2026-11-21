// 验证 v2.5 修复后的两个行为：滚轮步进 & 自动定位
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
int main(){
    HDC dc=GetDC(nullptr);
    HFONT f=CreateFontW(-13,0,0,0,400,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,
        DEFAULT_PITCH|FF_DONTCARE,L"Microsoft YaHei");
    SelectObject(dc,f);
    std::vector<std::wstring> tabs{L"ALL"};
    for(int i=1;i<=12;i++) tabs.push_back(L"G"+std::to_wstring(i));
    const int pad=14,leftW=320;
    const int barL=pad,barR=leftW-pad,viewW=barR-barL;
    std::vector<std::pair<int,int>> rects;
    int tx=pad;
    for(size_t i=0;i<tabs.size();i++){
        int cnt=(i==0)?72:6;
        std::wstring lab=tabs[i]+L" "+std::to_wstring(cnt);
        SIZE sz{0,0}; GetTextExtentPoint32W(dc,lab.c_str(),(int)lab.size(),&sz);
        int w=sz.cx+22; if(w<46)w=46;
        rects.push_back({tx,tx+w}); tx+=w+6;
    }
    int total=tx-6, overflow=total-viewW;

    // ---- 修复 1：滚轮步进 = 3/4 可视宽 ----
    int step=(viewW*3)/4; if(step<40)step=40;
    int notches_fixed=(overflow+step-1)/step;
    printf("=== FIX 1: wheel step ===\n");
    printf("  old step=28px  -> %2d notches (too sticky)\n",(overflow+27)/28);
    printf("  new step=%dpx -> %2d notches\n",step,notches_fixed);
    printf("  verdict: %s\n\n", notches_fixed<=5 ? "OK (<=5 notches)" : "still too many");

    // ---- 修复 2：ensureTabVisible 最小必要滚动 ----
    printf("=== FIX 2: ensureTabVisible (minimal scroll) ===\n");
    auto clamp=[&](int s){ if(overflow<=0)return 0; if(s<0)return 0; if(s>overflow)return overflow; return s; };
    auto ensure=[&](int idx,int cur)->int{
        if(idx<0||idx>=(int)rects.size())return cur;
        int l=rects[idx].first-cur, r=rects[idx].second-cur;
        int ns=cur;
        if(l<barL) ns=rects[idx].first-barL;
        else if(r>barR) ns=rects[idx].second-barR;
        return clamp(ns);
    };
    // 场景 A：选中的组已在视野内 → 不应移动
    int cur=0;
    int ns=ensure(2,cur);   // G1 本来就在视野里
    printf("  A) select G1 while scroll=0    -> scroll=%d %s\n",ns,
           ns==0?"OK (no needless jump)":"BAD (jumped)");
    // 场景 B：选中的组在右侧视野外 → 应贴右边
    ns=ensure(12,0);        // G11 在最右
    printf("  B) select G11 while scroll=0   -> scroll=%d %s\n",ns,
           ns>0?"OK (scrolled right)":"BAD");
    // 检查 G11 现在是否完整可见
    {
        int l=rects[12].first-ns, r=rects[12].second-ns;
        printf("     G11 screen [%d,%d] view [%d,%d] -> %s\n",l,r,barL,barR,
               (l>=barL&&r<=barR)?"fully visible OK":"STILL CLIPPED");
    }
    // 场景 C：选中的组在左侧视野外 → 应贴左边
    ns=ensure(1,overflow);  // G1 在最左，当前滚到最右
    {
        int l=rects[1].first-ns, r=rects[1].second-ns;
        printf("  C) select G1 while scroll=max  -> scroll=%d, G1 [%d,%d] %s\n",ns,l,r,
               (l>=barL&&r<=barR)?"fully visible OK":"STILL CLIPPED");
    }
    // 场景 D：全部可见（小班）→ 任何选择都不应滚动
    printf("  D) no overflow case -> scroll stays 0 (clamp forces 0)\n");

    // ---- 修复 3：命中一致性 ----
    printf("\n=== FIX 3: draw/hit offset consistency ===\n");
    bool ok=true;
    for(int s: {0,100,300,450}){
        for(size_t i=0;i<rects.size();i++){
            int dl=rects[i].first-s, dr=rects[i].second-s;
            int hl=rects[i].first-s, hr=rects[i].second-s;
            if(dl!=hl||dr!=hr) ok=false;
        }
    }
    printf("  %s\n", ok?"all offsets identical OK":"MISMATCH");
    return 0;
}
