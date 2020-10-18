# SimpleAllocator
An allocator I made in c++ for learning.

Implemented using sbrk and brk (for fun). It's stored as an explicit free list in a doubly linked list. Uses a best fit search to find the most appropriate node for the users request.

Allocator::Free(ptr) should be an O(1) operation, as it just appends it to the tail of the free list

Allocator::MergeFree(ptr) I removed this function since I reworked a lot of the core structure, but once I add it back, it will just do a merge during a free
attempting to place merge it in with another node vs just appending to the back, causing fragmentation slowly

# Allocation

Allocator::Malloc(size, alignment) is a bit more tricky. It will first do a best fit search, which is worst case an O(N) operation, N being the number of nodes in the free list. If it is unable to find a best fit, then it will defragment the free list. If defragmenting doesn't yield a result, then we will ask for more memory from the heap/sbrk

Defragmenting is tricky because all of the memory is stored in the same chunk of memory. The free list and all of the memory that has been given to a user, all exists in the same range of heap space. So when a user requests memory, we just stop tracking it in the free list, when they return it, we track it in the free list, all inside the same heap space. Because of this, if I use something like an std::vector, it will use dynamic memory, then when I request more space from sbrk it will cause a gap between my old heap address range, and the new memory from sbrk. 

So instead, I reserve a buffer of defragment space in `uintptr_t DefragMemory[MAX_STATIC_DEFRAG_MEM];`. Then I scan the free list, put every node pointer into the DefragMemory array, then sort it with `std::sort(DefragMemory, DefragMemory+count)`. Because it's all one contiguous chunk of memory I can sort it this way. Then I relink everything up into an ordered free list, then just go through each node and try to merge them together if they're directly adjacent in memory. In the case where the number of nodes is greater than `MAX_STATIC_DEFRAG_MEM` I just repeat the process until there are no more nodes in the list to sort.

As for allocating more memory, I just make a call to sbrk based off the requested size, and add some more memory as a multiple of `DEFAULT_BUFFER_SIZE`. Then insert that new chunk of memory to the back of the free list.


# Alignemnt

This was one of the more difficult parts for me to figure out, but I really like my solution, in terms of the amount of fun it was to figure out and write. And how much I learned doing it.

The goal of alignment in my case, is to give the user a pointer that is aligned to the degree of alignment that they request, otherwise I use my default value. This means that the address the pointer points to is a multiple of whatever value is given for alignment.

When a user requests some amount of memory, I have to add some extra memory to what they want, so we can do the book keeping. Each node has a header that tells you the size of a chunk of memory. The header is stored at the very beginning of the node. Then we have to add more space for the alignment and any other info we need for book keeping

So in terms of getting the user an aligned address, we have to return them a pointer that is offset from the beginning of the node. And this offset depends on a few factors, it isn't a constant amount and changes with every request. Because of this I have to store an additional piece of information directly left of the beginning of the pointer, where I can store the offset value. So when we give the user an aligned pointer, we calculate the offset, store it in the bytes of memory directy left of the pointer. Then when they return, we can retrieve the offset value, and use that to find the original beginning of our node. Then we can get the size and properly save it into the free list again.

