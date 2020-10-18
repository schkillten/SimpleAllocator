#include <unistd.h>
#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <algorithm>
#include <cassert>

typedef uint64_t SizeType;
typedef uint64_t PayloadOffsetType;

struct Block;

struct MetaHeader
{
  Block* next = nullptr;
  Block* prev = nullptr;
};

struct Block
{
  SizeType size;
  MetaHeader meta;
};


namespace
{

  const SizeType DEFAULT_BUFFER_SIZE = 2048;
  const SizeType DEFAULT_ALIGNMENT = 16;
  const SizeType MAX_STATIC_DEFRAG_MEM = 1000;

  class Allocator
  {
    public:

      void test()
      {
        AttemptToDefragFreeList(0);
      }

      Allocator()
      {
        std::cout << "START OF PROGRAM BREAK: " << sbrk(0) << '\n';
      }
      ~Allocator()
      {
        std::cout << "START PROGRAM BREAK: " << StartSBRK << '\n';
        assert(AddByteOffsetToPointer(StartSBRK, totalAllocated) == sbrk(0));
        auto r = brk(StartSBRK);
        std::cout << "END OF PROGRAM BREAK: " << sbrk(0) << '\n';
      }

      /*
        alignment needs to be a power of 2 (not checking for that yet)
      */
      void* Malloc(SizeType size, SizeType alignment = DEFAULT_ALIGNMENT)
      {
        SizeType metaHeaderSpace = sizeof(MetaHeader) > size ? sizeof(MetaHeader) : size;

        size = sizeof(PayloadOffsetType) + metaHeaderSpace + sizeof(Block::size) + alignment;

        Block* bestFit = FindBestFitChunk(size);

        //this chunk will only happen if we're out of memory or chunks large enough
        //for the request, so it shouldn't happen often
        if(bestFit == nullptr)
        {
          AttemptToDefragFreeList(size);
          bestFit = FindBestFitChunk(size);
          if(bestFit == nullptr)
          {
            CreateFreeSpace(size);
            bestFit = FindBestFitChunk(size);
          }
        }

        if(bestFit == nullptr)
        {
          return nullptr;
        }

        //should probably do some sort of check here to make sure removed from free list
        RemoveFromFreeList(bestFit);
        return GetAlignedPayloadFromBlock(bestFit, alignment);
      }

      /*
        For now this just takes the user pointer, gets the payloadoffset data that should
        be stored exactly sizeof(PayloadOffsetType) bytes to the left of the user ptr

        Get that out then that value will tell us where the original pointer is at, so we
        can move down to the actual header info that tells us the size of the chunk that was
        given to the user, then we just add that to the end of the linked list
      */
      void Free(void* ptr)
      {
        PayloadOffsetType offset = GetOffsetDataFromPayloadPointer(ptr);
        Block* blockptr = reinterpret_cast<Block*>(SubtractByteOffsetFromPointer(ptr, offset));
        blockptr->meta.next = nullptr;
        blockptr->meta.prev = nullptr;

        insert_back(blockptr);
      }

      /*
        Alternative to the regular free, that will occasionally defrag memory
        this will try to find something in the free list to merge with, otherwise
        it will use the regular free

        I had an implementation of this but I reworked a lot of stuff so I have to
        rewrite this too
      */
      void MergeFree(void* ptr){}

      void DebugMemory()
      {
        auto current = FreeListHead;
        while(current != nullptr)
        {
          std::cout << "~~~~~~~\n";
          PrintBlock(current);
          current = current->meta.next;
        }
        std::cout << "~~~~~~~\n\n\n";
      }

      void PrintBlock(Block* blockptr)
      {
        std::cout << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
        std::cout << "BLOCK SIZE: " << blockptr->size << '\n';
        std::cout << "BLOCK CURRENT: " << blockptr << '\n';
        std::cout << "BLOCK NEXT: " << blockptr->meta.next << '\n';
        std::cout << "BLOCK PREV: " << blockptr->meta.prev << '\n';
        std::cout << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
      }

      void ValidateFreeList()
      {
        /*
          I just want to go through and do some math basically, make sure everything lines up
        */
      }

      void TotalAllocated()
      {
        std::cout << "TOTAL ALLOCATED: " << totalAllocated << '\n';
      }

      void TotalFreeListSize()
      {
        auto current = FreeListHead;
        SizeType total = 0;
        while(current != nullptr)
        {
          total+=current->size;
          current = current->meta.next;
        }
        std::cout << "FREE LIST SIZE: " << total << '\n';
      }

      void FreeListNodeSize()
      {
        auto current = FreeListHead;
        SizeType total = 0;
        while(current != nullptr)
        {
          total++;
          current = current->meta.next;
        }
        std::cout << "FREE LIST NODE SIZE: " << total << '\n';
      }

      void AssertPointerAligned(void* ptr, SizeType alignment)
      {
        auto intptr = reinterpret_cast<uintptr_t>(ptr);
        assert(intptr % alignment == 0);
      }
      
      void AssertMemoryOrdered()
      {
        Block* current = FreeListHead;
        while(current != nullptr && current->meta.next != nullptr)
        {
          PrintBlock(current);
          PrintBlock(current->meta.next);
          assert(current < current->meta.next);
          current = current->meta.next;
        }
      }

      void CheckFreedMemoryContiguous()
      {
        Block* current = FreeListHead;
        SizeType count = 0;
        while(current != nullptr && current->meta.next != nullptr)
        {
          if(AddByteOffsetToPointer(current, current->size) != current->meta.next)
          {
            PrintBlock(current);
            PrintBlock(current->meta.next);
            PrintBlock(FreeListHead);
            PrintBlock(FreeListTail);
          }

          current = current->meta.next;
          count++;
        }
      }

    private:

      void insert_back(Block* blockptr)
      {
        if(FreeListHead == nullptr && FreeListTail == nullptr)
        {
          FreeListHead = blockptr;
          FreeListTail = blockptr;
          blockptr->meta.next = nullptr;
          blockptr->meta.prev = nullptr;
        }
        else
        {
          blockptr->meta.prev = FreeListTail;
          blockptr->meta.next = nullptr;
          FreeListTail->meta.next = blockptr;
          FreeListTail = blockptr;
        }
      }

      /*

        This will take the user requested size and increase the sbrk/program break
        by some amount, creating more of a buffer for next requests

        The pointer returned by sbrk is just added to the end of the free list
        with the header info updated approriately 
        cases:
          We could have nullptr head/tail meaning no memory avail at all
          We could have a single item, head/tail are the same
          we could have lots of chunks but non were enough to fit a request
      */
      void CreateFreeSpace(SizeType size)
      {
        //just taking advantage of align, getting the size to the next multiple of default buffer size
        //then doubling it, since the user is asking for a lot of space
        SizeType increaseSizeBy = size > DEFAULT_BUFFER_SIZE ? align(size,DEFAULT_BUFFER_SIZE)*2 : DEFAULT_BUFFER_SIZE;
        void* breakPtr = sbrk(increaseSizeBy);
        if(breakPtr != (void*)-1)
        {
          Block* newChunk = reinterpret_cast<Block*>(breakPtr);
          newChunk->size = increaseSizeBy;
          newChunk->meta.next = nullptr;
          newChunk->meta.prev = nullptr;

          totalAllocated+=increaseSizeBy;

          if(StartSBRK == nullptr)
          {
            StartSBRK = newChunk;
          }
        
          insert_back(newChunk); 
        }else{
          assert(false);
        }
      }

      void AttemptToDefragFreeList(SizeType size)
      {
        SizeType count = 0;
        Block* current = FreeListHead;
        FreeListHead = nullptr;
        FreeListTail = nullptr;
        while(current != nullptr)
        {
          while(current != nullptr && count < MAX_STATIC_DEFRAG_MEM)
          {
            DefragMemory[count] = reinterpret_cast<uintptr_t>(current);
            current = current->meta.next;
            count++;
          }

          std::sort(DefragMemory, DefragMemory+count);

          for(SizeType i = 0; i<count; i++)
          {
            Block* current = reinterpret_cast<Block*>(DefragMemory[i]);
            insert_back(current);
          }

          count = 0;
        }

        current = FreeListHead;  
        while(current != nullptr && current->meta.next != nullptr)
        {
          SizeType oldSize = current->size;
          Block* merged = AttemptToMerge(current, current->meta.next);
          if(merged->size == oldSize)
          {
            current = current->meta.next;
          }
          else
          {
            current = merged;
          }
        }

        AttemptToDecreaseBuffer();
      }

      Block* AttemptToMerge(Block* left, Block* right)
      {
        if(left != nullptr && right != nullptr && FreeListHead != nullptr)
        {
          if(AddByteOffsetToPointer(left, left->size) == right)
          {
            if(left == FreeListHead && right == FreeListTail)
            {
              left->size+=right->size;
              left->meta.next = right->meta.next;
              FreeListTail = FreeListHead;
            }
            else if(right == FreeListTail)
            {
              left->size+=right->size;
              left->meta.next = right->meta.next;
              FreeListTail = left;
            }
            else
            {
              left->size+=right->size;
              left->meta.next = right->meta.next;
              left->meta.next->meta.prev = left;
            }
          }
        }
        return left;
      }

      void AttemptToDecreaseBuffer()
      {
        if(FreeListHead == nullptr && FreeListTail == nullptr)
        {
          return;
        }
        if(AddByteOffsetToPointer(FreeListTail, FreeListTail->size) != sbrk(0))
        {
          return;
        }

        if(FreeListHead != FreeListTail)
        {
          Block* oldTail = FreeListTail;
          totalAllocated-=FreeListTail->size;
          RemoveFromFreeList(FreeListTail);
          auto b = brk(oldTail);
        }
        else if(FreeListHead == FreeListTail && FreeListTail->size > DEFAULT_BUFFER_SIZE)
        {
          totalAllocated-=FreeListTail->size-DEFAULT_BUFFER_SIZE;
          FreeListTail->size = DEFAULT_BUFFER_SIZE;
          auto b = brk(AddByteOffsetToPointer(FreeListTail, DEFAULT_BUFFER_SIZE));
        }
      }

      /*
        Finds the best fitting block in our freelist linked list

        This will scan the freelist finding the smallest block large enough for
        the request

        Then it will try to split the block to attempt to save some space
      */
      Block* FindBestFitChunk(SizeType size)
      {
        Block* current = FreeListHead;
        Block* currentBestFit = nullptr;
        
        while(current != nullptr)
        {
          if(currentBestFit == nullptr && current->size >= size)
          {
            currentBestFit = current;
          }

          if(current->size >= size && current->size < currentBestFit->size)
          {
            currentBestFit = current;
          }

          current = current->meta.next;
        }

        if(currentBestFit != nullptr){
          currentBestFit = SplitBlock(currentBestFit, size);
        }
        return currentBestFit;
      }

      /*
        Splitting a block is just taking a single node, offsetting its pointer
        by size bytes, and creating a new node in the linked list from that position
        updating the next/prev and size header values in the old node and the new node

        cases:
          header/tail are nullptrs
          header/tail are the same value
          just header being split
          just tail being split
          somehwere in the middle being split
      */
      Block* SplitBlock(Block* blockptr, SizeType size)
      {
        if(blockptr != nullptr)
        {
          SizeType leftOverAfterSplit = blockptr->size - size;

          if(leftOverAfterSplit > sizeof(Block) + DEFAULT_ALIGNMENT)
          {
            Block* rightSideSplit = reinterpret_cast<Block*>(AddByteOffsetToPointer(blockptr, size));

            rightSideSplit->size = leftOverAfterSplit;
            rightSideSplit->meta.next = blockptr->meta.next;
            rightSideSplit->meta.prev = blockptr;

            blockptr->size = size;
            blockptr->meta.next = rightSideSplit;

            if(FreeListHead == FreeListTail || blockptr == FreeListTail)
            {
              FreeListTail = rightSideSplit;
            }
            else
            {
              rightSideSplit->meta.next->meta.prev = rightSideSplit;
            }
          }
        }

        return blockptr;
      }

      /*
        Take the blockptr and increase it by sizeof(Header) + sizeof(PayloadOffsetType)
        Then align whatever that address is by the given alignment


        we take the difference of the blockptr plus whatever that alignment is
        and store it directly to the left of the pointer which we know we have at least
        sizeof(PayloadOffsetType) space to use.

        need to do the math and make sure everything lines up correctly and is within the
        allowed block size/space
      */
      void* GetAlignedPayloadFromBlock(Block* blockptr, SizeType alignment)
      {
        void* alignFromPtr = AddByteOffsetToPointer(blockptr, sizeof(Block::size) + sizeof(PayloadOffsetType));
        uintptr_t alignedAddressInteger = align(reinterpret_cast<uintptr_t>(alignFromPtr), alignment);

        PayloadOffsetType offset = alignedAddressInteger - reinterpret_cast<uintptr_t>(blockptr);
        void* payloadAddress = AddByteOffsetToPointer(blockptr, offset);

        PayloadOffsetType* payloadOffsetStorage = reinterpret_cast<PayloadOffsetType*>(SubtractByteOffsetFromPointer(payloadAddress, sizeof(PayloadOffsetType)));
        *payloadOffsetStorage = offset;

        return payloadAddress;
      }

      /*

        Given a blockptr, remove it from the linked list. This just adjusts the prev/next pointers
        of the nodes adjacent to it
        cases:
          removing header
          removing tail
          removing header/tail (they're the same)
          removing somewhere in the middle
      */
      void RemoveFromFreeList(Block* blockptr)
      {
        if(blockptr != nullptr)
        {
          if(blockptr == FreeListHead && blockptr == FreeListTail)
          {
            FreeListHead = nullptr;
            FreeListTail = nullptr;
          }
          else if(blockptr == FreeListHead)
          {
            FreeListHead = FreeListHead->meta.next;
            FreeListHead->meta.prev = nullptr;
          }
          else if(blockptr == FreeListTail)
          {
            FreeListTail = FreeListTail->meta.prev;
            FreeListTail->meta.next = nullptr;
          }
          else
          {
            blockptr->meta.prev->meta.next = blockptr->meta.next;
            blockptr->meta.next->meta.prev = blockptr->meta.prev;
          }
        }
      }

      PayloadOffsetType GetOffsetDataFromPayloadPointer(void* ptr)
      {
        return *reinterpret_cast<PayloadOffsetType*>(SubtractByteOffsetFromPointer(ptr, sizeof(PayloadOffsetType)));
      }
      
      void* AddByteOffsetToPointer(void* ptr, SizeType offset)
      {
        return reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(ptr)+offset);
      }

      void* SubtractByteOffsetFromPointer(void* ptr, SizeType offset)
      {
        return reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(ptr)-offset);
      }

      SizeType align(SizeType size, SizeType alignment)
      {
        return (size + alignment - 1) & ~(alignment - 1);
      }

      Block* FreeListHead = nullptr;
      Block* FreeListTail = nullptr;

      void* StartSBRK = nullptr;

      //used for debugging, dont need it after
      SizeType totalAllocated = 0;

      uintptr_t DefragMemory[MAX_STATIC_DEFRAG_MEM];
  };
};
