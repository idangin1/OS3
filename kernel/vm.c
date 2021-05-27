#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"

uint time = 0;

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // map kernel stacks
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// Initialize the one kernel_pagetable
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
//    if((*pte & PTE_V) == 0)
    if((*pte & PTE_V) == 0 && (*pte & PTE_PG)==0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
//    if(do_free){
    if(do_free && ((*pte & PTE_PG) == 0)){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  int physical_page = myproc()->num_of_phys_pages;
  int swap_pages = myproc()->num_of_swap_pages;
  uint64 a;
  int total_pages = physical_page + swap_pages;
  if(total_pages == MAX_TOTAL_PAGES) {
      panic("impossible to alloc page - reach to max size");
  }

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  int idx_counter = 0;
  for(a = oldsz; a < newsz; a += PGSIZE){
    idx_counter++;
      #ifndef NONE
        if(myproc()!=0 && myproc()->pid > 2) {
            if(myproc()->num_of_phys_pages == MAX_PSYC_PAGES) {
                free_one_page();
            }
        }
    #endif
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }

    #ifndef  NONE
      if(myproc()!=0 && myproc()->pid > 2) { //now we want to add the physical page to the memory
          add_page_to_phys_mem(a);
      }
    #endif
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdeallocnew(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
//    pte_t *pte;
//    uint64 add;
//    uint pa;
  if(newsz >= oldsz)
    return oldsz;

//  add = PGROUNDUP(newsz);
//  for(;add<oldsz;add+=PGSIZE) {
//      pte = walk(pagetable, add,0);
//    #ifndef NONE
//        int found = 0;
//        int i;
//        for(i=0; i<MAX_PSYC_PAGES; i++) {
//            if((myproc()->phys_pages[i].virtual_add == add) && (myproc()->phys_pages[i].state == P_USED)) { //search for the desired physical page
//                found = i;
//                break;
//            }
//        }
//    #endif
//    if(!pte) {
//        uint pdx = (((uint)(add) >> 22) & 0x3FF); // 22 = offset of PDX in a linear address
//        add = (uint)((pdx+1) << 22) - PGSIZE;
//    } else if((*pte & PTE_V) != 0) { // this page is present in the physical memo
//        pa = ((uint)(*pte) & ~0xFFF);
//        if(pa==0) {
//            panic("panic during deallocuvm - caused by kfree");
//        }
//        //remove page from physical memory
//    #ifndef NONE
//    if(found) {
//        remove_page_from_memo(pagetable, add, myproc()->phys_pages);
//        if(myproc()->num_of_phys_pages > 0) {
//            myproc()->num_of_phys_pages--;
//        }
//    }
//    #endif
//
//    uint64 v = PA2PTE(pa); //TODO wrong macro?
//    kfree((void*)v);
//    } else if((*pte & PTE_PG) > 0) { //remove the page from swap memory iff the page is not in the memo
//        #ifndef NONE
//            remove_page_from_memo(pagetable, add, myproc()->swap_pages);
//            if(myproc()->num_of_swap_pages > 0) {
//                myproc()->num_of_swap_pages--;
//            }
//            *pte = 0;
//        #endif
//    }
////
////    return newsz; //TODO compare dealloc
//  }

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
    for (int a = PGROUNDDOWN(oldsz); a > PGROUNDDOWN(newsz); a -= PGSIZE) {
        if(myproc()!=0 && myproc()->pid > 2){
            remove_page_from_memo(0,a,myproc()->phys_pages);
        }

    }
  }

  return newsz;
}

uint64
uvmdealloc(pagetable_t pte, uint64 oldsz, uint64 newsz)
{
    if(myproc()!=0 && myproc()->pid > 2) {
        return uvmdeallocnew(pte, oldsz ,newsz);
    }

    if(newsz >= oldsz) {
        return oldsz;
    } else {
        return newsz;
    }
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0 && (*pte & PTE_PG) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((flags & PTE_PG) == 0) {
        if ((mem = kalloc()) == 0)
            goto err;
        memmove(mem, (char *) pa, PGSIZE);
    }
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

//int
//uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
//{
//    if(myproc()!=0 && myproc()->pid > 2) {
//        return uvmcopynew(old, new, sz);
//    }
//
//    return 0;
//}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}

int
handle_page_fault(void)
{
  uint64 virt_add = r_stval();
  pte_t *pte = walk(myproc()->pagetable, virt_add, 0);
  if(virt_add >= KERNBASE || pte == 0 || is_user_access_disabled(pte)) { //3rd cond: if the file is not accessible to user - don't try to bring it to memory
//    myproc()->total_page_faults++;
    myproc()->killed = 1; //TODO should we increase total_page_fault
    return 0;
  }

  #ifndef NONE
    if(is_paged_out(pte)) { //if PG flag is on - means that we had this page before in our memory
        //TODO should we increase total_page_fault
        //PGROUNDOWN returns the offset (first 12 bits) of the VA
        myproc()->total_page_faults++;
        uint64 rounded = PGROUNDDOWN(virt_add);
            handle_page_out(rounded, pte);
            return 0;
    }
  #endif

  return 1;
}

int
check_if_write(pte_t* pte)
{
  if((*pte & PTE_W) > 0) {
    return 1;
  }
  return 0;
}

int
is_user_access_disabled(pte_t* pte)
{
  if((*pte & PTE_U) > 0) {
    return 0;
  }
  return 1;
}

int
is_paged_out(pte_t* pte)
{
  if((*pte & PTE_PG) > 0) {
    return 1;
  }
  return 0;
}

void
handle_page_out(uint64 va, pte_t* pte)
{
  int is_found = 0;
  if(myproc()->num_of_phys_pages == MAX_PSYC_PAGES) {
    //achieved maximum page size
    free_one_page();
  }

//  uint p_add = PTE2PA((uint64)addr); //TODO right macro?
//  *pte = /*p_add | */PTE_V | PTE_U | PTE_W;
//  *pte = *pte & ~PTE_PG; //indicate that the page is not paged-out

  int idx;
  struct proc* p = myproc();
  for(idx = 0; idx < MAX_PSYC_PAGES; idx++) {
    if(p->swap_pages[idx].state == P_USED && p->swap_pages[idx].virtual_add == va) {
      is_found = 1;
      break;
    }
  }
  if(is_found == 0) { //sanity check
    panic("handle page out: couldn't find a valid page");
  }

  //bring the data we want from the secondary memory (swap file) into the main memory
  char* buffer;
  if ( (buffer = kalloc()) == 0 ) {
      panic("failed to kalloc");
  }

  char* mem;
  if ( (mem = kalloc()) == 0 ) {
      panic("failed to kalloc");
  }

  if(readFromSwapFile(p, buffer, idx*PGSIZE, PGSIZE) == -1) { //sanity check
    panic("handle page out: unable to read data from swap_file");
  }

  memmove(mem,buffer,PGSIZE);
  int new_flag = (PTE_FLAGS(*pte) & ~PTE_PG) | PTE_V | PTE_U | PTE_W;
  *pte = PA2PTE(mem) | new_flag;
//  *pte = PA2PTE(buffer) | PTE_FLAGS(*pte) | PTE_V;
//  *pte = *pte & ~PTE_PG; //indicate that the page is not paged-out
//  *pte = *pte | PTE_V;
//  memmove((void*) PTE2PA(va), buffer, PGSIZE); // change the data of va (virtual address we want) to point to buffer, which includes the swap_file data
  
  //initialize current index under swap_pages array
//  va = buffer; // update pte using the npa
  p->swap_pages[idx].offset = 0;
  p->swap_pages[idx].c_time = 0;
  p->swap_pages[idx].virtual_add = 0;
  p->swap_pages[idx].state = P_UNUSED;
  p->num_of_swap_pages--;

  #if NFUA
    p->swap_pages[idx].counter = 0;
  #elif LAPA
    p->swap_pages[idx].counter = 0xFFFFFFFF;
  #endif

  add_page_to_phys_mem(va);
  
  sfence_vma();
}

//this function adds the provided pte to physical pages array
void
add_page_to_phys_mem(uint64 add)
{
  struct page *free_pg;
  struct proc *p = myproc();
  for(int i=0; i<MAX_PSYC_PAGES; i++) {
    if(p->phys_pages[i].state == P_UNUSED) {
      p->num_of_phys_pages++;
      free_pg = &p->phys_pages[i];
      free_pg->state = P_USED;
      free_pg->offset = i*PGSIZE;
      free_pg->virtual_add = add;

      #if NFUA
        free_pg->counter = 0;
      #elif LAPA
        free_pg->counter = 0xFFFFFFFF;
      #elif SCFIFO
        free_pg->c_time = ++time; //set and increase time
      #endif
      
      break;
    }
  }
}

//This function is reponsible of freeing 1 page from the page table of the process
void
free_one_page()
{
  struct page *new_page;
  struct page *phys_page = select_page(); //choose the page to remove
  uint idx = MAX_TOTAL_PAGES;
  
  //this loop is responsible of finding space under swap_pages
  for(int i=0; i<MAX_PSYC_PAGES; i++) {
    if(myproc()->swap_pages[i].state == P_UNUSED) {
      idx = i;
      break;
    }
  }
  
  if(idx == MAX_TOTAL_PAGES) { //sanity check
    panic("No free space was found under swap_pages array");
  }

  new_page = &myproc()->swap_pages[idx]; //pointing to the selected free space under swap_array
  new_page->virtual_add = phys_page->virtual_add; //point to the address you want to delete
  new_page->counter = phys_page->counter;
  new_page->offset = idx*PGSIZE;
  new_page->c_time = 0;
  new_page->state = P_USED;

  uint64 pa = walkaddr(myproc()->pagetable, PGROUNDDOWN(phys_page->virtual_add));
//  int success = writeToSwapFile(myproc(), (char*)PTE2PA(phys_page->virtual_add), idx*PGSIZE, PGSIZE); //write to swap_file from physical address
  int success = writeToSwapFile(myproc(), (char*)pa, idx*PGSIZE, PGSIZE); //write to swap_file from physical address

  if(success < 0) { //sanity check
    panic("failure during writing to swap file");
  }

  myproc()->num_of_phys_pages--;
  myproc()->num_of_swap_pages++;

  //Task2
//  char* v =(char*) walk(myproc()->pagetable, PGROUNDDOWN(phys_page->virtual_add),0);
//  kfree(v);
    kfree((void*)pa);

  pte_t* p_table_entry = walk(myproc()->pagetable, phys_page->virtual_add, 0); //extract PTE from virtual address for the process' page-table
  *p_table_entry = *p_table_entry | PTE_U | PTE_PG;  //set user bit and PG bit on
  *p_table_entry = *p_table_entry & ~PTE_V;          //set valid bit off

  phys_page->state = P_UNUSED;
  phys_page->offset = 0;
  phys_page->c_time = 0;
  phys_page->virtual_add = 0;
  phys_page->table = 0;

  //TODO kfree on physical page VA?
  
  sfence_vma(); //TODO right place? should be between user<->kernel spaces
}

struct page*
select_page(void)
{
  #if NFUA
    return NFUA_page_selection();
  #elif LAPA
    return LAPA_page_selection();
  #elif SCFIFO
    return SCFIFO_page_selection();
  #endif

  return 0;
}

struct page*
NFUA_page_selection(void)
{
  int idx = 0;
  struct page *pg;
  struct proc *curr_proc = myproc();
  uint min_val = 0xFFFFFFFF;
  
  // get the minimum counter of all physical pages
  for(int i=0; i<MAX_PSYC_PAGES; i++) {
    if(curr_proc->phys_pages[i].state == P_USED) {
      if(curr_proc->phys_pages[i].counter < min_val) {
        min_val = curr_proc->phys_pages[i].counter;
        idx = i;
      }
    }
  }

  pg = &curr_proc->phys_pages[idx]; //select the page
  pg->counter = 0; //reset counter
  return pg;
}

struct page*
LAPA_page_selection(void)
{
  int idx = 0;
  struct page *pg;
  struct proc *curr_proc = myproc();
  uint min_counter_val = 0xFFFFFFFF;
  uint min_by_ones = 31;
  int val;
  
  // get the minimum counter of all physical pages
  for(int i=0; i<MAX_PSYC_PAGES; i++) {
    if(curr_proc->phys_pages[i].state == P_USED) {
      val = one_bits_counter(curr_proc->phys_pages[i].counter);
      if(val < min_by_ones) {
        min_by_ones = val;
        min_counter_val = curr_proc->phys_pages[i].counter;
        idx = i;
      } else if(val == min_by_ones) {
        if(curr_proc->phys_pages[i].counter < min_counter_val) {
          min_counter_val = curr_proc->phys_pages[i].counter;
          idx = i;
        }
      }
    }
  }

  pg = &curr_proc->phys_pages[idx]; //select the page
  pg->counter = 0xFFFFFFFF; //reset counter
  return pg;
}

//count the number of bits of "1" for the counter inserted. Necessary for LAPA page selection
int
one_bits_counter(uint counter) 
{
  uint new_counter = 0;
  while(counter) {
    new_counter += counter & 1; //if the LSB is 1 add to counter, otherwise - don't add
    counter = counter >> 1; // trim the LSB
  }

  return new_counter;
}

struct page*
SCFIFO_page_selection(void)
{
  struct page *pg = 0;
  struct proc *curr_proc = myproc();
  int min_creation_val;

  while(1) {
    min_creation_val = time + 1;
    for(int i=0; i<MAX_PSYC_PAGES; i++) {
        pte_t *my_pte = walk(curr_proc->pagetable, curr_proc->phys_pages[i].virtual_add, 0);
        if((curr_proc->phys_pages[i].state == P_USED) && (*my_pte & PTE_U)) {
            if(curr_proc->phys_pages[i].c_time < min_creation_val) {
              pg = &curr_proc->phys_pages[i];
              min_creation_val = pg->c_time;
            }
        }
    }

    pte_t *pte = walk(curr_proc->pagetable, pg->virtual_add, 0); //get the entry of the physical page we want to remove
    if((*pte & PTE_A) > 0) {
      *pte = *pte & ~PTE_A; //turn off access bit and give another chance later before selection
      pg->c_time = ++time; //update creation time so that this page will go to the end of the FIFO until next round
    } else {
      pg->c_time = 0;
      return pg;
    }
  }
}

void
NFUA_LAPA_handler(void)
{
  uint num = 1 << 31;
  pte_t *pte = 0;
  struct proc *curr_proc = myproc();
  
  //go over all physical pages and check for access bit. Turn it off after increase the MSB
  for(int i=0; i<MAX_PSYC_PAGES; i++) {
    if(curr_proc->phys_pages[i].state == P_USED) {
      curr_proc->phys_pages[i].counter = curr_proc->phys_pages[i].counter >> 1; //trim the LSB
      pte = walk(curr_proc->pagetable, (uint64)curr_proc->phys_pages[i].virtual_add, 0);
      if((*pte & PTE_A) > 0) { //access bit is on
        curr_proc->phys_pages[i].counter = curr_proc->phys_pages[i].counter | num; //turn on the MSB
        *pte = (*pte & ~PTE_A); //turn off access bit
      }
    }
  }
}

void
remove_page_from_memo(pte_t *pte, uint add, struct page *arr)
{
    struct page* p;
    for(int i=0; i<MAX_PSYC_PAGES; i++) {
        if((arr[i].virtual_add == add) && (arr[i].state == P_USED)) { //find the physical page matches the virtual address nad reset its' values
            p = &(arr[i]);
            p->counter = 0;
            p->table = 0;
            p->virtual_add = 0;
            p->c_time = 0;
            p->state = P_UNUSED;
            p->offset = 0;
            #if LAPA
                p->counter = 0xFFFFFFFF;
            #endif
            break;
        }
    }
}