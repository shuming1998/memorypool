#ifndef NGX_MEM_POOL_H
#define NGX_MEM_POOL_H

#include <stdlib.h>
#include <memory.h>
#include <functional>

struct NgxPool;
struct NgxPoolLarge;

#ifndef NGX_ALIGNMENT
#define NGX_ALIGNMENT sizeof(unsigned long) //* 小块内存考虑内存字节对齐时的单位
#endif

using uintptr_t = long unsigned int;
using u_char = unsigned char;
using ngx_uint = unsigned int;
using size_t = unsigned long;

//* 将数值 d 调整为向上最邻近 a 的倍数
#define ngxAlign(d, a) (((d) + (a - 1)) & ~(a - 1))
//* 调整指针 p 为向上最邻近 a 的倍数
#define ngxAlignPtr(p, a) (u_char *)(((uintptr_t)(p) + ((uintptr_t)a - 1)) & ~((uintptr_t)a - 1))
//* 内存清零
#define ngx_memzero(buf, n) (void)memset(buf, 0, n)

//* 用于清理外部资源的函数指针
// typedef void (*NgxPoolCleanupPt)(void *data);
using NgxPoolCleanupPt = std::function<void(void *)>;

//* 清理操作的回调函数等相关数据
struct NgxPoolCleanup {
  NgxPoolCleanupPt  handler_;    //* 函数指针，保存清理函数的回调
  void              *data_;      //* 回调函数要回收的资源参数
  NgxPoolCleanup    *next_;      //* 用链表串接清理数据的
};

//* 大块内存的头部信息
struct NgxPoolLarge {
  NgxPoolLarge      *next_;      //* 用链表串接大块内存
  void              *alloc_;     //* 分配出去的大块内存的起始地址
};

//* 小块内存的头部信息
struct NgxPoolData {
  u_char            *last_;      //* 小块内存可使用部分的起始地址
  u_char            *end_;       //* 小块内存可使用部分的末尾地址
  NgxPool           *next_;      //* 用链表串接小块内存
  ngx_uint         failed_;     //* 记录向该内存块申请内存失败的次数
};

//* 内存池头部信息和资源管理信息
struct NgxPool {
  NgxPoolData       d_;          //* 当前小块内存的使用情况
  size_t            max_;        //* 当前小块内存的最大尺寸，与大块内存区分
  NgxPool           *current_;   //* 指向内存池可分配的第一个小块内存
  NgxPoolLarge      *large_;     //* 指向大块内存的入口地址
  NgxPoolCleanup    *cleanup_;   //* 所有清理操作的入口地址
};

const int ngxPageSize = 4096;                             //* 物理页大小 4k
const int NGX_MAX_ALLOC_FROM_POOL = ngxPageSize - 1;      //* ngx 小块内存可分配的最大空间
const int NGX_DEFAULT_POOL_SIZE = 16 * 1024;              //* 默认创建的内存池大小
const int NGX_POOL_ALLGNMENT = 16;                        //* 内存池对齐尺寸
const int NGX_MIN_POOL_SIZE = ngxAlign((sizeof(NgxPool) + 2 * sizeof(NgxPoolLarge))
                                  , NGX_POOL_ALLGNMENT);  //* 小块内存最小尺寸

class NgxMemPool {
public:
  NgxMemPool(size_t size);
  ~NgxMemPool();
  // void ngxCreatPool(size_t size);  //* 分配指定 size 大小的内存池，申请的小块内存不能超过设置的 max
  void *ngxPalloc(size_t size);     //* 从内存池申请大小为 size 字节的内存，考虑内存字节对齐
  void *ngxPnalloc(size_t size);    //* 从内存池申请大小为 size 字节的内存，不考虑内存字节对齐
  void *ngxPcalloc(size_t size);    //* 同 ngxPnalloc，但将内存初始化为 0
  void ngxPfree(void *p);           //* 释放大块内存
  void ngxResetPool();              //* 重置内存池
  // void ngxDestoryPool();            //* 销毁内存池
  NgxPoolCleanup *ngxCleanupAdd(size_t size);         //* 添加清理外部资源操作

private:
  void *ngxPallocSmall(size_t size, ngx_uint align); //* 小块内存分配
  void *ngxPallocLarge(size_t size);                  //* 大块内存分配
  void *ngxPallocBlock(size_t size);                  //* 分配新的小块内存池

  NgxPool *pool_;                   //* 指向 ngx 内存池入口的指针
};



#endif