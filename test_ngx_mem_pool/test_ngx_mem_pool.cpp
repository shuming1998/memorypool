#include "./ngx_mem_pool.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Data stData;
struct Data {
  char *ptr;
  FILE *pfile;
};

void func1(void *p) {
  char *p1 = (char *)p;
  printf("free ptr mem!\n");
  free(p);
}
void func2(void *pf) {
  FILE *pf1 = (FILE *)pf;
  printf("close file!\n");
  fclose(pf1);
}

int main() {
  // 512 - sizeof(ngx_pool_t) - 4095   =>   max
  NgxMemPool pool(512);

  void *p1 = pool.ngxPalloc(128); // 从小块内存池分配的
  if (p1 == NULL) {
    printf("ngx_palloc 128 bytes fail...\n");
    return -1;
  }

  stData *p2 = (stData *)pool.ngxPalloc(512); // 从大块内存池分配的
  if (p2 == NULL) {
    printf("ngx_palloc 512 bytes fail...\n");
    return -1;
  }

  //* 大块内存中的对象指向了外部资源
  p2->ptr = (char *)malloc(12);
  strcpy(p2->ptr, "hello world");
  p2->pfile = fopen("data.txt", "w");

  NgxPoolCleanup *c1 = pool.ngxCleanupAdd(sizeof(char *));
  c1->handler_ = func1;
  c1->data_ = p2->ptr;

  NgxPoolCleanup *c2 = pool.ngxCleanupAdd(sizeof(FILE *));
  c2->handler_ = func2;
  c2->data_ = p2->pfile;

  // pool.ngxDestoryPool(); // 1.调用所有的预置的清理函数 2.释放大块内存 3.释放小块内存池所有内存
  return 0;
}