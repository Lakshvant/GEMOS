#include<context.h>
#include<memory.h>
#include<lib.h>
#include<entry.h>
#include<file.h>
#include<tracer.h>


///////////////////////////////////////////////////////////////////////////
//// 		Start of Trace buffer functionality 		      /////
///////////////////////////////////////////////////////////////////////////

int is_valid_mem_range(unsigned long buff, u32 count, int access_bit) 
{
    struct exec_context *current = get_current_ctx();
	if(current == NULL) return -EINVAL;
		
	
	if(count == 0) return -EINVAL;

	int flag = 0;
	
	int flag2 = -1;
	
	for(int i = 0; i < (MAX_MM_SEGS - 1); i++){
		if((buff >= current->mms[i].start) && ((buff + count - 1) <= ((current -> mms[i].next_free) - 1))){
			flag = 1;
			unsigned int access_flags = current->mms[i].access_flags;
			if(access_flags && (1 << access_bit)){
				flag2 = 1;
			}

		}
	}
	if(!flag){
		if((buff>=current->mms[MM_SEG_STACK].start) && ((buff+count-1)<=((current->mms[MM_SEG_STACK].end)-1))){
			unsigned int access_flags = current->mms[MM_SEG_STACK].access_flags;
			int flag =1;
			if(access_flags && (1<<access_bit)) flag2 =1;
		}
		
	}
	if(!flag){
		struct vm_area *vm_area = current->vm_area;
		
			if((buff>=vm_area->vm_start) && ((buff+count-1)<=((vm_area->vm_end)-1))){
				unsigned int vaccess_flags = vm_area->access_flags;
				flag =1;
				if(vaccess_flags && (1<<access_bit)) flag2=1;
				
			}
	}
		if(flag2 != 1){
		return -EINVAL;
	}

	else if(flag2 == 1){
		return 1;
	}

	return 0;
}



long trace_buffer_close(struct file *filep)
{
	 if (filep->type != TRACE_BUFFER) {
        return -EINVAL; // Invalid file type
    }

    struct trace_buffer_info *trace_buffer = filep->trace_buffer;

    // Deallocate memory for the trace buffer data using os_free
    os_page_free(USER_REG,trace_buffer->trace_buffer);

    // Deallocate memory for the trace buffer info
    os_free(trace_buffer, sizeof(struct trace_buffer_info));

    // Deallocate memory for the fileops object
    os_free(filep->fops, sizeof(struct fileops));

    // Deallocate memory for the file object
    os_page_free(USER_REG,filep);

    return 0; // Success
}



int trace_buffer_read(struct file *filep, char *buff, u32 count)

{   if(count==0){
    return 0;
    }
    unsigned long buffadd=(unsigned long)buff;
    if(is_valid_mem_range(buffadd,count,1)!=1) return -EBADMEM;
    if (filep->mode != O_READ && filep->mode != O_RDWR) {
        return -EINVAL; // Invalid mode
    }
    if (filep->type != TRACE_BUFFER) {
        return -EINVAL; // Invalid file type
    }
    
    struct trace_buffer_info *trace_buffer = filep->trace_buffer;

    // Calculate the available data, accounting for wrap-around if necessary
    int available_data;
    if (trace_buffer->write_offset > trace_buffer->read_offset || (trace_buffer->write_offset==trace_buffer->read_offset&&trace_buffer->mode==0)) {
        available_data = trace_buffer->write_offset - trace_buffer->read_offset;     
    }
    else if((trace_buffer->write_offset==trace_buffer->read_offset&&trace_buffer->mode==1)){
        available_data=TRACE_BUFFER_MAX_SIZE;
    }
     else {
        available_data = TRACE_BUFFER_MAX_SIZE - trace_buffer->read_offset + trace_buffer->write_offset;
    }

    if (count <= 0 || available_data <= 0) {
        return 0; // No data read
    }

    int bytes_to_read = (count < available_data) ? count : available_data;

    // Check if the read operation will wrap-around to the beginning of the buffer
    if (trace_buffer->read_offset + bytes_to_read > TRACE_BUFFER_MAX_SIZE) {
        int remaining_space = TRACE_BUFFER_MAX_SIZE - trace_buffer->read_offset;
        
        // First, read data from the current read offset to the end of the buffer
        char *trace_buf = (char *)trace_buffer->trace_buffer + trace_buffer->read_offset;
        char *user_buf = (char *)buff;

        for (int i = 0; i < remaining_space; i++) {
            user_buf[i] = trace_buf[i];
        }

        // Then, read the remaining data from the beginning of the buffer
        trace_buf = (char *)trace_buffer->trace_buffer;
        user_buf += remaining_space;

        for (int i = 0; i < bytes_to_read - remaining_space; i++) {
            user_buf[i] = trace_buf[i];
        }

        trace_buffer->read_offset = (trace_buffer->read_offset + bytes_to_read) % TRACE_BUFFER_MAX_SIZE;
    } else {
        // Read data without wrap-around
        char *trace_buf = (char *)trace_buffer->trace_buffer + trace_buffer->read_offset;
        char *user_buf = (char *)buff;

        for (int i = 0; i < bytes_to_read; i++) {
            user_buf[i] = trace_buf[i];
        }

        trace_buffer->read_offset += bytes_to_read;
    }
    if(bytes_to_read>0){
        trace_buffer->mode=0;
    }
    return bytes_to_read;
}




int trace_buffer_write(struct file *filep, char *buff, u32 count)
{    if(count==0){
    return 0;
}
    unsigned long buffadd=(unsigned long)buff;
     if(is_valid_mem_range(buffadd,count,1)!=1) return -EBADMEM;
     if ( filep->mode != O_WRITE && filep->mode != O_RDWR) {
        return -EINVAL; // Invalid mode
    }
    if (filep->type != TRACE_BUFFER) {
        return -EINVAL; // Invalid file type
    }

    struct trace_buffer_info *trace_buffer = filep->trace_buffer;

    // Calculate the available space, accounting for wrap-around if necessary
    int available_space;
    if (trace_buffer->read_offset < trace_buffer->write_offset || (trace_buffer->write_offset==trace_buffer->read_offset&&trace_buffer->mode==0)) {
        available_space = TRACE_BUFFER_MAX_SIZE - trace_buffer->write_offset + trace_buffer->read_offset;
    } 
    else if((trace_buffer->write_offset==trace_buffer->read_offset&&trace_buffer->mode==1)){
        available_space=0;
    }
    else {
        available_space = trace_buffer->read_offset - trace_buffer->write_offset;
    }

    if (count <= 0) {
        return 0; // No data written
    }

    int bytes_to_write = (count < available_space) ? count : available_space;

    // Check if data needs to wrap-around to the beginning of the buffer
    if (bytes_to_write > (TRACE_BUFFER_MAX_SIZE - trace_buffer->write_offset)) {
        // First, write data to the end of the buffer
        char *trace_buf = (char *)trace_buffer->trace_buffer + trace_buffer->write_offset;
        char *user_buf = (char *)buff;
        int remaining_space = TRACE_BUFFER_MAX_SIZE - trace_buffer->write_offset;

        for (int i = 0; i < remaining_space; i++) {
            trace_buf[i] = user_buf[i];
        }

        // Then, write the remaining data to the beginning of the buffer
        trace_buf = (char *)trace_buffer->trace_buffer;
        user_buf += remaining_space;
        for (int i = 0; i < bytes_to_write - remaining_space; i++) {
            trace_buf[i] = user_buf[i];
        }

        trace_buffer->write_offset = bytes_to_write - remaining_space;
    } else {
        // Write data without wrap-around
        char *trace_buf = (char *)trace_buffer->trace_buffer + trace_buffer->write_offset;
        char *user_buf = (char *)buff;
        for (int i = 0; i < bytes_to_write; i++) {
            trace_buf[i] = user_buf[i];
        }
        trace_buffer->write_offset += bytes_to_write;
    }
    if(bytes_to_write>0){
        trace_buffer->mode=1;
    }
    return bytes_to_write;
}

int trace_buffer_read2(struct file *filep, char *buff, u32 count)

{   if(count==0){
    return 0;
    }
    unsigned long buffadd=(unsigned long)buff;
    //if(is_valid_mem_range(buffadd,count,1)!=1) return -EBADMEM;
    if (filep->mode != O_READ && filep->mode != O_RDWR) {
        return -EINVAL; // Invalid mode
    }
    if (filep->type != TRACE_BUFFER) {
        return -EINVAL; // Invalid file type
    }
    
    struct trace_buffer_info *trace_buffer = filep->trace_buffer;

    // Calculate the available data, accounting for wrap-around if necessary
    int available_data;
    if (trace_buffer->write_offset > trace_buffer->read_offset || (trace_buffer->write_offset==trace_buffer->read_offset&&trace_buffer->mode==0)) {
        available_data = trace_buffer->write_offset - trace_buffer->read_offset;     
    }
    else if((trace_buffer->write_offset==trace_buffer->read_offset&&trace_buffer->mode==1)){
        available_data=TRACE_BUFFER_MAX_SIZE;
    }
     else {
        available_data = TRACE_BUFFER_MAX_SIZE - trace_buffer->read_offset + trace_buffer->write_offset;
    }

    if (count <= 0 || available_data <= 0) {
        return 0; // No data read
    }

    int bytes_to_read = (count < available_data) ? count : available_data;

    // Check if the read operation will wrap-around to the beginning of the buffer
    if (trace_buffer->read_offset + bytes_to_read > TRACE_BUFFER_MAX_SIZE) {
        int remaining_space = TRACE_BUFFER_MAX_SIZE - trace_buffer->read_offset;
        
        // First, read data from the current read offset to the end of the buffer
        char *trace_buf = (char *)trace_buffer->trace_buffer + trace_buffer->read_offset;
        char *user_buf = (char *)buff;

        for (int i = 0; i < remaining_space; i++) {
            user_buf[i] = trace_buf[i];
        }

        // Then, read the remaining data from the beginning of the buffer
        trace_buf = (char *)trace_buffer->trace_buffer;
        user_buf += remaining_space;

        for (int i = 0; i < bytes_to_read - remaining_space; i++) {
            user_buf[i] = trace_buf[i];
        }

        trace_buffer->read_offset = (trace_buffer->read_offset + bytes_to_read) % TRACE_BUFFER_MAX_SIZE;
    } else {
        // Read data without wrap-around
        char *trace_buf = (char *)trace_buffer->trace_buffer + trace_buffer->read_offset;
        char *user_buf = (char *)buff;

        for (int i = 0; i < bytes_to_read; i++) {
            user_buf[i] = trace_buf[i];
        }

        trace_buffer->read_offset += bytes_to_read;
    }
    if(bytes_to_read>0){
        trace_buffer->mode=0;
    }
    return bytes_to_read;
}




int trace_buffer_write2(struct file *filep, char *buff, u32 count)
{    if(count==0){
    return 0;
}
    unsigned long buffadd=(unsigned long)buff;
     //if(is_valid_mem_range(buffadd,count,1)!=1) return -EBADMEM;
     if ( filep->mode != O_WRITE && filep->mode != O_RDWR) {
        return -EINVAL; // Invalid mode
    }
    if (filep->type != TRACE_BUFFER) {
        return -EINVAL; // Invalid file type
    }

    struct trace_buffer_info *trace_buffer = filep->trace_buffer;

    // Calculate the available space, accounting for wrap-around if necessary
    int available_space;
    if (trace_buffer->read_offset < trace_buffer->write_offset || (trace_buffer->write_offset==trace_buffer->read_offset&&trace_buffer->mode==0)) {
        available_space = TRACE_BUFFER_MAX_SIZE - trace_buffer->write_offset + trace_buffer->read_offset;
    } 
    else if((trace_buffer->write_offset==trace_buffer->read_offset&&trace_buffer->mode==1)){
        available_space=0;
    }
    else {
        available_space = trace_buffer->read_offset - trace_buffer->write_offset;
    }

    if (count <= 0) {
        return 0; // No data written
    }

    int bytes_to_write = (count < available_space) ? count : available_space;

    // Check if data needs to wrap-around to the beginning of the buffer
    if (bytes_to_write > (TRACE_BUFFER_MAX_SIZE - trace_buffer->write_offset)) {
        // First, write data to the end of the buffer
        char *trace_buf = (char *)trace_buffer->trace_buffer + trace_buffer->write_offset;
        char *user_buf = (char *)buff;
        int remaining_space = TRACE_BUFFER_MAX_SIZE - trace_buffer->write_offset;

        for (int i = 0; i < remaining_space; i++) {
            trace_buf[i] = user_buf[i];
        }

        // Then, write the remaining data to the beginning of the buffer
        trace_buf = (char *)trace_buffer->trace_buffer;
        user_buf += remaining_space;
        for (int i = 0; i < bytes_to_write - remaining_space; i++) {
            trace_buf[i] = user_buf[i];
        }

        trace_buffer->write_offset = bytes_to_write - remaining_space;
    } else {
        // Write data without wrap-around
        char *trace_buf = (char *)trace_buffer->trace_buffer + trace_buffer->write_offset;
        char *user_buf = (char *)buff;
        for (int i = 0; i < bytes_to_write; i++) {
            trace_buf[i] = user_buf[i];
        }
        trace_buffer->write_offset += bytes_to_write;
    }
    if(bytes_to_write>0){
        trace_buffer->mode=1;
    }
    return bytes_to_write;
}

int sys_create_trace_buffer(struct exec_context *current, int mode)
{  
	
    // Check if the mode is valid
    if (mode != O_READ && mode != O_WRITE && mode != O_RDWR) {
        return -EINVAL; // Invalid mode
    }
   
    // printk("hi");
    // Find a free file descriptor in the current process's file descriptor array
    int fd;
    for (fd = 0; fd < MAX_OPEN_FILES; fd++) {
        if (current->files[fd] == NULL) {
            break; // Found a free file descriptor
        }
    }

    // If there are no free file descriptors
    if (fd == MAX_OPEN_FILES) {
        return -EINVAL; // No available file descriptors
    }
  // Allocate a struct file object for the trace buffer
    struct file *filep = (struct file *)os_page_alloc(USER_REG);
    if (filep == NULL) {
        return -ENOMEM; // Memory allocation error
    }

    // Initialize the fields of the file object
    filep->type = TRACE_BUFFER;
    filep->mode = mode;
    filep->offp = 0; // Initialize the offset
    filep->ref_count = 1; // Initialize the reference count
    filep->inode = NULL; // For a trace buffer, inode is NULL

    // Allocate a struct trace_buffer_info object
    struct trace_buffer_info *trace_buffer = (struct trace_buffer_info *)os_alloc(sizeof(struct trace_buffer_info));
    if (trace_buffer == NULL) {
        os_free(filep, sizeof(struct file));
        return -ENOMEM; // Memory allocation error
    }

    // Initialize the members of the trace_buffer object 
   

    // Allocate a struct fileops object for trace buffer operations
    struct fileops *fops = (struct fileops *)os_alloc(sizeof(struct fileops));
    if (fops == NULL) {
        os_free(filep, sizeof(struct file));
        os_free(trace_buffer, sizeof(struct trace_buffer_info));
        return -ENOMEM; // Memory allocation error
    }

    // Initialize the function pointers in fops with your implementations for read, write, and close
    
    fops->read = trace_buffer_read;
    fops->write = trace_buffer_write;
    fops->lseek=NULL;
    fops->close = trace_buffer_close;
	 // Allocate a buffer for the trace data
    char* buffer = (char*)os_page_alloc(USER_REG);
    trace_buffer->trace_buffer=buffer;
    trace_buffer->mode=0;
    trace_buffer->read_offset=0;
    trace_buffer->write_offset=0;
  
    // if (trace_buffer->trace_buffer == NULL) {
    //     os_free(filep, sizeof(struct file));
    //     os_free(trace_buffer, sizeof(struct trace_buffer_info));
    //     return -ENOMEM; // Memory allocation error
    // }
    // initialise to avoid lazy allocation
    // for(int i = 0; i < TRACE_BUFFER_MAX_SIZE; i++){
	// 	buffer[i] = '0';
	// }
   
    // Link data structures
    filep->trace_buffer = trace_buffer;
    filep->fops = fops;

    // Set the file descriptor in the process's file descriptor array
    current->files[fd] = filep;
    // Return the allocated file descriptor
    
    return fd;


}

///////////////////////////////////////////////////////////////////////////
//// 		Start of strace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////
// u64 char_u64(char*buffer){
// 	u64 value=0;
// 	value |= (u64)buffer[0] << 0;
// 	value |= (u64)buffer[1] << 8;
// 	value |= (u64)buffer[2] << 16;
// 	value |= (u64)buffer[3] << 24;
// 	value |= (u64)buffer[4] << 32;
// 	value |= (u64)buffer[5] << 40;
// 	value |= (u64)buffer[6] << 48;
// 	value |= (u64)buffer[7] << 56;
// 	return value;
// }

int perform_tracing(u64 syscall_num, u64 param1, u64 param2, u64 param3, u64 param4)
{   

    
	struct exec_context *current = get_current_ctx();
        
	if(current->st_md_base->is_traced == 1 && syscall_num!=38&&syscall_num!=39){
       

	 int fd = current->st_md_base->strace_fd;
	// // int fd=1;
	// // int fd=1;
	 struct file *filep =current->files[fd];
	// if(filep==NULL) return -ENOMEM;
	// printk("%d:syscall\n",syscall_num);
	// printk("%d:trace\n",current->st_md_base->is_traced);
	 struct trace_buffer_info *trace_buffer = filep->trace_buffer;
	 u64 size=-1;
    if(syscall_num==1){
        size=1;
    }
    else if(syscall_num==2){
        size=0;
    }
    else if(syscall_num==10){
        size=0;
    }
    else if(syscall_num==15){
        size=0;
    }
    else if(syscall_num==20){
        size=0;
    }
    else if(syscall_num==12){
        size=1;
    }
    else if(syscall_num==8){
        size=2;
    }
    else if(syscall_num==7){
        size=1;
    }
    else if(syscall_num==4){
        size=2;
    }
    else if(syscall_num==9){
        size=2;
    }
    else if(syscall_num==14){
        size=1;
    }
    else if(syscall_num==13){
        size=0;
    }
    else if(syscall_num==16){
        size=4;
    }
    else if(syscall_num==17){
        size=2;
    }
    else if(syscall_num==18){
        size=3;
    }
    else if(syscall_num==19){
        size=1;
    }
    else if(syscall_num==21){
        size=0;
    }
    else if(syscall_num==22){
        size=0;
    }
    else if(syscall_num==23){
        size=2;
    }
    else if(syscall_num==25){
        size=3;
    }
    else if(syscall_num==24){
        size=3;
    }
    else if(syscall_num==27){
        size=1;
    }
    else if(syscall_num==28){
        size=2;
    }
    else if(syscall_num==29){
        size=1;
    }
    else if(syscall_num==30){
        size=3;
    }
    else if(syscall_num==35){
        size=4;
    }
    else if(syscall_num==36){
        size=1;
    }
    else if(syscall_num==37){
        size=2;
    }
    else if(syscall_num==39){
        size=3;
    }
    else if(syscall_num==41){
        size=3;
    }
    else if(syscall_num==38){
        size=0;
    }
    else if(syscall_num==42){
        size=0;
    }
    else{
        return -EINVAL;
    }
 //    char *buffer; // Allocate a buffer of the same size as the u64

 
    if(current->st_md_base->tracing_mode==FULL_TRACING&&syscall_num!=38){
        char buffer3[8];
            u64 param5=size;
   
            for(int i=0;i<8;i++){
						char x=param5%256;
						param5=param5/256;
						buffer3[i]=x;
					}
            trace_buffer_write2(filep, buffer3, 8);
            // printk("%dsize\n",((u64*)buffer3)[0]);
            param5=syscall_num;
   
            for(int i=0;i<8;i++){
						char x=param5%256;
						param5=param5/256;
						buffer3[i]=x;
					}
            trace_buffer_write2(filep, buffer3, 8);
           // printk("%d\n",((u64*)buffer3)[0]);
         if(size>=1){
            param5=param1;
   
            for(int i=0;i<8;i++){
						char x=param5%256;
						param5=param5/256;
						buffer3[i]=x;
					}
            trace_buffer_write2(filep, buffer3, 8);
            //printk("%d\n",((u64*)buffer3)[0]);
         }
         if(size>=2){
            param5=param2;
   
            for(int i=0;i<8;i++){
						char x=param5%256;
						param5=param5/256;
						buffer3[i]=x;
					}
            trace_buffer_write2(filep, buffer3, 8);
           // printk("%d\n",((u64*)buffer3)[0]);
         }
         if(size>=3){
            param5=param3;
   
            for(int i=0;i<8;i++){
						char x=param5%256;
						param5=param5/256;
						buffer3[i]=x;
					}
            trace_buffer_write2(filep, buffer3, 8);
           // printk("%d\n",((u64*)buffer3)[0]);
         }
         if(size>=4){
            param5=param4;
   
            for(int i=0;i<8;i++){
						char x=param5%256;
						param5=param5/256;
						buffer3[i]=x;
					}
            trace_buffer_write2(filep, buffer3, 8);
          //  printk("%d\n",((u64*)buffer3)[0]);
         }
    }
 
	 else if(current->st_md_base->tracing_mode == FILTERED_TRACING){
	    	struct strace_info *curr;
	     	int flag=0;
	        curr=current->st_md_base->next;
		while(curr){
			if(curr->syscall_num == syscall_num){
				flag=1;
				break;

	 		}
			curr=curr->next;
	 	}
	 	if(flag==1){
           
            char buffer3[8];
            u64 param5=size;
   
            for(int i=0;i<8;i++){
						char x=param5%256;
						param5=param5/256;
						buffer3[i]=x;
					}
            trace_buffer_write2(filep, buffer3, 8);
        
            param5=syscall_num;
   
            for(int i=0;i<8;i++){
						char x=param5%256;
						param5=param5/256;
						buffer3[i]=x;
					}
            trace_buffer_write2(filep, buffer3, 8);
  
         if(size>=1){
            param5=param1;
   
            for(int i=0;i<8;i++){
						char x=param5%256;
						param5=param5/256;
						buffer3[i]=x;
					}
            trace_buffer_write2(filep, buffer3, 8);
            
         }
         if(size>=2){
            param5=param2;
   
            for(int i=0;i<8;i++){
						char x=param5%256;
						param5=param5/256;
						buffer3[i]=x;
					}
            trace_buffer_write2(filep, buffer3, 8);
          
         }
         if(size>=3){
            param5=param3;
   
            for(int i=0;i<8;i++){
						char x=param5%256;
						param5=param5/256;
						buffer3[i]=x;
					}
            trace_buffer_write2(filep, buffer3, 8);
           
         }
         if(size>=4){
            param5=param4;
   
            for(int i=0;i<8;i++){
						char x=param5%256;
						param5=param5/256;
						buffer3[i]=x;
					}
            trace_buffer_write2(filep, buffer3, 8);
           
         }}}

	// 
    }
          
    return 0;
}

int sys_strace(struct exec_context *current, int syscall_num, int action)
{   

	 struct strace_head *strace_head = current->st_md_base;

     struct strace_info* head=strace_head->next;
	if(action == ADD_STRACE) {
        
		struct strace_info *info = os_alloc(sizeof(struct strace_info));
		if(!info) return -ENOMEM;
        info->syscall_num=syscall_num;
        info->next=NULL;
    
		if (head==NULL) {
            
            strace_head->next = info;
            strace_head->last = info;
        } 
		else {
            strace_head->last->next = info;
            strace_head->last = info;
        }

        strace_head->count++;
	}
	if(action == REMOVE_STRACE){
		struct strace_info *prev= NULL;
		struct strace_info *current_info = strace_head->next;
		while(current_info){
			if(current_info->syscall_num == syscall_num){
				if(prev) prev->next=current_info->next;
            else strace_head->next=current_info->next;
			os_free(current_info,sizeof(struct strace_info));
			strace_head->count--;
			break;
			}
			prev=current_info;
			current_info=current_info->next;
		}
	}

	return 0;
}

int sys_read_strace(struct file *filep, char *buff, u64 count)
{
	
   char* buffer=buff;
    char sizebuff[8];
    u64 size;
    int count1=0;
    for(int i=0;i<count;i++){
        trace_buffer_read2(filep,sizebuff,8);
        size=*(u64*)(sizebuff);
        //printk("read_size%d\n",size);
        for(int j=0;j<size+1;j++){
            count1+=trace_buffer_read2(filep,buffer,8);
            buffer+=8;
            
        }
    }
    return count1;

}



int sys_start_strace(struct exec_context *current, int fd, int tracing_mode)
{   
     // Check if system call tracing is already enabled for this process
    // if (current->st_md_base != NULL) {
    //     return -EINVAL; // Tracing is already enabled
    // }

    // Create a new strace_head structure
    // struct strace_head *new_strace_head = os_alloc(sizeof(struct strace_head));
    // if (new_strace_head == NULL) {
    //     return -ENOMEM; // Failed to allocate memory
    // }
    //struct strace_head *new_strace_head=current->st_md_base;
    // Initialize the strace_head structure
    
    current->st_md_base->is_traced = 1; // Enable tracing
    current->st_md_base->strace_fd = fd;
    current->st_md_base->tracing_mode = tracing_mode;
     //new_strace_head;

    // Set st_md_base to the new strace_head
    //current->st_md_base = new_strace_head;
	return 0;
}

int sys_end_strace(struct exec_context *current)
{
    
    // Check if system call tracing is enabled
    if (current->st_md_base != NULL) {
        // Clean up tracing metadata structures
        struct strace_head *tracing_head = current->st_md_base;

        // Set is_traced to 0 to disable tracing
        tracing_head->is_traced = 0;

        // Set st_md_base to NULL to indicate no tracing metadata
        //current->st_md_base = NULL;

        // Iterate through strace_info structures to release allocated memory
       
            struct strace_info *info = tracing_head->next;
            // os_free(tracing_head,sizeof(tracing_head)); // Free the strace_head structure

            // Clean up strace_info structures
            while (info != NULL) {
                struct strace_info *next_info = info->next;
                os_free(info,sizeof(info)); // Free the strace_info structure
                info = next_info;
            }
        tracing_head->next=NULL;
        tracing_head->last=NULL;
        

        return 0; // Success
    }

    return  -EINVAL; // Tracing is not enabled
}



///////////////////////////////////////////////////////////////////////////
//// 		Start of ftrace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////


long do_ftrace(struct exec_context *ctx, unsigned long faddr, long action, long nargs, int fd_trace_buffer)
{ 
    struct ftrace_head* ft_head=ctx->ft_md_base;

    
    if(ft_head=NULL){
        return -EINVAL;
    }
    // Perform different operations based on the action value
   
    switch (action) {
    case ADD_FTRACE:{
    // Check if the list is full
    // if (ft_head->count >= FTRACE_MAX) {
    //     printk("%d",ft_head->count);
    //     return -EINVAL;
    // }
    
    // Check if the function is already in the list
    struct ftrace_info *info = ft_head->next;
     //printk("hi\n");
    while (info!=NULL) {

       
        if (info->faddr == faddr) {
            
            return -EINVAL;
           
        }
        
        info = info->next;
    }

    // Create a new ftrace_info node
    struct ftrace_info *new_info = (struct ftrace_info *)os_alloc(sizeof(struct ftrace_info));
    if (new_info == NULL) {
       
        return -EINVAL;
    }

    new_info->faddr = faddr;
    new_info->num_args = nargs;
    new_info->fd = fd_trace_buffer;
    new_info->capture_backtrace = 0; // Backtrace is initially disabled
    new_info->next = NULL;
    for(int i=0;i<4;i++){
        new_info->code_backup[i]=*((u8*)(faddr)+i);
    }

    // Add the new node to the list
    if (ft_head->next == NULL) {
       
        ft_head->next = new_info;
        ft_head->last = new_info;
    } else {
        
        ft_head->last->next = new_info;
        ft_head->last = new_info;
    }
    ft_head->count++;
    return 0; // Success
}



        
            break;
    case REMOVE_FTRACE:{
            // Check if the function is in the list
    struct ftrace_info *prev = NULL;
    struct ftrace_info *info = ft_head->next;
    while (info) {
        if (info->faddr == faddr) {
            // Disable tracing if it's currently enabled
            // You will need to implement a function to disable tracing
            // Remove the node from the list
            if (prev == NULL) {
                ft_head->next = info->next;
            } else {
                prev->next = info->next;
            }

            // Free the memory allocated for the node
            os_free(info,sizeof(struct ftrace_info));

            ft_head->count--;

            return 0; // Success
        }

        prev = info;
        info = info->next;
    }

    return -EINVAL;}
            break;

        case ENABLE_FTRACE:{
            struct ftrace_info *info = ft_head->next;
        while (info) {
        if (info->faddr == faddr) {
            // Manipulate the address space to trigger invalid opcode fault
            // You will need to implement a function for this
             for(int i=0;i<4;i++){
                *((u8*)(faddr)+i)=INV_OPCODE;
             }
            
            return 0; // Success
        }
        info = info->next;
    }

    return -EINVAL; }
            break;
    case DISABLE_FTRACE:{
             struct ftrace_info *info = ft_head->next;
        while (info) {
        if (info->faddr == faddr) {
            // Manipulate the address space to trigger invalid opcode fault
            // You will need to implement a function for this
             for(int i=0;i<4;i++){
                *((u8*)(faddr)+i)=info->code_backup[i];
             }
            
            return 0; // Success
        }
        info = info->next;
    }

    return -EINVAL; }
            break;
    case ENABLE_BACKTRACE:{
            struct ftrace_info *info = ft_head->next;
        while (info) {
        if (info->faddr == faddr) {
            // Manipulate the address space to trigger invalid opcode fault
            // You will need to implement a function for this
             for(int i=0;i<4;i++){
                *((u8*)(faddr)+i)=info->code_backup[i];
                *((u8*)(faddr)+i)=INV_OPCODE;
                info->capture_backtrace=1;
             }
            
            return 0; // Success
        }
        info = info->next;
    } }
           
            break;
    case DISABLE_BACKTRACE:{
             struct ftrace_info *info = ft_head->next;
        while (info) {
        if (info->faddr == faddr) {
            // Manipulate the address space to trigger invalid opcode fault
            // You will need to implement a function for this
             for(int i=0;i<4;i++){
                *((u8*)(faddr)+i)=info->code_backup[i];
                info->capture_backtrace=0;
             }
            
            return 0; // Success
        }
        info = info->next;
    }

    return -EINVAL; }
            // Disable both function tracing and backtrace for a function
            // Return appropriate values on success or error
            break;
        default:
            return -EINVAL; // Invalid action
    }
    return 0; // Success
}

//Fault handler
long handle_ftrace_fault(struct user_regs *regs)
{
    struct exec_context *ctx = get_current_ctx();
    
    // Check if the current RIP is within the range of traced functions
    struct ftrace_info *info = ctx->ft_md_base->next;
    while (info) {
        if (info->faddr == regs->entry_rip) {
            regs->entry_rsp-=8;
            *((u64*)regs->entry_rsp)=regs->rbp;
            regs->rbp=regs->entry_rsp;
            regs->entry_rip+=4;
            // This is a traced function
            // Save tracing information to the trace buffer
            // calculate size
            u64 size=info->num_args+1;
            if(info->capture_backtrace){
                size++;
           u64 base_ptr=regs->rbp;
           u64 param;
           char buffer[8];
           while(*(u64*)(base_ptr+8)!=END_ADDR){
            param=*(u64*)(base_ptr+8);
            for(int i=0;i<8;i++){
                buffer[i]=param%256;
                param=param/256;
            }
              (base_ptr)=*(u64*)base_ptr;
              size++;
           }
       }    
            char buffer0[8];
            u64 param=size;
            for(int i=0;i<8;i++){
						char x=param%256;
						param=param/256;
						buffer0[i]=x;
					}
            trace_buffer_write2(ctx->files[info->fd], buffer0, 8);
            

            char buffer1[8];
            param=info->faddr;
            for(int i=0;i<8;i++){
						char x=param%256;
						param=param/256;
						buffer1[i]=x;
					}
            trace_buffer_write2(ctx->files[info->fd], buffer1, 8);
            char buffer2[8];
            u64 param2=info->num_args;
    
    // Save the values of function arguments
    // Assuming argument values are in registers rdi, rsi, rdx, rcx, etc.
    if(param2>0){
        char buffer3[8];
            u64 param3=(regs->rdi);
    // Save the number of arguments
            for(int i=0;i<8;i++){
						char x=param3%256;
						param3=param3/256;
						buffer3[i]=x;
					}
            trace_buffer_write2(ctx->files[info->fd], buffer3, 8);
    }
    if(param2>1){
        char buffer3[8];
            u64 param3=regs->rsi;
    // Save the number of arguments
            for(int i=0;i<8;i++){
						char x=param3%256;
						param3=param3/256;
						buffer3[i]=x;
					}
            trace_buffer_write2(ctx->files[info->fd], buffer3, 8);
    }
    if(param2>2){
        char buffer3[8];
            u64 param3=regs->rdx;
    // Save the number of arguments
            for(int i=0;i<8;i++){
						char x=param3%256;
						param3=param3/256;
						buffer3[i]=x;
					}
            trace_buffer_write2(ctx->files[info->fd], buffer3, 8);
    }
    if(param2>3){
        char buffer3[8];
            u64 param3=regs->rcx;
    // Save the number of arguments
            for(int i=0;i<8;i++){
						char x=param3%256;
						param3=param3/256;
						buffer3[i]=x;
					}
            trace_buffer_write2(ctx->files[info->fd], buffer3, 8);
    }
       if(info->capture_backtrace){
           u64 base_ptr=regs->rbp;
           u64 param;
           char buffer1[8];
            param=info->faddr;
           for(int i=0;i<8;i++){
						char x=param%256;
						param=param/256;
						buffer1[i]=x;
					}
            trace_buffer_write2(ctx->files[info->fd], buffer1, 8);
           char buffer[8];
           while(*(u64*)(base_ptr+8)!=END_ADDR){
            param=*(u64*)(base_ptr+8);
            for(int i=0;i<8;i++){
                buffer[i]=param%256;
                param=param/256;
            }
              trace_buffer_write2(ctx->files[info->fd],buffer,8);
              (base_ptr)=*(u64*)base_ptr;
           }
       }
            
            
            return 0; // Success
        }
        info = info->next;
    }

    return -EINVAL; // Invalid opcode fault not associated with a traced function
}


int sys_read_ftrace(struct file *filep, char *buff, u64 count)
{
    char* buffer=buff;
    char sizebuff[8];
    u64 size;
    int count1=0;
    for(int i=0;i<count;i++){
        trace_buffer_read2(filep,sizebuff,8);
        size=*(u64*)(sizebuff);
        for(int j=0;j<size;j++){
           count1+= trace_buffer_read2(filep,buffer,8);
            buffer+=8;
            
        }
    }
    return count1;
}