/* Reads records from the MAME reference script on stdin and writes block
   hashes: record = "DFKR" u32 frame u32 len, then len bytes.
   Output (argv[1]): per record u32 frame, u32 nblocks, nblocks x u64 FNV-1a
   of 64 KB blocks.  argv[2] (optional): directory for full dumps of frames
   listed in argv[3..]. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static uint64_t fnv(const unsigned char *p, size_t n){uint64_t h=1469598103934665603ULL;size_t i=0;
  for(;i+8<=n;i+=8){uint64_t w;memcpy(&w,p+i,8);h^=w;h*=0x9E3779B97F4A7C15ULL;h^=h>>29;}
  for(;i<n;i++){h^=p[i];h*=1099511628211ULL;} return h;}
int main(int argc,char**argv){
  FILE*o=fopen(argv[1],"wb"); if(!o) return 1;
  unsigned char hdr[12]; unsigned char*buf=NULL; size_t cap=0;
  while(fread(hdr,1,12,stdin)==12){
    uint32_t fr,len; memcpy(&fr,hdr+4,4); memcpy(&len,hdr+8,4);
    if(len>cap){buf=realloc(buf,len);cap=len;}
    if(fread(buf,1,len,stdin)!=len) break;
    uint32_t nb=(len+65535)/65536;
    fwrite(&fr,4,1,o); fwrite(&nb,4,1,o);
    for(uint32_t b=0;b<nb;b++){size_t n=len-b*65536u; if(n>65536)n=65536; uint64_t h=fnv(buf+b*65536u,n); fwrite(&h,8,1,o);}
    fflush(o);
    for(int i=3;i<argc;i++) if((uint32_t)atoi(argv[i])==fr){char fn[512];snprintf(fn,sizeof fn,"%s/d%05u.bin",argv[2],fr);FILE*d=fopen(fn,"wb");fwrite(buf,1,len,d);fclose(d);}
  }
  fclose(o); return 0;
}
