
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "userprog/fd.h"
#include "userprog/process.h"



void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void syscall_check_vaddr(uint64_t va, struct thread *curr);


/************************************************************************/
/* syscall, project 2  */
#ifdef USERPROG 

#include "filesys/filesys.h"
#include "filesys/file.h"
#include "string.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"

typedef void 
syscall_handler_func(struct intr_frame *);

static syscall_handler_func 
*syscall_handlers[25]; // 25는 총 syscall 갯수;

#define get_user_fd(thread) (thread->fd_table)

static struct file* 
get_user_file(int fd);

static bool
is_vaddr_valid(void* vaddr);

static void
halt_handler(struct intr_frame *f);

static void
exit_handler(struct intr_frame* f);

static void 
fork_handler(struct intr_frame* f);

static void
exec_handler(struct intr_frame* f);

static void 
wait_handler(struct intr_frame* f);

static void
create_handler(struct intr_frame* f);

static void
remove_hander(struct intr_frame* f);

static void
read_handler(struct intr_frame* f);

static void 
open_handler(struct intr_frame *f);

static void 
filesize_handler(struct intr_frame* f);

static void  
write_handler(struct intr_frame* f);

static void
seek_handler(struct intr_frame* f);

static void
tell_handler(struct intr_frame* f);

static void
close_handler(struct intr_frame* f);


#endif
/* syscall, project 2 */
/************************************************************************/

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	
	
#ifdef USERPROG /* syscall, project 2 */

	// /* 자 지금부터 열려있는 모든 파일은 커널이 관리합니다. */
	memset(syscall_handlers, 0 , sizeof(syscall_handlers)); // sycall 함수배열 초기화

	syscall_handlers[SYS_HALT] = halt_handler;
	syscall_handlers[SYS_EXIT] = exit_handler;
	syscall_handlers[SYS_FORK] = fork_handler;
	syscall_handlers[SYS_EXEC] = exec_handler;
	syscall_handlers[SYS_WAIT] = wait_handler;
	syscall_handlers[SYS_CREATE] = create_handler;
	syscall_handlers[SYS_REMOVE] = remove_hander;
	syscall_handlers[SYS_READ] = read_handler;
	syscall_handlers[SYS_OPEN] = open_handler;
	syscall_handlers[SYS_FILESIZE] = filesize_handler;
	syscall_handlers[SYS_WRITE] = write_handler; 
	syscall_handlers[SYS_SEEK] = seek_handler;
	syscall_handlers[SYS_TELL] = tell_handler;
	syscall_handlers[SYS_CLOSE] = close_handler;

#endif /* syscall, project 2 */
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

#ifdef USERPROG /* syscall, project 2 */

	syscall_handler_func *handler;
	int sys_num;
	
	sys_num = f->R.rax;
	handler = syscall_handlers[sys_num];
	handler(f);

#endif /* syscall, project 2 */
}


/******************************************************/
/* userprogram, project 2 */
#ifdef USERPROG

static void
halt_handler(struct intr_frame *f){
	// power_off();
	// NOT_REACHED();
}

static void 
exit_handler(struct intr_frame* f){
	struct thread *current = thread_current();
	int exit_code = f->R.rdi;
	current->exit_code = exit_code;
	current->tf.R.rax = exit_code;
	thread_exit();
}

static void 
fork_handler(struct intr_frame* f){
 	char *thread_name = (char *)f->R.rdi;
	syscall_check_vaddr(thread_name, thread_current());
	f->R.rax = process_fork(thread_name, f);
}

static void
exec_handler(struct intr_frame* f)
{ 	
	int fd;
	const char *fn_copy;
	const char* file_name = f->R.rdi;
	syscall_check_vaddr(file_name, thread_current());
	if((fn_copy =  palloc_get_page(PAL_USER)) == NULL){
		/* test */
		thread_current()->exit_code = -1;
		thread_exit();
	}

	strlcpy(fn_copy, file_name, strlen(file_name) + 1);
	f->R.rax = process_exec(fn_copy);
}

static void 
wait_handler(struct intr_frame* f)
{
 	int pid = (int)f->R.rdi;
	int exit_code = process_wait(pid);
	f->R.rax = exit_code;
}

static void
create_handler(struct intr_frame* f)
{
	char* file_name;
	unsigned initial_size;
	
	file_name = f->R.rdi;
	initial_size = f->R.rsi;
	if(!is_vaddr_valid(file_name) || *file_name == NULL){
		thread_current()->exit_code = -1;
		thread_exit();
		NOT_REACHED();
	}

	f->R.rax = filesys_create(file_name, initial_size);
}

static void
remove_hander(struct intr_frame* f)
{
	char *file_name;

	file_name = f->R.rdi;

	f->R.rax = filesys_remove(file_name);
}

static void
read_handler(struct intr_frame* f)
{
	struct file* file;
	int fd = f->R.rdi;
	void *buffer = f->R.rsi;
	unsigned size = f->R.rdx;
	struct thread* t = thread_current();
	syscall_check_vaddr(buffer, t);
	f->R.rax = fd_read(fd ,buffer, size, t->fd_table->fd_array);
}

static void 
open_handler(struct intr_frame *f)
{
	char* file_name = f->R.rdi;
	struct thread* t = thread_current();
	syscall_check_vaddr(file_name, t);

	f->R.rax = fd_open(file_name, t->fd_table->fd_array);
}

static void 
filesize_handler(struct intr_frame* f)
{	
	struct file *file;
	int fd = f->R.rdi;

	f->R.rax = fd_filesize(fd, thread_current()->fd_table->fd_array);
}

static void  
write_handler(struct intr_frame* f) // 핸들러를 실행하는 주체는 kernel 모드로 전환한 user prog이다.
{	
	struct file *file;
	int fd = f->R.rdi;
	void *buffer = f->R.rsi;
	unsigned size = f->R.rdx;
	struct thread *t = thread_current();
	syscall_check_vaddr(buffer, t);

	f->R.rax = fd_write(fd, buffer, size, t->fd_table->fd_array);
}

static void
seek_handler(struct intr_frame* f)
{
	struct file *file;
	int fd; 
	unsigned position;

	fd = f->R.rdi;
	position = f->R.rsi;

	if((file = get_user_file(fd)) == NULL){
		f->R.rax = -1;
		return;
	}
	file_seek(file,position);
}

static void
tell_handler(struct intr_frame* f)
{
	struct file *file;
	int fd;

	fd = f->R.rdi;
	if((file = get_user_file(fd)) == NULL){
		f->R.rax = -1;
		return;
	}
	f->R.rax = file_tell(file);
}

static void
close_handler(struct intr_frame* f)
{
	struct file *file;
	int fd;

	fd = f->R.rdi;
	if((file = get_user_file(fd)) == NULL){
		thread_current()->exit_code = -1;
		thread_exit();
	}
	free_fd(get_user_fd(thread_current()),fd);
	file_close(file);
}

static struct file* 
get_user_file(int fd)
{
	struct fd_table *user_fd = get_user_fd(thread_current());
	if(fd < FD_MIN_SIZE || fd > FD_MAX_SIZE)
		return NULL;
	if(is_empty(user_fd,fd))
		return NULL;
		
	//todo 유효한 파일인지 검사, 이미 닫혔는지 아닌지, 가르키는 주소가 파일이 맞는지.
	return get_file(user_fd,fd);
}

static bool
is_vaddr_valid(void* vaddr)
{
	return !(is_kernel_vaddr(vaddr) 
		|| pml4_get_page(thread_current()->pml4, vaddr) == NULL 
		|| vaddr == NULL);
}

#endif

void syscall_check_vaddr(uint64_t va, struct thread *curr) {
	int temp;
	if (!is_user_vaddr(va)) {
		curr->exit_code = -1;
		thread_exit();
	}
	temp = *(int *)va;
	return;
}
/* userprogram, project 2 */
/******************************************************/
