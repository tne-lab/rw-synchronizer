# RWSync

The purpose of RWSync is to allow one "writer" thread to continally
update some arbitrary piece of information and N "reader" threads to retrieve
the latest version of that information that has been "pushed" by the writer,
without any thread having to wait to acquire a mutex or allocate memory
on the heap (aside from internal allocations depending on the data structure used).
For one reader and one writer, this requires three instances of whatever data type is
being shared to be allocated upfront; for an arbitrary number of readers N, it requires
N + 2 instances. During operation, "pushing" from the writer and "pulling" to a reader
are accomplished by exchanging atomic indices between slots indicating what each instance
is to be used for, rather than any actual copying or allocation.

## Installation

The library is only 4 files in a flat structure: two headers, a "template implementation" header,
and a C++ implementation file. These only use the C++11 standard library and can be easily
incorporated in various projects.

There is also a CMake build file to create a common library for the [Open Ephys GUI](https://open-ephys.atlassian.net/wiki/spaces/OEW/pages/491527/Open+Ephys+GUI) under `RWSync/OpenEphysCMakeBuild`. (See: [Plugin CMake Builds](https://open-ephys.atlassian.net/wiki/spaces/OEW/pages/1259110401/Plugin+CMake+Builds))

## Usage

You can either use a **container**, which manages the construction and destruction of all
data instances along with the synchronization logic and gives you pointers to write to
and read from, or a **manager**, which just does the sync logic and gives you an index
to use when accessing your own data structures.

### Container interface

 * There are two constructible subclasses of the abstract `Container` class:

     * `RWSync::FixedContainer<T, N=1>` if you have fixed number of maximum readers `N`;
     * `RWSync::ResizableContainer<T>` if the number of maximum readers is unknown.

   Here, T is the type of data that needs to be exchanged. A `ResizableContainer<T>` can
   only be created if T is copy-constructible, since new data instances to support
   additional readers have to be copied from a template.

 * The constructor either type of container takes whatever arguments
   would be used to construct each `T` object, for instance:
     
   ```
   RWSync::FixedContainer<std::array<int, 5>> myArrayContainer({ 1, 2, 3, 4, 5 });
   ```

   would construct a container that allows one reader, with each data instance
   initialized to the array `{ 1, 2, 3, 4, 5 }`.

 * Any function that takes a reference to the data type can be called on all copies
   using the `apply` method as long as there are no active readers or writers.
   You can pass any callable with the correct signature, such as a lambda.

 * The `reset` method on a container brings you back to the state where no writes have
   been performed yet. Can only be called when no read or write pointers exist.

 * To write data, construct a `RWSync::WritePtr<T>` with the container
   as an argument. This can be used as a normal pointer. It can be acquired, written to,
   and released multiple times and will keep referring to the same instance until
   `pushUpdate()` is called, at which point this instance is released for reading
   and a new one is acquired for writing.

   Note that on construction and after calling `pushUpdate()`, there are no guarantees about
   the contents of the new data instance owned by a WritePtr. Generally it will have
   some data that was previously written and then read, and will have to be cleared
   or just overwritten with the new data.
   
 * To read, construct a `RWSync::ReadPtr<T>` with the container as an argument. This can 
   be used as a normal pointer, but first wait for `canRead()` to return
   true - this will be false until at least one write has occurred.

 * If you want to get the latest update from the writer without destroying the read
   pointer and constructing a new one, you can call the pullUpdate() method.

 * If you attempt to create two write pointers to the same Container, the
   second one will be effectively null; you can check for this with `isValid()`
   (if `isValid()` ever returns false, this should be considered a logic error since a program
   shouldn't create writers to the same container in multiple places). The same is true
   of read pointers, except the limit is the number of allocated readers rather than 1.

 * You can also create a `GuaranteedReadPtr<T>` if you have a `ResizableContainer<T>`, which
   will never be invalid. The tradeoff is that it might have to alloate a new data
   instance during construction. Still use `canRead()` to make sure you're not reading
   before a write has occurred.

 * If you find yourself in a situation with an invalid pointer, you can use
   `tryToMakeValid()` to try to get an available data instance rather than constructing
   a new pointer. This is probably not a good way of doing things.

 * The `hasUpdate()` method on a ReadPtr returns true if there is new data
   from the writer that has not been read by this reader yet. After a call to `hasUpdate()` returns
   true, the current read ptr is guaranteed to be readable after calling `pullUpdate()`.

### Manager interface

 * An `RWSync::Manager` directly works similarly to a container; the main difference is 
   that you are responsible for allocating and accessing the data, and the Manager just
   tells you which index into your structure to use as for reads and writes.

 * To write, use an `RWSync::WriteIndex` instead of a `WritePtr`.
   This can be converted to int to use directly as an index, and has a `pushUpdate()` method
   that works the same way as for the write pointer. The index can be -1 (i.e. invalid)
   if you try to create two write indices with the same manager.

 * To read, use an `RWSync::ReadIndex` instead of a `ReadPtr`.
   This works how you would expect and also has a pullUpdate() method. Check whether it is
   valid before using by calling `canRead()` or `hasUpdate()` and `pullUpdate()`.

 * `RWSync::Lockout` is a scoped try-lock for both readers and writers; it will be "valid"
   iff no read or write indices exist at the point of construction. By constructing
   one of these and proceeding only if it is valid, you can make changes to each data
   instance outside of the reader/writer framework (instead of using `map()` on a container).
