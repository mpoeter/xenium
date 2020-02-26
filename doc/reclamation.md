@page reclamation_schemes Reclamation scheme interface

@brief General information about the interface used to implement all the reclamation schemes.

@tableofcontents

The implementation of the reclamation schemes is based on an adapted version of the interface
proposed by Robison \[[Rob13](index.html#ref-robison-2013)\], which defines the following
fundamental abstractions:
  * A `marked_ptr` allows one or more low-order bits to be borrowed (many lock-free algorithms
    rely on such mark tricks).
  * A `concurrent_ptr` acts like an atomic `marked_ptr`, i.e., it supports atomic operations.
  * A `guard_ptr` is an object that can atomically take a snapshot of the value of a `concurrent_ptr`
    and if the target has not yet been deleted, guarantees that the target will not be deleted as
    long as the `guard_ptr` holds a pointer to it.
    
It is important to note that only `guard_ptr` references protect against deletion. In effect, a
`concurrent_ptr` is a "weak" pointer and a `guard_ptr` is a "shared ownership" pointer,
conceptually similar to `std::weak_ptr` and `std::shared_ptr` with the following key differences:
  * `concurrent_ptr` and `guard_ptr` are abstract interfaces (a.k.a. "concepts"), not concrete
    interfaces.
  * They support a snapshot operation that is conceptually similar to the `std::weak_ptr::lock()` method.
  * A `std::weak_ptr` can indicate whether it has "expired", i.e., its target was deleted. A
    `concurrent_ptr` gives no such indication even if, as it can in some cases, points to
    freed memory.
    
A reclaimer type `R` has to define the following abstractions:
  * `R::concurrent_ptr<T>`: acts like an atomic markable pointer to objects of type `T` and provides an
    interface that is compatible with that of `std::atomic`. The class `T` must be
    derived from `enable_concurrent_ptr`.
  * `R::enable_concurrent_ptr<T, N, D>`: defines a mandatory base class for targets of `concurrent_ptr`;
    `T` is the derived class, `N` is the number of mark bits supported, which defaults to zero, and `D`
    is the deleter type that should be used for all objects of type `T`.
  * `R::region_guard`: allows some reclamation schemes to amortize the overhead; this is explained in
    more detail in @ref region_guard.
    
The intent of `enable_concurrent_ptr<T, N, D>` is to provide implementers of reclaimers with two things:
  * A way to force the alignment of targets, which is a common way to	provide mark bits in the pointers.
  * A place to embed reclaimer state, such as reference counts, in the user's objects.
  
The class `concurrent_ptr<T>` provides two auxiliary types:
  * `concurrent_ptr<T>::marked_ptr` : Acts like a pointer, but has `N` mark bits, where `N` is specified
    by the base class `enable_concurrent_ptr<T, N>` of `T`.
  * `concurrent_ptr<T>::guard_ptr` : Similar to a `marked_ptr`, but has shared ownership of its target
    _if_ the target has not been deleted.

To obtain a snapshot from `concurrent_ptr` and populate a `guard_ptr` the `acquire` and `acquire_if_equal`
methods can be used. In wait-free algorithms, `acquire` may be problematic with some schemes like
@ref `hazard_pointer` or @ref `lock_free_ref_count`, because it may have to loop indefinitely. For these
cases `acquire_if_equal` can be used, as it simply stops trying if the value in variable `p` does not match
the provided value in variable `m` and reports whether it was successful or not.

Releasing a `guard_ptr` follows the standard smart pointer interface. For a `guard_ptr` instance `g`, the
operation `g.reset` releases ownership and sets `g` to `nullptr`; the destructor of `guard_ptr` implicitly
calls `reset`.

In order to release a node, the `reclaim` method on a `guard_ptr` has to be called. This operation also
resets the `guard_ptr`.

Here is an example of these types and how they are used:
```cpp
// Let's assume we have a type "Reclaimer" that implements this interface.
// Forward declaration of our node struct so we can use it in the following aliases.
struct node;
// Define a number of aliases for simpler code.
using concurrent_ptr = typename Reclaimer::template concurrent_ptr<node, 0>;
using marked_ptr = typename concurrent_ptr::marked_ptr;
using guard_ptr = typename concurrent_ptr::guard_ptr;

// We want to use our node with concurrent_ptr, so we have to derive
// it from enable_concurrent_ptr.
struct node : Reclaimer::template enable_concurrent_ptr<node> {};

// Let's create a new node and store it in some publicly available concurrent_ptr.
marked_ptr new_node = new node();
concurrent_ptr cp;
cp.store(new_node);

// Acquire a guard to the node referenced by cp.
// This will protect the node from getting reclaimed as long as the guard_ptr exists.
guard_ptr guard;
guard.acquire(cp);

// Mark the node for reclamation. This will reset the guard_ptr and ensure that the
// node gets reclaimed once it is safe.
guard.reclaim();
```

@section enable_concurrent_ptr enable_concurrent_ptr

Every reclaimer must define a class `enable_concurrent_pointer` that is used as mandatory base class
for targets of `concurrent_ptr`. This base class does not only define the `number_of_mark_bits` and
an alias for the `Deleter` for internal use in the reclaimer, but also allows to enforce alignment of
instances or to store additional information like a reference counter. The minimal definition of such
a class looks like this:
```cpp
template <
  class T,
  std::size_t N = 0,
  class DeleterT = std::default_delete<T>>
struct enable_concurrent_ptr
{
  static constexpr std::size_t number_of_mark_bits = N;
  using Deleter = DeleterT;
};
```
`enable_concurrent_pointer` is a class template with the following template parameters:
  * `T` - is the derived class (this is an application of the
    [_curiously recurring template pattern_](https://www.fluentcpp.com/2017/05/12/curiously-recurring-template-pattern/)).
  * `N` -  is the number of mark bits that a `marked_ptr` must reserve when used with this class;
    this parameter defaults to zero.
  * `DeleterT` - is the deleter functor that shall be applied once an object can safely be reclaimed.

The class must define a member `number_of_mark_bits` that is set to `N` and a type alias `Deleter`.
In the original proposal the `Deleter` was not a parameter of `enable_concurrent_ptr` but of
`concurrent_ptr`. However, all implemented reclamation schemes except LFRC collect the
_to-be-reclaimed_ nodes in some list in order to defer reclamation until a later time when it is safe
to do so. Such a list can contain arbitrary nodes from different data structures, potentially using
different deleters. The information which deleter shall be used must therefore be stored together with
the node. But if this information is not already part of the node itself, it would require an additional
memory allocation to store this information, even in cases where the deleter itself has no data members
like `std::default_deleter`. In order to avoid this additional memory allocation, the `Deleter` parameter
is part of the `enabled_concurrent_ptr` class, which allows to embed `Deleter` instances directly in the
node. For this the internal helper class @ref deletable_object comes into play.

@section deletable_object

`deletable_object` is an internal helper class that is used by most of the reclamation schemes as the
common base class for `enable_concurrent_ptr`.
```cpp
struct deletable_object
{
  virtual void delete_self() = 0;
  deletable_object* next = nullptr;
protected:
  virtual ~deletable_object() = default;
};
```
The `next` pointer is used to build the single-linked list of _to-be-reclaimed_ nodes. The pure
virtual `delete_self` method is required because such lists contain only pointers to `deletable_object`
instances, but the `Deleter` expects an instance of the derived type. Therefore, a derived class has
to override `delete_self` and call the deleter with the appropriate parameter. To avoid duplication of
this code for each reclamation scheme, there are two different base classes which again make use of
the _curiously recurring template pattern_ to down-cast to the correct derived type which is handed
down as template parameter. The two different base classes are `deletable_object_with_empty_deleter`
and `deletable_object_with_non_empty_deleter`. This distinction is necessary because the size of an
empty class is not zero, but one. Unconditionally storing a deleter instance as member in the object
would therefore produce an unnecessary memory overhead that can be avoided this way. Instead of using
these classes directly, the alias `deletable_object_impl` is used to verify whether the given deleter
is an empty class and resolves to the correct base class.

`deletable_object_with_non_empty_deleter` contains an `aligned_storage` as buffer for a `Deleter`
instance. When a deleter is set using the `set_deleter` method the deleter instance is _moved_ into
the buffer. This allows the `Deleter` type to be non-default-constructible.

The template parameter `Base` defaults to `deletable_object` which should be fine for most of the
cases. In case there are special requirements this parameter can be used to define a custom base
class as long as it defines the same interface as `deletable_object`. This is used for example in
the implementations of @ref `stamp_it` and @ref `hazard_eras`, because these schemes requires that
the base class can store additional information for retired nodes.

@section marked_ptr marked_ptr

Many lock-free algorithms rely on the ability to store special flags in a pointer. The `marked_ptr`
class defines a high-level interface to a pointer of which a number of low-order bits can be
borrowed to store additional information. The number of bits that shall be used can be defined via
a template parameter.
```cpp
template <class T, std::size_t N>
class marked_ptr {
public:
  // Construct a marked ptr
  marked_ptr(T* p = nullptr, uintptr_t mark = 0) noexcept;

  // Set to nullptr
  void reset() noexcept;

  // Get mark bits
  uintptr_t mark() const noexcept;

  // Get underlying pointer (with mark bits stripped off).
  T* get() const noexcept;

  // True if get() != nullptr || mark() != 0
  explicit operator bool() const noexcept;

  // Get pointer with mark bits stripped off.
  T* operator->() const noexcept;

  // Get reference to target of pointer.
  T& operator*() const noexcept;

  inline friend bool operator==(const marked_ptr& l, const marked_ptr& r);
  inline friend bool operator!=(const marked_ptr& l, const marked_ptr& r);

  static constexpr std::size_t number_of_mark_bits = N;
};
```
Runtime assertions ensure that the specified mark value does not use more bits than reserved, as
well as that the pointer value does not occupy bits that are reserved for marking.

@section concurrent_ptr concurrent_ptr

A `concurrent_ptr` is basically an atomic `marked_ptr`; its interface is compatible to that of
`std::atomic`. In addition it defines aliases for the reclaimer's `marked_ptr` and `guard_ptr`
types.
```cpp
//! T must be derived from enable_concurrent_ptr<T>. D is a deleter.
template <
  class T,
  std::size_t N,
  template <class, std::size_t> class MarkedPtr,
  template <class T2, class MarkedPtrT, class Deleter> class GuardPtr,
  class DefaultDelete = std::default_delete<T>
>
class concurrent_ptr {
public:
  struct marked_ptr : MarkedPtr<T, N> {};

  template <class D = DefaultDelete>
  using guard_ptr = GuardPtr<T, marked_ptr, D>;

  concurrent_ptr(const marked_ptr& p = marked_ptr()) noexcept : ptr(p) {}
  concurrent_ptr(const concurrent_ptr&) = delete;
  concurrent_ptr(concurrent_ptr&&) = delete;
  concurrent_ptr& operator=(const concurrent_ptr&) = delete;
  concurrent_ptr& operator=(concurrent_ptr&&) = delete;

  // Atomic load that does not guard target from being reclaimed.
  marked_ptr load(std::memory_order order = std::memory_order_seq_cst) const;

  // Atomic store.
  void store(const marked_ptr& src,
             std::memory_order order = std::memory_order_seq_cst);

  // Shorthand for store (src.get())
  template <class D>
  void store(const guard_ptr<D>& src,
             std::memory_order order = std::memory_order_seq_cst);

  bool compare_exchange_weak(marked_ptr& expected, marked_ptr desired,
    std::memory_order order = std::memory_order_seq_cst);
  bool compare_exchange_weak(marked_ptr& expected, marked_ptr desired,
    std::memory_order order = std::memory_order_seq_cst) volatile;
  bool compare_exchange_weak(marked_ptr& expected, marked_ptr desired,
    std::memory_order success, std::memory_order failure);
  bool compare_exchange_weak(marked_ptr& expected, marked_ptr desired,
    std::memory_order success, std::memory_order failure) volatile;

  bool compare_exchange_strong(marked_ptr& expected, marked_ptr desired,
    std::memory_order order = std::memory_order_seq_cst);
  bool compare_exchange_strong(marked_ptr& expected, marked_ptr desired,
    std::memory_order order = std::memory_order_seq_cst) volatile;
  bool compare_exchange_strong(marked_ptr& expected, marked_ptr desired,
    std::memory_order success, std::memory_order failure);
  bool compare_exchange_strong(marked_ptr& expected, marked_ptr desired,
    std::memory_order success, std::memory_order failure) volatile;
};
```

@section guard_ptr

A `guard_ptr` is basically a `marked_ptr` that protects the object that it points to from being
reclaimed. In that sense it is conceptually similar to a `std::shared_ptr`. In contrast to the other
two pointer types, `guard_ptr` implementations are very specific to the concrete reclamation scheme.
```cpp
template <class T>
class guard_ptr
{
  using Deleter = typename T::Deleter;
public:
  guard_ptr() noexcept;
  ~guard_ptr();
    
  // Guard a marked ptr.
  explicit guard_ptr(const marked_ptr& p);
  
  guard_ptr(const guard_ptr& p);
  guard_ptr(guard_ptr&& p) noexcept;

  guard_ptr& operator=(const guard_ptr& p) noexcept;
  guard_ptr& operator=(guard_ptr&& p) noexcept;

  // Get underlying pointer
  T* get() const noexcept;

  // Get mark bits
  uintptr_t mark() const noexcept;

  // Support implicit conversion from guard_ptr to marked_ptr.
  operator marked_ptr() const noexcept;

  // True if get() != nullptr || mark() != 0
  explicit operator bool() const noexcept;

  // Get pointer with mark bits stripped off. Undefined if target has been reclaimed.
  T* operator->() const noexcept;

  // Get reference to target of pointer. Undefined if target has been reclaimed.
  T& operator*() const noexcept;

  // Swap two guards.
  void swap(guard_ptr& g) noexcept;

  // Atomically take snapshot of p, and *if* it points to unreclaimed object,
  // acquire shared ownership of it.
  void acquire(concurrent_ptr<T>& p,
               std::memory_order order = std::memory_order_seq_cst);

  // Like acquire, but quit early if p != expected.
  bool acquire_if_equal(concurrent_ptr<T>& p,
                        const marked_ptr& expected
                        std::memory_order order = std::memory_order_seq_cst);

  // Release ownership. Postcondition: get() == nullptr.
  void reset() noexcept;

  // Reset. Deleter d will be applied some time after all owners release their ownership.
  void reclaim(Deleter d = Deleter()) noexcept;
};
```

A `guard_ptr` has to be acquired by the methods `acquire` or `acquire_if_equal` to ensure that the
`guard_ptr` holds a safe reference that protects the object from being reclaimed. These methods take
a snapshot of the value of a `concurrent_ptr` and store a safe reference to the object in the
`guard_ptr` if the target has not yet been deleted. 

For some implemenations like @ref `hazard_pointers`, @ref `hazard_eras` or `lock_free_ref_count`,
`acquire` may has to loop indefinitely in order to acquire a safe reference, which can be problematic
in wait-free algorithms. In these cases `acquire_if_equal` can be used as it simply stops trying if
the value in `p` does not match a provided value `m` and reports whether it was successful or not.

Both methods, `acquire` and `acquire_if_equal` take an optional `memory_order` parameter that defines
the order of the read operation on the `concurrent_ptr` object `p`. The default value is
`memory_order_seq_cst`.

Releasing a `guard_ptr` follows the standard smart pointer interface; the operation `g.reset`
releases ownership and sets `g` to `nullptr`, the destructor of `guard_ptr` implicitly calls reset.

The `reclaim` method resets the `guard_ptr` and marks the node for deletion.

One important limitation of `guard_ptr`s is that they must not be moved between threads, i.e., move
construction or move assignment operations must not be used to transfer ownership of a `guard_ptr`
from one thread to another. The reason for this simply is that these move operations are optimized
based on the assumption that both, the target and the source operands, belong to the same thread.
However, copy construction and copy assignment do not suffer from this limitation.

@section region_guard

A `region_guard` is an additional concept that is not part of the proposal by Robison. However, some
reclamation schemes like @ref `epoch_based`, @ref `new_epoch_based` or @ref `stamp_it` use the concept
of _critical regions_. In these schemes a `guard_ptr` can only exist inside a
critical region, so unless the thread is already inside a critical region the `guard_ptr`
automatically enters a new one. But entering and leaving critical regions are usually rather expensive
operations, so `region_guard`s allow to amortize this overhead. The constructor of a `region_guard`
enters a new region (unless the thread is already inside one) and, if it was the last one, leaves the
region in the destructor. Any `guard_ptr` instances that are created inside the scope of the
`region_guard` can simply use the current critical region and save the overhead of entering a new one.

The `region_guard` class does not define any member functions, it only uses the RAII concept to leave
the region upon destruction.

In order to provide a consistent interface every reclamation scheme has to define a `region_guard` class
regardless of whether the scheme actually supports this concept. For reclamation schemes that do not
support it, it is sufficient to define an empty `region_guard` class.
