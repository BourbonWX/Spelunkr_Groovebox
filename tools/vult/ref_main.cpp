#include "ref_gen.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
int main(int c, char**v){
  int mode=atoi(v[1]); double cut=atof(v[2]), res=atof(v[3]);
  Ref__ctx_type_0 a; Ref__ctx_type_0_init(a);
  Ref__ctx_type_1 b; Ref__ctx_type_1_init(b);
  for(int i=0;i<8820;i++){ double ph=fmod(i*110.0/44100.0,1.0); float x=(float)(0.8*(2*ph-1));
    float y = mode==0 ? Ref_lad(a,x,(float)cut,(float)res) : Ref_comb(b,x,(float)cut,(float)res*0+0.5f,(float)res);
    printf("%.6f\n",y);}
}
