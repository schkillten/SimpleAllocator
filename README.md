TODO rework the flow of malloc, right now I just try to find a suitable chunk, if that fails I try to defrag the free list, then try to find a best fit again. If we still fail to find a chunk then we expand our memory pool with sbrk. Then try to find a best fit again. This is one of the weakest parts of my design so I really want to focus on finding a much efficient solution.

TODO in the defragger, just write a sort algorithm for the linked list. Right now I throw each pointer into a buffer and use std::sort. I mainly did this just to test out the idea and see if it all works. It does work but the biggest issue is because I can't use any dynamic memory to defrag the linked list, I used a fix sized buffer and sort the linked list in batches. But I only do one pass, so only each independent batch is sorted, resulting in a poor defragment. If I wrote an algorithm to sort the linked list in place then I could get significantly better results when defragging, giving an overall performance boost I believe.

TODO much more thoroughly test everything out. I'm doing a lot of pointer arithmetic and lots of explicit conversions. And some pretty tricky alignment stuff. So I just want to write some tests (aside from lots of manual tests I've done) to actually check the accuracy of everything.

# SimpleAllocator
An allocator I made in c++ for learning. The goal was to learn more about memory and allocators, and implement my own simple allocator.

Implemented using sbrk and brk (for fun). It's stored as an explicit free list in a doubly linked list. Uses a best fit search to find the most appropriate node for the users request.

My two favorite parts of the allocator were the defrager and the alignment features.

# Defrager

Since we use an explicit free list, our linked list can be all over the place in heap memory. And as users ask for more memory and free memory, chunks will slowly become fragmented, causing longer scans of the free list and bloated memory since we will ask for more memory from sbrk if we cant find a chunk to meet a users request. Defragmenting can help fix this issue but going through the free list and merging nodes in the linked list that are directly adjacent to each other.

The solution that I came up with to defragment the free list is to sort the free list, then just do a single scan and merge any nodes that are directly adjacent to each other. Right now I use a fix sized buffer and throw in as many pointers as I can in the buffer and sort that buffer, then repeat that until I've scanned the entire linked list once. Then once everything is sorted I do another scan and try to merge nodes if they're directly adjacent.

# Alignment

The goal of alignment is to return to the user a pointer whose address is basically a multiple of the desired alignment. This means that when we find a chunk of memory in our free list (linked list of unused chunks of memory) that we want to return to the user, and that chunk of memory isn't aligned to the desired alignment, we will have to add an offset from the beginning of the chunk to the nearest aligned address.

Since the distance from the beginning of the chunk to the nearest aligned address can be anywhere between 0 to 16 (if the desired alignment is 16) bytes away, and we have no way of knowing that value when a user calls Free(ptr) on their pointer, we have to some how store that information. So my solution is to store it directly to the left of the pointer we returned to the user. So when the user calls free, we get that value, then subtract it from the users pointer to get us back to the beginning of the chunk of memory, where our header info is stored so we can add it back to the free.
