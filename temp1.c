#include <stdio.h>
#include "udp.h"
#include "mfs.h"
#include "ufs.h"
#include "message.h"
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>

#define BUFFER_SIZE (4096)

int sd, fd; 
void *mapaddr; 
super_t *s; 
inode_t *inode_ptr; 
MFS_Stat_t mStat;

void intHandler(int dummy) {
    UDP_Close(sd);
    exit(130);
}

int main(int argc, char *argv[]) {  
    signal(SIGINT, intHandler);
    printf("djksandkjsandkldsandklsa\n");
    if (argc != 3) 
    {
        fprintf(stderr, "this is not 3 arguments");
        exit(1);
    } 

    int portnum = atoi(argv[1]);
    sd = UDP_Open(portnum);
    assert(sd > -1);
    if((fd = open(argv[2], O_RDWR|O_SYNC)) == -1) 
    {
        fprintf(stderr, "unable to open fs image");
        exit(1);
    } 
    struct stat sb; 
    if(fstat(fd, &sb) < 0) 
    {
        fprintf(stderr, "fstat error");
        exit(1);
    }
    mapaddr = mmap(NULL, sb.st_size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0); 
    s = (super_t *) mapaddr; 
    inode_ptr = mapaddr + s->inode_region_addr*UFS_BLOCK_SIZE;
    while (1) {
        struct sockaddr_in addr;
        printf("server:: waiting...\n");
        int result; 
        message_t message;
        int rc = UDP_Read(sd, &addr, (char*) &message, BUFFER_SIZE);
        printf("server:: read message [size:%d contents:(%s)]\n", rc, message.buffer);
        if (rc > 0) 
        {
            switch(message.mesType)
            {
                case MFS_INIT:
                    break;
                /*
                case MFS_LOOKUP:
                    result = mfs_lookup(message.inum,message.name);
                    message.inum = result;
                    rc = UDP_Write(sd, &addr, (char*)&message, BUFFER_SIZE);
                    printf("server:: Lookup\n");
                    break;
                case MFS_STAT:
                    result = mfs_stat(message.inum);
                    if(result==-1) message.rc = -1;
                    else if (rc == mStat.size + mStat.type) {
                        message.type = mStat.type;
                        message.nbytes = mStat.size;
                    }
                    rc = UDP_Write(sd, &addr, (char*) &message, BUFFER_SIZE);
                    fprintf(stderr,"Server: Stat Reply %d %d\n",message.type,message.nbytes);
                    break;
                case MFS_WRITE:
                    result = mfs_write(message.inum, message.buffer,message.offset,message.nbytes);
                    message.rc = result;
                    rc = UDP_Write(sd, &addr, (char*)&message, BUFFER_SIZE);
                    fprintf(stderr,"Server: Write Reply\n");
                    break;
                case MFS_READ:
                    result = mfs_read(message.inum,message.buffer,message.offset,message.nbytes);
                    message.rc = result;
                    rc = UDP_Write(sd, &addr, (char*)&message, BUFFER_SIZE);
                    fprintf(stderr,"Server: Read reply\n");
                    break;
                case MFS_CREAT:
                    result = mfs_creat(message.inum,message.type,message.name);
                    message.rc = result;
                    rc = UDP_Write(sd,&addr,(char*)&message,BUFFER_SIZE);
                    fprintf(stderr,"Server: Create Reply\n");
                    break;
                case MFS_UNLINK:
                    result = mfs_unlink(message.inum,message.name);
                    message.rc = result;
                    rc = UDP_Write(sd,&addr,(char*)&message,BUFFER_SIZE);
                    fprintf(stderr,"Server: Unlink Reply\n");
                    break;
                    */
                case MFS_SHUTDOWN:
                    exit(0);
                    break;
            }
        } 
    }
        printf("end\n");

    return 0; 
}
    


unsigned int get_bit(unsigned int *bitmap, int position) {
   int index = position / 32;
   int offset = 31 - (position % 32);
   return (bitmap[index] >> offset) & 0x1;
}

void set_bit(unsigned int *bitmap, int position) {
   int index = position / 32;
   int offset = 31 - (position % 32);
   bitmap[index] |= 0x1 << offset;
} 

void clear_bit(unsigned int *bitmap, int position){
    int index = position / 32;
    int offset = 31 - (position % 32);
    bitmap[index] &= ~(0x1 << offset);
}

int mfs_lookup(int pinum, char *name){
    if (pinum > s->num_inodes || pinum < 0) return -1; 
    inode_t p_inode = inode_ptr[pinum];
    if(p_inode.type != UFS_DIRECTORY) return -1; 

    for(int i = 0; i < DIRECT_PTRS; i++)
    {
        if(p_inode.direct[i] == -1) continue;
        for (int j = 0; j < UFS_BLOCK_SIZE / sizeof(dir_ent_t); j++){
            dir_ent_t *dir_ent = (dir_ent_t *)mapaddr + (s->data_region_addr + p_inode.direct[i])*UFS_BLOCK_SIZE + i*sizeof(dir_ent_t); // could be wrong TODOs
            if(strcmp(dir_ent->name, name) == 0) return dir_ent->inum;
        }
    }
    return -1;
}


int mfs_write(int inum, char *buffer, int offset, int nbytes){
    if (inum < 0 || inum >= s->num_inodes || offset < 0 || nbytes < 0 || nbytes + offset > UFS_BLOCK_SIZE) return -1; 

    char *t = (char *) mapaddr + inode_ptr[inum].direct[offset/UFS_BLOCK_SIZE] * UFS_BLOCK_SIZE + offset % UFS_BLOCK_SIZE;
    memcpy((void *)t, (void *) buffer, nbytes); 
    return 0;
}

int mfs_creat(int pinum, int type, char *name){
    printf("HELLO\n");
    if (strlen(name) > 28) return -1;
    inode_t t = inode_ptr[pinum]; 
    if (t.type != UFS_DIRECTORY) return -1; 
    if(mfs_lookup(pinum, name) != -1) return 0; 
    
    int q = -1; 
    void *inBit = mapaddr + s->inode_bitmap_addr * UFS_BLOCK_SIZE;
    void *daBit = mapaddr + s->data_bitmap_addr * UFS_BLOCK_SIZE;
    int inode_ind = -1, data_ind = -1; 
    
    for (int i = 0; i < s->num_inodes; i++){
        if(get_bit(inBit, i) != 0) continue;
        set_bit((unsigned int *)inBit, i);
        inode_ind = i; 
        break; 
    }
    if (inode_ind == -1) return -1; 

    for (int i = 0; i < s->data_region_len; i++){
        if(get_bit((unsigned int *)daBit, i) == 0) data_ind = i;
    }
    
    int indi = 0, indj= -1; 
    for (int i = 0; i<DIRECT_PTRS; i++){
        if(t.direct[i]!=-1) continue;
        if(t.size%UFS_BLOCK_SIZE == 0){
            indi = i; // last block 
            indj = 0; 
            break;
        }
        else {
            indi = i-1; 
            dir_ent_t *temp = (dir_ent_t *) (mapaddr + t.direct[indi]*UFS_BLOCK_SIZE);
            for (int j = 0; j < UFS_BLOCK_SIZE/sizeof(dir_ent_t); j++){
                if(temp[j].inum == -1) indj = j; 
            }
            break;
        }
    }
    dir_ent_t *dir_ent = (dir_ent_t *)(mapaddr + (t.direct[indi])*UFS_BLOCK_SIZE);
    dir_ent[indj].inum = inode_ind;
    t.size += sizeof(dir_ent_t);
    strcpy(dir_ent[indj].name, name);

    /*
    for (int j = 0; j<DIRECT_PTRS; j++){
        if(t.direct[j] == -1) continue;
        dir_ent_t *dir_ent = (dir_ent_t *)(mapaddr + (t.direct[j])*UFS_BLOCK_SIZE);
        for (int i = 0; i<UFS_BLOCK_SIZE/sizeof(dir_ent_t); i++){
            if(dir_ent[i].inum == -1){    
                q = j;
                dir_ent[i].inum = inode_ind; 
                memcpy(dir_ent[i].name, name, sizeof(name));
                //printf("NAME: %s\n", dir_ent[i].name);
                t.size += sizeof(dir_ent_t);
                break; 
            }
        }
    }
    */

    inode_ptr[inode_ind].type = type;
    
    if (type == UFS_DIRECTORY){
        inode_ptr[inode_ind].direct[0] = data_ind + s->data_region_addr;
        dir_ent_t *qwer = (dir_ent_t *)(mapaddr + (inode_ptr[inode_ind].direct[indi])*UFS_BLOCK_SIZE);
        inode_ptr[inode_ind].size = 2 * (sizeof(dir_ent_t));
        qwer[0].inum = inode_ind;
        strcpy(qwer[0].name, ".");
        qwer[1].inum = pinum; 
        strcpy(qwer[1].name, "..");
        
        set_bit(daBit, data_ind);
        
        for (int i = 1; i<(DIRECT_PTRS); i++){
            inode_ptr[inode_ind].direct[i] = -1;
        }
        
        for (int i = 2; i<UFS_BLOCK_SIZE/sizeof(dir_ent_t); i++){
            qwer[i].inum = -1;
        }
    }
    else 
    {
        inode_ptr[inode_ind].size = 0;
        for (int i = 0; i<(DIRECT_PTRS); i++){
            inode_ptr[inode_ind].direct[i] = -1;
        }
        
    }
    printf("CREAT END\n");
    return 0;
}

int mfs_stat(int inum) {
    if (inum<0 || inum >= s->num_inodes) {
        return -1;
    }
    inode_t inode = inode_ptr[inum];
    if (&inode == 0) {
        return -1;
    }
    mStat.type = inode.type;
    mStat.size = inode.size;
    return inode.type + inode.size;
}

/**
 * reads nbytes of data (max size 4096 bytes) specified by the byte offset offset into the buffer from file specified 
 * by inum. The routine should work for either a file or directory; directories should return data in the format specified 
 * by MFS_DirEnt_t
*/
int mfs_read(int inum, char *buffer, int offset, int nbytes) {
    if (inum<0 || inum >= s->num_inodes) {
        return -1;
    }
    inode_t inode = inode_ptr[inum];
    if (&inode == 0) {
        return -1;
    }
    if (inode.size > offset+nbytes) {
        return -1;
    }

    int dataSize = offset%UFS_BLOCK_SIZE+nbytes;
    if (UFS_BLOCK_SIZE < dataSize ) { //size is over 1 block -> 2 blocks
        void* firstBlock = mapaddr+(inode.direct[offset/UFS_BLOCK_SIZE]) * UFS_BLOCK_SIZE;
        void* firstBlockLoc =  firstBlock+(offset%UFS_BLOCK_SIZE);
        int dif = UFS_BLOCK_SIZE-(offset%UFS_BLOCK_SIZE);
        void* secBlock = mapaddr+(inode.direct[(offset/UFS_BLOCK_SIZE)+1]) * UFS_BLOCK_SIZE;

        memcpy(buffer, firstBlockLoc, dif);
        memcpy((void*)buffer+dif, secBlock, nbytes-dif);
    } else { //size is 1 block
        void* block = mapaddr+(inode.direct[offset/UFS_BLOCK_SIZE]) * UFS_BLOCK_SIZE;
        void* blockLoc =  block+(offset%UFS_BLOCK_SIZE);
        memcpy(buffer, blockLoc, nbytes);
    }
    return 0;
}

/**
 * removes the file or directory name from the directory specified by pinum. 0 on success, -1 on failure. 
 * Failure modes: pinum does not exist, directory is NOT empty. 
 * Note that the name not existing is NOT a failure by our definition (think about why this might be).
*/
int mfs_unlink(int pinum, char *name) {

    if (pinum > s->num_inodes || pinum < 0) return -1; 
    inode_t p_inode = inode_ptr[pinum];
    if(p_inode.type != UFS_DIRECTORY) return -1; 

    for(int i = 0; i < DIRECT_PTRS; i++) {
        if(p_inode.direct[i] == -1) continue;
        for (int j = 0; j < UFS_BLOCK_SIZE / sizeof(dir_ent_t); j++){
            dir_ent_t *dir_ent = (dir_ent_t *)mapaddr + (s->data_region_addr + p_inode.direct[i])*UFS_BLOCK_SIZE + i*sizeof(dir_ent_t); // could be wrong TODOs
            if(strcmp(dir_ent->name, name) == 0) {
                inode_t inode = inode_ptr[dir_ent->inum];

                if (inode.type == MFS_DIRECTORY) {
                    for (int k = 1; k < UFS_BLOCK_SIZE / sizeof(dir_ent_t); k++){
                        dir_ent_t *nodeEntry = (dir_ent_t *)mapaddr + (s->data_region_addr + inode.direct[k])*UFS_BLOCK_SIZE + k*sizeof(dir_ent_t);
                        if (nodeEntry->inum != -1) return -1;
                    }
                }

                dir_ent->inum = -1;
                for(int j = 0; j<=inode.size/UFS_BLOCK_SIZE; j++) {
                    clear_bit((unsigned int*)mapaddr+(s->data_bitmap_addr*UFS_BLOCK_SIZE), inode.direct[j]-s->data_region_addr);
                }
                clear_bit((unsigned int*)mapaddr+(s->inode_bitmap_addr*UFS_BLOCK_SIZE), dir_ent->inum);
                return 0;
            }
        }
    }
    return 0;
}