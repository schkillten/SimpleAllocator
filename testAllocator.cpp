#include "SimpleAllocator.h"

int main()
{

  SizeType alignment = 8;
  Allocator allocator;
  std::srand(std::time(nullptr));

  SizeType allocations = 1000000;

  //if allocations goes to high it segfaults because of this array
  //I believe I'm going over my stack limit so it crashes
  void* ptrs[allocations];
  SizeType ptrsCount = 0;
  std::cout << "ASFSDF\n";
  for(int i = 0; i < allocations; i++)
  {
    std::cout << i << '\n';
    auto randValue = std::rand() % 1000;
    int* tmp = reinterpret_cast<int*>(allocator.Malloc(randValue,alignment));
    allocator.AssertPointerAligned(tmp, alignment);
    *tmp = i;
    if(i%12==0)
    {
      allocator.Free(tmp);
    }
    else
    {
      ptrs[ptrsCount] = tmp;
      ptrsCount++;
    }
  }

  for(SizeType i = 0; i < ptrsCount; i++)
  {
    allocator.Free(ptrs[i]);
  }
  allocator.FreeListNodeSize();
  allocator.TotalAllocated();
  allocator.TotalFreeListSize();
  allocator.test();
  allocator.FreeListNodeSize();
  allocator.TotalAllocated();
  allocator.TotalFreeListSize();

}
