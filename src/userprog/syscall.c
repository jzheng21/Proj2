#include "userprog/syscall.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "process.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "userprog/exception.h"
#define MAXCALL 21
#define MaxFiles 200
#define stdin 1
static void syscall_handler(struct intr_frame *);
typedef void (*CALL_PROC)(struct intr_frame *);
CALL_PROC pfn[MAXCALL];
void IWrite(struct intr_frame *);
void IExit(struct intr_frame *f);
void ExitStatus(int status);
void ICreate(struct intr_frame *f);
void IOpen(struct intr_frame *f);
void IClose(struct intr_frame *f);
void IRead(struct intr_frame *f);
void IFileSize(struct intr_frame *f);
void IExec(struct intr_frame *f);
void IWait(struct intr_frame *f);
void ISeek(struct intr_frame *f);
void IRemove(struct intr_frame *f);
void ITell(struct intr_frame *f);
void IHalt(struct intr_frame *f);

static long long page_fault_cnt;

struct file_node *GetFile(struct thread *t, int fd);
void syscall_init(void) {
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
  int i;
  for (i = 0; i < MAXCALL; i++)
    pfn[i] = NULL;
  pfn[SYS_WRITE] = IWrite;
  pfn[SYS_EXIT] = IExit;
  pfn[SYS_CREATE] = ICreate;
  pfn[SYS_OPEN] = IOpen;
  pfn[SYS_CLOSE] = IClose;
  pfn[SYS_READ] = IRead;
  pfn[SYS_FILESIZE] = IFileSize;
  pfn[SYS_EXEC] = IExec;
  pfn[SYS_WAIT] = IWait;
  pfn[SYS_SEEK] = ISeek;
  pfn[SYS_REMOVE] = IRemove;
  pfn[SYS_TELL] = ITell;
  pfn[SYS_HALT] = IHalt;
}
static void syscall_handler(struct intr_frame *f /*UNUSED*/) {
  if (!is_user_vaddr(f->esp))
    ExitStatus(-1);
  int No = *((int *)(f->esp));
  if (No >= MAXCALL || MAXCALL < 0) {
    printf("We don't have this System Call!\n");
    ExitStatus(-1);
  }
  if (pfn[No] == NULL) {
    printf("this System Call %d not Implement!\n", No);
    ExitStatus(-1);
  }
  pfn[No](f);
}
void IWrite(struct intr_frame *f){ //三个参数
    int *esp = (int *)f->esp;
if (!is_user_vaddr(esp + 7))
  ExitStatus(-1);
int fd = *(esp + 2);               //文件句柄
char *buffer = (char *)*(esp + 6); //要输出人缓冲
unsigned size = *(esp + 3);        //输出内容大小。
if (fd == STDOUT_FILENO)           //标准输出设备
{
  putbuf(buffer, size);
  f->eax = 0;
} else //文件
{
  struct thread *cur = thread_current();
  struct file_node *fn = GetFile(cur, fd); //获取文件指针
  if (fn == NULL) {
    f->eax = 0;
    return;
  }
  f->eax = file_write(fn->f, buffer, size); //写文件
}
}
void IExit(struct intr_frame *f) //一个参数正常退出时使用
{
  if (!is_user_vaddr(((int *)f->esp) + 2))
    ExitStatus(-1);
  struct thread *cur = thread_current();
  cur->ret = *((int *)f->esp + 1);
  f->eax = 0;
  thread_exit();
}
void ExitStatus(int status) //非正常退出时使用
{
  struct thread *cur = thread_current();
  cur->ret = status;
  thread_exit();
}
void ICreate(struct intr_frame *f) //两个参数
{
  if (!is_user_vaddr(((int *)f->esp) + 6))
    ExitStatus(-1);
  if ((const char *)*((unsigned int *)f->esp + 4) == NULL) {
    f->eax = -1;
    ExitStatus(-1);
  }
  bool ret = filesys_create((const char *)*((unsigned int *)f->esp + 4),
                            *((unsigned int *)f->esp + 5));
  f->eax = ret;
}
void IOpen(struct intr_frame *f) {
  if (!is_user_vaddr(((int *)f->esp) + 2))
    ExitStatus(-1);
  struct thread *cur = thread_current();
  const char *FileName = (char *)*((int *)f->esp + 1);
  if (FileName == NULL) {
    f->eax = -1;
    ExitStatus(-1);
  }
  struct file_node *fn = (struct file_node *)malloc(sizeof(struct file_node));
  fn->f = filesys_open(FileName);
  if (fn->f == NULL || cur->FileNum >= MaxFiles) //
    fn->fd = -1;
  else
    fn->fd = ++cur->maxfd;
  f->eax = fn->fd;
  if (fn->fd == -1)
    free(fn);
  else {
    cur->FileNum++;
    list_push_back(&cur->file_list, &fn->elem);
  }
}
void IClose(struct intr_frame *f) {
  if (!is_user_vaddr(((int *)f->esp) + 2))
    ExitStatus(-1);
  struct thread *cur = thread_current();
  int fd = *((int *)f->esp + 1);
  f->eax = CloseFile(cur, fd, false);
}
int CloseFile(struct thread *t, int fd, int bAll) {
  struct list_elem *e, *p;
  if (bAll) {
    while (!list_empty(&t->file_list)) {
      struct file_node *fn =
          list_entry(list_pop_front(&t->file_list), struct file_node, elem);
      file_close(fn->f);
      free(fn);
    }
    t->FileNum = 0;
    return 0;
  }
  for (e = list_begin(&t->file_list); e != list_end(&t->file_list);) {
    struct file_node *fn = list_entry(e, struct file_node, elem);
    if (fn->fd == fd) {
      list_remove(e);
      if (fd == t->maxfd)
        t->maxfd--;
      t->FileNum--;
      file_close(fn->f);
      free(fn);
      return 0;
    }
  }
}
void IRead(struct intr_frame *f) {
  int *esp = (int *)f->esp;
  if (!is_user_vaddr(esp + 7))
    ExitStatus(-1);
  int fd = *(esp + 2);
  char *buffer = (char *)*(esp + 6);
  unsigned size = *(esp + 3);
  if (buffer == NULL || !is_user_vaddr(buffer + size)) {
    f->eax = -1;
    ExitStatus(-1);
  }
  struct thread *cur = thread_current();
  struct file_node *fn = NULL;
  unsigned int i;
  if (fd == STDIN_FILENO) //从标准输入设备读
  {
    for (i = 0; i < size; i++)
      buffer[i] = input_getc();
  } else //从文件读
  {
    fn = GetFile(cur, fd); //获取文件指针
    if (fn == NULL) {
      f->eax = -1;
      return;
    }
    f->eax = file_read(fn->f, buffer, size);
  }
}
struct file_node *GetFile(struct thread *t,
                          int fd) //依据文件句柄从进程打开文件表中找到文件指针
{
  struct list_elem *e;
  for (e = list_begin(&t->file_list); e != list_end(&t->file_list);
       e = list_next(e)) {
    struct file_node *fn = list_entry(e, struct file_node, elem);
    if (fn->fd == fd)
      return fn;
  }
  return NULL;
}
void IFileSize(struct intr_frame *f) {
  if (!is_user_vaddr(((int *)f->esp) + 2))
    ExitStatus(-1);
  struct thread *cur = thread_current();
  int fd = *((int *)f->esp + 1);
  struct file_node *fn = GetFile(cur, fd);
  if (fn == NULL) {
    f->eax = -1;
    return;
  }
  f->eax = file_length(fn->f);
}
void IExec(struct intr_frame *f) {
  if (!is_user_vaddr(((int *)f->esp) + 2))
    ExitStatus(-1);
  const char *file = (char *)*((int *)f->esp + 1);
  tid_t tid = -1;
  if (file == NULL) {
    f->eax = -1;
    return;
  }
  char *newfile = (char *)malloc(sizeof(char) * (strlen(file) + 1));
  memcpy(newfile, file, strlen(file) + 1);
  tid = process_execute(newfile);
  struct thread *t = GetThreadFromTid(tid);
  sema_down(&t->SemaWaitSuccess);
  f->eax = t->tid;
  t->father->sons++;
  free(newfile);
  sema_up(&t->SemaWaitSuccess);
}
void IWait(struct intr_frame *f){
  if (!is_user_vaddr(((int *)f->esp) + 2))
    ExitStatus(-1);
  tid_t tid = *((int *)f->esp + 1);
  if (tid != -1) {
    f->eax = process_wait(tid);
  } else
    f->eax = -1;
}
void ISeek(struct intr_frame *f) {
  if (!is_user_vaddr(((int *)f->esp) + 6))
    ExitStatus(-1);
  int fd = *((int *)f->esp + 4);
  unsigned int pos = *((unsigned int *)f->esp + 5);
  struct file_node *fl = GetFile(thread_current(), fd);
  file_seek(fl->f, pos);
}
void IRemove(struct intr_frame *f) {
  if (!is_user_vaddr(((int *)f->esp) + 2))
    ExitStatus(-1);
  char *fl = (char *)*((int *)f->esp + 1);
  f->eax = filesys_remove(fl);
}
void ITell(struct intr_frame *f){
  if (!is_user_vaddr(((int *)f->esp) + 2))
    ExitStatus(-1);
  int fd = *((int *)f->esp + 1);
  struct file_node *fl = GetFile(thread_current(), fd);
  if (fl == NULL || fl->f == NULL) {
    f->eax = -1;
    return;
  }
  f->eax = file_tell(fl->f);
}
void IHalt(struct intr_frame *f) {
  shutdown_power_off();
  f->eax = 0;
}

void record_ret(struct thread *t, int tid, int ret) {
  struct ret_data *rd = (struct ret_data *)malloc(sizeof(struct ret_data));
  rd->ret = ret;
  rd->pid = tid;
  list_push_back(&t->sons_ret, &rd->elem);
}
static void page_fault(struct intr_frame *f) {
  bool not_present; /* True: not-present page, false: writing r/o page. */
  bool write;       /* True: access was write, false: access was read. */
  bool user;        /* True: access by user, false: access by kernel. */
  void *fault_addr; /* Fault address. */
  /* Obtain faulting address, the virtual address that was
  accessed to cause the fault.  It may point to code or to
  data.  It is not necessarily the address of the instruction
  that caused the fault (that's f->eip).
  See [IA32-v2a] "MOV--Move to/from Control Registers" and
  [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
  (#PF)". */
  asm("movl %%cr2, %0" : "=r"(fault_addr));
  /* Turn interrupts back on (they were only off so that we could
  be assured of reading CR2 before it changed). */
  intr_enable();
  /* Count page faults. */
  page_fault_cnt++;
  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;
  if (not_present || (is_kernel_vaddr(fault_addr) && user))
    ExitStatus(-1);
  // if(!not_present&&is_user_vaddr(fault_addr)&&!user)
  //  return;
  /* To implement virtual memory, delete the rest of the function
  body, and replace it with code that brings in the page to
  which fault_addr refers. */
  printf("Page fault at %p: %s error %s page in %s context.\n", fault_addr,
         not_present ? "not present" : "rights violation",
         write ? "writing" : "reading", user ? "user" : "kernel");
  kill(f);
}
