#ifndef SGI_STL_MEM_POOL
#define SGI_STL_MEM_POOL
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#define __THROW_BAD_ALLOC fprintf(stderr, "out of memory\n"); exit(1)

namespace sgi_stl {

extern void *memcpy (void *__restrict __dest, const void *__restrict __src,
		     size_t __n) __THROW __nonnull ((1, 2));

//* 封装 malloc 和 free，可设置 oom 释放内存的回调函数
template <int __inst>
class __malloc_alloc_template {
public:
  //! 内存池第一次malloc失败并且无法从后续chunk链表获取chunk时，二次调用
  static void *allocate(size_t __n) {
    void *__result = malloc(__n);
    if (0 == __result) __result = _S_oom_malloc(__n);
    return __result;
  }

  static void deallocate(void *__p, size_t /* __n */) {
    free(__p);
  }

  static void* reallocate(void *__p, size_t /* old_sz */, size_t __new_sz) {
    void* __result = realloc(__p, __new_sz);
    if (0 == __result) __result = _S_oom_realloc(__p, __new_sz);
    return __result;
  }

  static void (* __set_malloc_handler(void (*__f)()))() {
    void (* __old)() = __malloc_alloc_oom_handler;
    __malloc_alloc_oom_handler = __f;
    return(__old);
  }

private:
  static void *_S_oom_malloc(size_t);
  static void *_S_oom_realloc(void*, size_t);
  static void (* __malloc_alloc_oom_handler)();

};

template <int __inst>
void (* __malloc_alloc_template<__inst>::__malloc_alloc_oom_handler)() = nullptr;

template <int __inst>
void *__malloc_alloc_template<__inst>::_S_oom_malloc(size_t __n)
{
  void (* __my_malloc_handler)();
  void *__result;

  for (;;) {
    //* 将用户设置的回调函数指针赋值给 __my_malloc_handler
    __my_malloc_handler = __malloc_alloc_oom_handler;
    //* 如果用户没有设置回调函数，抛出异常
    if (0 == __my_malloc_handler) { __THROW_BAD_ALLOC; }
    //* 如果设置了，调用用户的回调，用户回调中一般会释放内存
    (*__my_malloc_handler)();
    //* 然后系统再次 malloc
    __result = malloc(__n);
    //* 如果仍然失败，在这个for循环中再次尝试，直到成功
    if (__result) return(__result);
  }
}

template <int __inst>
void *__malloc_alloc_template<__inst>::_S_oom_realloc(void *__p, size_t __n)
{
  void (* __my_malloc_handler)();
  void *__result;

  for (;;) {
    __my_malloc_handler = __malloc_alloc_oom_handler;
    if (0 == __my_malloc_handler) { __THROW_BAD_ALLOC; }
    (*__my_malloc_handler)();
    __result = realloc(__p, __n);
    if (__result) return(__result);
  }
}

typedef __malloc_alloc_template<0> malloc_alloc;

template<typename T>
class Allocator {
public:
  using value_type = T;

  constexpr Allocator() noexcept {}
  constexpr Allocator(const Allocator &) noexcept = default;
  template <class _Other>
  constexpr Allocator(const Allocator<_Other> &) noexcept {}

  //* 内存开辟
  T *allocate(long unsigned int __n) {
    __n = __n * sizeof(T);

    void *__ret = nullptr;

    //* 如果申请的不是小块内存(> 128 bytes)，仍采用默认的空间配置器(malloc、free管理)
    if (__n > (long unsigned int) _MAX_BYTES) {
      __ret = malloc_alloc::allocate(__n);
    } else {
      //* 二级指针_Obj **，申请的自由链表 = 存储自由链表的数组名(起始地址) + 申请字节数映射的链表位置
      _Obj *volatile * __my_free_list = _S_free_list + _S_freelist_index(__n);

      //* 操作链表时加锁，lock_guard 出作用域后锁自动析构
      std::lock_guard<std::mutex> guard(mtx);

      //* 对二级指针解引用，拿到指向的 _Obj * (头结点)
      _Obj *__result = *__my_free_list;

      if (__result == nullptr) {
        //*  如果 n 对应的链表为空，或者已经是最后一个节点的下个节点指针(即 0)，分配内存块，让 ret 指向新分配的首节点
        __ret = _S_refill(_S_round_up(__n));
      } else {
        //* 如果不为空，将头节点的下一个节点的地址，赋给 __my_free_list
        *__my_free_list = __result -> _M_free_list_link;
        //* 将刚才的节点分配出去，此时链表不为空，且 __my_free_list 指向的是下次可往外分配的节点
        //* 下一次申请该链表中的内存块时，再找到下一个节点的指针赋给 __my_free_list ，并将当前节点分配出去
        __ret = __result;
      }
    }

    return (T *)__ret;
  }

  //* 内存释放
  void deallocate(void *__p, long unsigned int __n) {
    //* 如果归还的内存大小大于128字节，说明不是内存池申请的，而是 malloc，所以应 free
    if (__n > (long unsigned int) _MAX_BYTES) {
      malloc_alloc::deallocate(__p, __n);
    } else {
      //* 将通过内存池申请的内存块 归还到内存池相应的位置中
      //* 首先定位该内存块对应的静态链表的二级指针
      _Obj *volatile *__my_free_list = _S_free_list + _S_freelist_index(__n);
      //* 找到链表后，定位该内存块在链表中对应的节点位置
      _Obj *__q = (_Obj *)__p;

      std::lock_guard<std::mutex> guard(mtx);

      //* 让要归还节点的 _M_free_list_link 指针指向当前链表中第一个空闲节点
      __q -> _M_free_list_link = *__my_free_list;
      //* 然后将当前链表的头指针指向要归还的节点，从之前指向的第一个空闲节点变成了要归还的节点
      //* 并且要归还的节点的 _M_free_list_link (next指针) 指向之前的第一个空闲节点
      *__my_free_list = __q;
      // lock is released here
    }
  }

  //* 内存扩容/缩容
  void *reallocate(void *__p, long unsigned int __old_sz, long unsigned int __new_sz) {
    void *__result;
    long unsigned int __copy_sz;

    //* 如果内存不是从内存池开辟的，直接调用 realloc
    if (__old_sz > (long unsigned int) _MAX_BYTES && __new_sz > (long unsigned int) _MAX_BYTES) {
      return(realloc(__p, __new_sz));
    }
    //* 如果新旧尺寸映射 8 的倍数后 chunk 大小相同，不用扩容或者缩容，直接返回
    if (_S_round_up(__old_sz) == _S_round_up(__new_sz)) {
      return(__p);
    }
    //* 以新尺寸开辟内存
    __result = allocate(__new_sz);
    //* 扩容就 copy __old_sz，缩容就 copy __new_sz
    __copy_sz = __new_sz > __old_sz ? __old_sz : __new_sz;
    memcpy(__result, __p, __copy_sz);
    //* 归还 __old_sz chunk块
    deallocate(__p, __old_sz);
    return(__result);
  }

  //* 对象构造
  void construct(T *__p, const T &val) {
    new (__p) T(val);
  }
  //* 对象析构
  void destory(T *__p) {
    __p->~T();
  }


private:
  enum {    _ALIGN = 8    };  //* 自由链表以 8 字节为对齐方式从 8 扩充到 128
  enum { _MAX_BYTES = 128 };  //* 内存池最大的 chunk 块
  enum { _NFREELISTS = 16 };  //* 自由链表的个数

  //* 每个 chunk 块的头信息
  union _Obj {
    union _Obj *_M_free_list_link;  //* 存储下一个 chunk 块的地址
    char _M_client_data[1];
  };

  //* 将 __bytes 上调为最邻近的 8 的倍数
  static long unsigned int _S_round_up(long unsigned int __bytes) {
    return (((__bytes) + (long unsigned int)_ALIGN - 1) & ~((long unsigned int)_ALIGN - 1));
  }

  //* 返回申请 __bytes 大小的内存块在自由链表中的索引
  static long unsigned int _S_freelist_index(long unsigned int __bytes) {
    return (((__bytes) + (long unsigned int)_ALIGN - 1) / ((long unsigned int)_ALIGN - 1));
  }

  //* 连接分配好的 chunk 块
  static void *_S_refill(long unsigned int __n) {
    //* 在当前链表中开辟的 chunk 块的个数
    int __nobjs = 20;
    //* 内存块开辟，返回起始地址
    char *__chunk = _S_chunk_alloc(__n, __nobjs);
    //* _Obj 的二级指针，用于遍历指针数组
    _Obj *volatile * __my_free_list;
    //* 定义局部变量，用于存储遍历过程中的节点
    _Obj *__result;
    _Obj *__current_obj;
    _Obj *__next_obj;
    int __i;

    //* 备用内存池只够申请 1 个新字节大小的内存块，就直接返回 chunk 块的起始地址
    if (1 == __nobjs) return(__chunk);

    //* 定位到申请 chunk 块字节数所映射的链表
    __my_free_list = _S_free_list + _S_freelist_index(__n);

    /* Build free list in chunk */
      __result = (_Obj *)__chunk;  //* 让 __result 也指向 __chunk指向的新申请的内存池的起始地址
      //* 让二级指针指向的 _Obj * 和 __next_obj 都指向新申请的内存池的起始地址的下一个节点，因为新申请内存
      //* 池的首节点要分配出去，此时指针数组的第 __my_free_list 个链表指针 元素的首节点还是未分配出去的空闲内存块
      *__my_free_list = __next_obj = (_Obj *)(__chunk + __n);
      //! for 循环让每个内存块的 M_free_list_link 指针(相当于next指针)指向下一个节点的地址，将节点连起来
      for (__i = 1; ; __i++) {
        //* 将 __next_obj 赋值给 __current_obj
        __current_obj = __next_obj;
        //* 让 __next_obj 指向下一个节点(转成char* 保证了指针每次加一个字节，+ __n 即加 n 个字节)
        __next_obj = (_Obj *)((char *)__next_obj + __n);
        if (__nobjs - 1 == __i) {
            //* 当前节点已经是最后一个节点了，下一个节点赋值为空指针
            __current_obj -> _M_free_list_link = nullptr;
            break;
        } else {
            __current_obj -> _M_free_list_link = __next_obj;
        }
      }
    //* 返回要分配出去的节点
    return(__result);
  }

  //* 分配自由链表的 chunk 块
  static char *_S_chunk_alloc(long unsigned int __size, int &__nobjs) {
    char *__result;
    long unsigned int __total_bytes = __size * __nobjs;  //* 计算内存池需要分配的字节数
    //* 计算剩余空闲字节数| 40块都用完时，_S_end_free 和 _S_start_free 都在末尾，结果为 0
    long unsigned int __bytes_left = _S_end_free - _S_start_free;
    //* 最初 __bytes_left = 0 - 0 = 0
    //* 第一次分配的所有 chunk 块用完时，再进来时： __bytes_left == __total_bytes
    if (__bytes_left >= __total_bytes) {
      //* 让 __result 指向即将被分配出去的 chunk 块
      __result = _S_start_free;
      //* 让 _S_start_free 指向第 21 个 chunk 块(第一次)
      //* 让 _S_start_free 指向第 40 个 chunk 块(第二次)
      _S_start_free += __total_bytes;
      //* 返回即将被分配出去的所有 chunk 块的头指针 __result，在 _S_refill 函数中将所有 chunk 块连起来
      return(__result);
    } else if (__bytes_left >= __size) {
      //* 申请其他大小的内存块时，先从备用的内存池中想办法，直接作为新字节大小的 chunk 块使用
      //* 计算一下备用内存池相当于几块新大小的 chunk 块
      __nobjs = (int)(__bytes_left/__size);
      __total_bytes = __size * __nobjs;
      __result = _S_start_free; //* __result 指向备用内存池的第一个 chunk 块(即原来第 21 个 chunk)
      //* ① _S_start_free 移动到 _S_end_free 处(正常情况下)
      //* ② _S_start_free 移动到以新大小划分的末尾处(备用内存池无法整除新 chunk 大小的情况下)
      _S_start_free += __total_bytes;
      //* 返回第一个 chunk 块的地址，然后在 _S_refill 函数中串起来所有 chunk
      //* 接着在 allocate 函数中将第一个 chunk 块分配出去，并将链表头指针指向下一个空闲 chunk
      return(__result);
    } else {
      //* __bytes_to_get 为两倍 __total_bytes 加上 (_S_heap_size 右移 4 位后向 8 取整)
      //* _S_heap_size 会越来越大，所以随着内存的不断申请，新申请的内存块空间也越来越大
      //* 第一次：0 右移 4 位再取整，结果为 0
      long unsigned int __bytes_to_get = 2 * __total_bytes + _S_round_up(_S_heap_size >> 4);
      // Try to make use of the left-over piece.
      //* 备用内存池以新大小划分 chunk 块后，还剩余一部分未利用的空间，且该部分空间不足以划分一块当前大小的 chunk
      if (__bytes_left > 0) {
        //* 定位到该剩余空间所对应 chunk 块大小的链表指针
        _Obj *volatile * __my_free_list =
            _S_free_list + _S_freelist_index(__bytes_left);
        //* 将该空间转化为对应的 chunk 节点，并将该节点头指针指向链表头结点
        ((_Obj *)_S_start_free) -> _M_free_list_link = *__my_free_list;
        //* 接着将 _S_start_free 转化的 _Obj* 赋值给该链表的头节点指针
        *__my_free_list = (_Obj*)_S_start_free;
        //* 此时，该大小 chunk 对应的链表头结点(_Obj*)指向的是备用内存池中的 (_Obj*)_S_start_free
      }
      //* malloc 开辟所有 chunk 块(20 * 2 = 40块)，首地址赋值给 _S_start_free
      _S_start_free = (char *)malloc(__bytes_to_get);
      //* malloc 开辟失败
      if (0 == _S_start_free) {
        long unsigned int __i;
        //* 二级指针用于遍历链表的指针数组
        _Obj *volatile * __my_free_list;
        _Obj *__p;
        // Try to make do with what we have.  That can't
        // hurt.  We do not try smaller requests, since that tends
        // to result in disaster on multi-process machines.
        //* 遍历指针数组中从当前 size 大小的元素开始到最后的所有元素(因为 size 之前的 chunk 块放不下size大小)
        for (__i = __size;
              __i <= (long unsigned int) _MAX_BYTES;
              __i += (long unsigned int) _ALIGN) {
          //* 第 i 个链表指针元素
          __my_free_list = _S_free_list + _S_freelist_index(__i);
          //* 准备访问第 i 个链表指针元素中的内容(chunk 块)
          __p = *__my_free_list;
          if (0 != __p) {
            //* 如果 __p 链表不为空，把链表头结点的头指针指向的下一个空闲chunk块指针赋值给链表头结点
            *__my_free_list = __p -> _M_free_list_link;
            //* 此时该链表原来的头结点 chunk 就可以分配给申请内存失败的 _S_start_free
            _S_start_free = (char *)__p;
            //* 从大小为 i 的 chunk 链表中获取了一块 chunk，所以此时_S_end_free - _S_start_free == __i
            //* 即 _S_end_free 和 _S_start_free 维护的大小变为 i
            _S_end_free = _S_start_free + __i;
            //* 再次递归调用时，__bytes_left >= __size，进入第二段逻辑
            //* 返回一块 __size 大小的 chunk， 备用内存块再次剩余一些未利用的空间，将该空间复用到对应大小的 chunk 链表
            return(_S_chunk_alloc(__size, __nobjs));
            // Any leftover piece will eventually make it to the
            // right free list.
          }
        }
        //* 如果后续所有 chunk 链表都为空，即无法从后面的 chunk 链表中分配一块对应大小的 chunk 给当前 size
        _S_end_free = 0;	// In case of exception.
        //* 再次尝试调用另外一个 allocate 函数申请内存
        _S_start_free = (char *)malloc_alloc::allocate(__bytes_to_get);
        // This should either throw an
        // exception or remedy the situation.  Thus we assume it
        // succeeded.
      }
      //* 记录内存池申请的总字节数 _S_heap_size
      _S_heap_size += __bytes_to_get;
      _S_end_free = _S_start_free + __bytes_to_get;
      //* 递归调用，此时 大小为 __size 的所有 chunk 块都已完成开辟
      return(_S_chunk_alloc(__size, __nobjs));
    }
  }

   //* 自由链表的指针数组，存放每个自由链表的起始地址
  static _Obj *volatile _S_free_list[_NFREELISTS];

  static std::mutex mtx;   //* 保证链表的线程安全

  static char  *_S_start_free;  //* 备用内存池起始地址
  static char  *_S_end_free;    //* 备用内存池末尾地址
  static long unsigned int _S_heap_size;         //* 内存池申请的总字节数
};

template <typename T>
char *Allocator<T>::_S_start_free = nullptr;

template <typename T>
char *Allocator<T>::_S_end_free = nullptr;

template <typename T>
long unsigned int Allocator<T>::_S_heap_size = 0;

template <typename T>
typename Allocator<T>::_Obj *volatile Allocator<T>::_S_free_list[_NFREELISTS] = {nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

template <typename T>
std::mutex Allocator<T>::mtx;

} //* namespace sgi_stl
#endif