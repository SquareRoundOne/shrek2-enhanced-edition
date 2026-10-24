#include "binkaudio.h"
#include <cstdio>
#include <random>
int main(){
  for (int N : {16, 1024}) {
    binkaudio::Dct3 d; d.init(N);
    std::mt19937 r(1); std::normal_distribution<float> g;
    std::vector<float> X(N), x(N); for(auto&v:X) v=g(r);
    x=X; d.run(x.data());
    double err=0, mx=0;
    for(int n=0;n<N;n++){ double s=X[0]/2; for(int k=1;k<N;k++) s+=X[k]*cos(M_PI*k*(2*n+1)/(2.0*N)); err=std::max(err,fabs(s-x[n])); mx=std::max(mx,fabs(s)); }
    printf("N=%d maxerr %g (max %g)\n",N,err,mx);
  }
}
