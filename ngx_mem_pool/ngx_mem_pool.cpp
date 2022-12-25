#include "ngx_mem_pool.hpp"

NgxMemPool::NgxMemPool(size_t size) {
  pool_ = (NgxPool *)malloc(size);
  if (pool_ == nullptr) {
    return;
  }

  pool_->d_.last_ = (u_char *)pool_ + sizeof(NgxPool);  //* 内存池可用部分起始地址
  pool_->d_.end_ = (u_char *)pool_ + size;              //* 内存池可用部分末尾地址
  pool_->d_.next_ = nullptr;
  pool_->d_.failed_ = 0;

  size = size - sizeof(NgxPool);
  //* 小块内存不应超过一个页, max 取申请的内存大小和一个页大小中的较小值
  pool_->max_ = (size < NGX_MAX_ALLOC_FROM_POOL ? size : NGX_MAX_ALLOC_FROM_POOL);

  pool_->current_ = pool_;
  pool_->large_ = nullptr;
  pool_->cleanup_ = nullptr;
}

NgxMemPool::~NgxMemPool() {
  NgxPool *p, *n;
  NgxPoolLarge *l;
  NgxPoolCleanup *c;

  //* 1. 第一步，释放在大块内存的对象中申请的外部资源
  //* 大块内存中的对象可能会占用外部资源，比如某个对象存储了一个指针，这个指针通过 malloc 开辟了
  //* 一块内存或者打开了某个资源，那么在释放内存池中的资源之前，应该将外部资源释放掉，类似 C++ 中的析构函数，
  //* 应该执行那个释放外部资源的函数(通过用户设置的回调函数 cleanup->handler 实现)
  for (c = pool_->cleanup_; c; c = c->next_) {
    if (c->handler_) {
      c->handler_(c->data_);
    }
  }

  //* 2. 第二步，释放大块内存
  for (l = pool_->large_; l; l = l->next_) {
    if (l->alloc_) {
      free(l->alloc_);
    }
  }

  //* 3. 第三步，清理小块内存 小块内存中存储了很多与大块内存相关的头信息，所以要最后清理
  for (p = pool_, n = pool_->d_.next_; /* void */; p = n, n = n->d_.next_) {
    free(p);
    if (n == nullptr) {
      break;
    }
  }
}

//* 分配指定 size 大小的内存池，申请的小块内存不能超过设定的 max
// void NgxMemPool::ngxCreatPool(size_t size) {
//   pool_ = (NgxPool *)malloc(size);
//   if (pool_ == nullptr) {
//     return;
//   }

//   pool_->d_.last_ = (u_char *)pool_ + sizeof(NgxPool);  //* 内存池可用部分起始地址
//   pool_->d_.end_ = (u_char *)pool_ + size;              //* 内存池可用部分末尾地址
//   pool_->d_.next_ = nullptr;
//   pool_->d_.failed_ = 0;

//   size = size - sizeof(NgxPool);
//   //* 小块内存不应超过一个页, max 取申请的内存大小和一个页大小中的较小值
//   pool_->max_ = (size < NGX_MAX_ALLOC_FROM_POOL ? size : NGX_MAX_ALLOC_FROM_POOL);

//   pool_->current_ = pool_;
//   pool_->large_ = nullptr;
//   pool_->cleanup_ = nullptr;
// }

//* 从内存池申请大小为 size 字节的内存，考虑内存字节对齐
void *NgxMemPool::ngxPalloc(size_t size) {
  if (size <= pool_->max_) {
    return ngxPallocSmall(size, 1);
  }
  return ngxPallocLarge(size);
}

//* 从内存池申请大小为 size 字节的内存，不考虑内存字节对齐
void *NgxMemPool::ngxPnalloc(size_t size) {
  if (size <= pool_->max_) {
    return ngxPallocSmall(size, 0);
  }
  return ngxPallocLarge(size);
}

//* 同 ngxPnalloc，但将内存初始化为 0
void *NgxMemPool::ngxPcalloc(size_t size) {
  void *p;
  p = ngxPalloc(size);
  if (p) {
    ngx_memzero(p, size);
  }

  return p;
}

//* 释放大块内存
void NgxMemPool::ngxPfree(void *p) {
  NgxPoolLarge  *l;

  for (l = pool_->large_; l; l = l->next_) {
    if (p == l->alloc_) {
        free(l->alloc_);
        l->alloc_ = nullptr;
        return;
    }
  }
}

//* 重置内存池
void NgxMemPool::ngxResetPool() {
  NgxPool *p;
  NgxPoolLarge *l;

  //* 遍历大块内存的内存头
  for (l = pool_->large_; l; l = l->next_) {
    //* 如果内存头中的大块内存不为空，释放掉
    if (l->alloc_) {
      free(l->alloc_);
    }
  }

  /*
  遍历小块内存的内存池，重置可用内存起始地址
  for (p = pool; p; p = p->d.next) {
    此处有问题，因为只有第一块内存中存储了完整的信息头，剩余内存块只存储了内存池头部信息的一部分
    p->d.last = (u_char *) p + sizeof(ngx_pool_t);
    p->d.failed = 0;
  }
  */

  //* 正确处理方式 start
  //* 遍历小块内存的内存池，复位可用内存的起始地址，此处并没有也不可以释放小块内存
  p = pool_;
  p->d_.last_ = (u_char *)p + sizeof(NgxPool);
  p->d_.failed_ = 0;
  for (p = p->d_.next_; p; p = p->d_.next_) {
      p->d_.last_ = (u_char *)p + sizeof(NgxPoolLarge);
      p->d_.failed_ = 0;
  }
  //* 正确处理方式 end

  pool_->current_ = pool_;
  pool_->large_ = nullptr;
}

//* 销毁内存池
// void NgxMemPool::ngxDestoryPool() {
//   NgxPool *p, *n;
//   NgxPoolLarge *l;
//   NgxPoolCleanup *c;

//   //* 1. 第一步，释放在大块内存的对象中申请的外部资源
//   //* 大块内存中的对象可能会占用外部资源，比如某个对象存储了一个指针，这个指针通过 malloc 开辟了
//   //* 一块内存或者打开了某个资源，那么在释放内存池中的资源之前，应该将外部资源释放掉，类似 C++ 中的析构函数，
//   //* 应该执行那个释放外部资源的函数(通过用户设置的回调函数 cleanup->handler 实现)
//   for (c = pool_->cleanup_; c; c = c->next_) {
//     if (c->handler_) {
//       c->handler_(c->data_);
//     }
//   }

//   //* 2. 第二步，释放大块内存
//   for (l = pool_->large_; l; l = l->next_) {
//     if (l->alloc_) {
//       free(l->alloc_);
//     }
//   }

//   //* 3. 第三步，清理小块内存 小块内存中存储了很多与大块内存相关的头信息，所以要最后清理
//   for (p = pool_, n = pool_->d_.next_; /* void */; p = n, n = n->d_.next_) {
//     free(p);
//     if (n == nullptr) {
//       break;
//     }
//   }
// }

//* 添加清理外部资源操作
NgxPoolCleanup *NgxMemPool::ngxCleanupAdd(size_t size) {
  NgxPoolCleanup *c;

  //* 在小块内存中开辟清理操作的头部信息
  c = (NgxPoolCleanup *)ngxPalloc(sizeof(NgxPoolCleanup));
  if (c == nullptr) {
    return nullptr;
  }

  if (size) {
    c->data_ = ngxPalloc(size);
    if (c->data_ == nullptr) {
      return nullptr;
    }
  } else {
    c->data_ = nullptr;
  }

  //* 将头部信息连接在链表上
  c->handler_ = nullptr;
  c->next_ = pool_->cleanup_;
  pool_->cleanup_ = c;

  //* 返回头信息的起始地址
  return c;
}

//* 小块内存分配
void *NgxMemPool::ngxPallocSmall(size_t size, ngx_uint align) {
  u_char *m;
  NgxPool *p;
  //* 从 current 指向的内存块分配内存
  p = pool_->current_;

  do {
    //* m 指向可分配内存的起始地址
    m = p->d_.last_;

    //* 如果考虑内存对齐，将 m 调整为 unsigned long 的整数倍
    if (align) {
      m = ngxAlignPtr(m, NGX_ALIGNMENT);
    }

    //* 判断可用 size 是否足够分配给当前要申请的 size
    if ((size_t) (p->d_.end_ - m) >= size) {
      p->d_.last_ = m + size;
      return m;
    }
    //* 如果可用内存不足以分配 size，切换下一个内存块
    p = p->d_.next_;

  } while (p);

  //* 无法从内存池中找到可分配 size 大小的内存块，申请新的内存块
  return ngxPallocBlock(size);
}

//* 分配新的小块内存池
void *NgxMemPool::ngxPallocBlock(size_t size) {
    u_char *m;
    size_t pSize;
    NgxPool *p, *newP;

    //* 计算当前内存块的总 size
    pSize = (size_t)(pool_->d_.end_ - (u_char *)pool_);

    //* 开辟一个相同大小的内存块， m 指向起始地址
    m = (u_char *)malloc(pSize);
    if (m == nullptr) {
        return nullptr;
    }

    //* newP 指向内存块起始地址
    newP = (NgxPool *) m;

    newP->d_.end_ = m + pSize;
    newP->d_.next_ = nullptr;
    newP->d_.failed_ = 0;

    //* 从第二个内存块开始，只需要存头部信息的一部分，即给用户分配内存的数据信息
    m += sizeof(NgxPoolData);
    //* 调整 m 到 unsigned long 的上邻近倍数
    m = ngxAlignPtr(m, NGX_ALIGNMENT);
    //* last 指向空闲内存起始地址，从 m 到 m + size 将被分配出去
    newP->d_.last_ = m + size;

    //* 如果在当前遍历的内存块上申请内存失败的次数大于 4，代表该内存块可使用空间已消耗殆尽，需更换申请对象
    for (p = pool_->current_; p->d_.next_; p = p->d_.next_) {
        if (p->d_.failed_++ > 4) {
            pool_->current_ = p->d_.next_;
        }
    }

    //* 将新申请的内存块与传入的内存块连起来
    p->d_.next_ = newP;

    return m;
}

//* 大块内存分配
void *NgxMemPool::ngxPallocLarge(size_t size) {
    void *p;
    ngx_uint n;
    NgxPoolLarge *large;

    //* 通过 malloc 调用指定大小的大块内存
    p = malloc(size);
    if (p == nullptr) {
        return nullptr;
    }

    n = 0;

    //* 先遍历大块内存的内存头，将内存头中已经 free 的大块内存赋值为新开辟的大块内存
    for (large = pool_->large_; large; large = large->next_) {
        if (large->alloc_ == nullptr) {
            large->alloc_ = p;
            return p;
        }

        //* 考虑效率，不遍历太多次
        if (n++ > 3) {
            break;
        }
    }

    //* 大块内存的内存头在小块内存中开辟，内存头中的 alloc 指向大块内存的起始地址
    large = (NgxPoolLarge *)ngxPallocSmall(sizeof(NgxPoolLarge), 1);
    //* 如果内存头在小块内存中开辟失败，将刚刚通过 malloc 申请的大块内存 free 掉(对比小块内存不释放)
    if (large == nullptr) {
        free(p);
        return nullptr;
    }

    //* 记录大块内存的起始地址
    large->alloc_ = p;
    //* 头插法连接大块内存的内存头
    large->next_ = pool_->large_;
    pool_->large_ = large;

    return p;
}

