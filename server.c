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
    mapaddr = mmap(NULL, sb.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0); 
    s = (super_t *) mapaddr; 
    inode_ptr = (inode_t *)((char *)mapaddr + s->inode_region_addr*UFS_BLOCK_SIZE);
    while (1) {
        struct sockaddr_in addr;
        //printf("server:: waiting...\n");
        int result; 
        message_t message;
        int rc = UDP_Read(sd, &addr, (char*) &message, sizeof(message_t));
        //printf("server:: read message [size:%d contents:(%d)]\n", rc, message.mesType);
        if (rc > 0) 
        {
            switch(message.mesType)
            {
                case MFS_INIT:
                    break;
                case MFS_LOOKUP:
                    message.rc = mfs_lookup(message.inum, message.name);
                    rc = UDP_Write(sd, &addr, (char*)&message, sizeof(message_t));
                    break;
                case MFS_STAT:
                    if(message.inum >= s->num_inodes) return -1; 
                    inode_t t = inode_ptr[message.inum];
                    fprintf(stderr, "SERVER:: type: %d size %d\n", t.type, t.size);
                    message.type = t.type;
                    message.nbytes = t.size; 
                    rc = UDP_Write(sd, &addr, (char*) &message, sizeof(message_t));
                    //fprintf(stderr,"Server: Stat Reply %d %d\n",message.type,message.nbytes);
                    break;
                case MFS_WRITE:
                    message.rc = mfs_write(message.inum, message.buffer, message.offset, message.nbytes);
                    msync(mapaddr, sb.st_size, MS_SYNC);
                    rc = UDP_Write(sd, &addr, (char*)&message, sizeof(message_t));
                    //fprintf(stderr,"Server: Write Reply\n");
                    break;
                case MFS_READ:
                    message.rc = mfs_read(message.inum,message.buffer,message.offset,message.nbytes);
                    rc = UDP_Write(sd, &addr, (char*)&message, sizeof(message_t));
                    //fprintf(stderr,"Server: Read reply\n");
                    break;
                case MFS_CREAT:
                    message.rc = mfs_creat(message.inum,message.type,message.name);
                    msync(mapaddr, sb.st_size, MS_SYNC);
                    rc = UDP_Write(sd,&addr,(char*)&message,sizeof(message_t));
                    //fprintf(stderr,"Server: Create Reply\n");
                    break;
                case MFS_UNLINK:
                    message.rc  = mfs_unlink(message.inum,message.name);
                    msync(mapaddr, sb.st_size, MS_SYNC);
                    rc = UDP_Write(sd,&addr,(char*)&message,sizeof(message_t));
                    //fprintf(stderr,"Server: Unlink Reply\n");
                    break;
                case MFS_SHUTDOWN:
                    message.rc = 0;
                    msync(mapaddr, sb.st_size, MS_SYNC);
                    UDP_Write(sd, &addr, (char *)&message, sizeof(message_t));
                    exit(0);
                    break;
            }
        } 
    }
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
    if (pinum < 0) return -1; 
    inode_t p_inode = inode_ptr[pinum];
    //printf("MFS_LOOKUP SERVER::: type: %d\n", p_inode.type);
    if(p_inode.type != UFS_DIRECTORY) return -1; 

    for(int i = 0; i < DIRECT_PTRS; i++)
    {
        if(p_inode.direct[i] != -1){
            dir_ent_t *dir_ent = (dir_ent_t *)(mapaddr + (p_inode.direct[i])*UFS_BLOCK_SIZE);
            for (int j = 0; j < UFS_BLOCK_SIZE/sizeof(dir_ent_t); j++){
                if(strcmp(dir_ent[j].name, name) == 0) return dir_ent[j].inum;
            }
        }
    }
    return -1;
}


int mfs_write(int inum, char *buffer, int offset, int nbytes){
        // if (inum < 0 || inum >= s->num_inodes || offset < 0 || nbytes < 0 || nbytes + offset > UFS_BLOCK_SIZE){
    //     fprintf(stderr,"Server: mfs_write inside initial check\n");
    //     return -1; 
    // }
    if (inum < 0){
        fprintf(stderr,"Server: mfs_write inum<0 \n");
        return -1; 
    }
    // if (inum >= s->num_inodes){
    //     fprintf(stderr,"Server: mfs_write inum %d >= s->numinodes %d \n", inum, s->num_inodes);
    //     return -1; 
    // }
    if (offset < 0){
        fprintf(stderr,"Server: mfs_write offset < 0 \n");
        return -1; 
    }
    if (nbytes < 0){
        fprintf(stderr,"Server: mfs_write nbytes < 0 \n");
        return -1; 
    }
    if (nbytes + offset > DIRECT_PTRS*UFS_BLOCK_SIZE){
        fprintf(stderr,"Server: mfs_write nbytes %d + offset %d > UFS_BLOCK_SIZE %d \n", nbytes, offset, UFS_BLOCK_SIZE);
        return -1; 
    }

    char *t = (char *) mapaddr + inode_ptr[inum].direct[offset/UFS_BLOCK_SIZE] * UFS_BLOCK_SIZE + offset % UFS_BLOCK_SIZE;
    inode_ptr[inum].size += nbytes;
    memcpy((void *)t, (void *) buffer, nbytes); 
    return 0;
}

int mfs_creat(int pinum, int type, char *name){
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

    
    int indi = 0, indj= 0; 
    if(t.size%UFS_BLOCK_SIZE == 0){
            indi = t.size/UFS_BLOCK_SIZE; // last block 
            indj = 0; 
    }
    else{
        for (int i = 0; i<DIRECT_PTRS; i++){
            if(t.direct[i]!=-1) continue;
            indi = i-1; 
            dir_ent_t *temp = (dir_ent_t *) (mapaddr + t.direct[indi]*UFS_BLOCK_SIZE);
            for (int j = 0; j < UFS_BLOCK_SIZE/sizeof(dir_ent_t); j++){
                if(temp[j].inum == -1) indj = j; 
            }
            break;
        }

    }
    dir_ent_t *dir_ent = (dir_ent_t *)(mapaddr + (t.direct[indi])*UFS_BLOCK_SIZE);
    //fprintf(stderr, "SERVER CREAT INDI %d INDJ %d\n", indi, indj);
    dir_ent[indj].inum = inode_ind;
    t.size += sizeof(dir_ent_t);
    strcpy(dir_ent[indj].name, name);

    inode_ptr[inode_ind].type = type;
    inode_ptr[inode_ind].size = 0;
    if (type == UFS_DIRECTORY){
        set_bit(daBit, data_ind);
        inode_ptr[inode_ind].direct[0] = data_ind + s->data_region_addr;
        for (int i = 1; i<(DIRECT_PTRS); i++){
            inode_ptr[inode_ind].direct[i] = -1;
        }
        dir_ent_t *dir_ent1 = (dir_ent_t *)(mapaddr + (inode_ptr[inode_ind].direct[indi])*UFS_BLOCK_SIZE);
        
        for (int i = 2; i<UFS_BLOCK_SIZE/sizeof(dir_ent_t); i++){
            dir_ent1[i].inum = -1;
        }

        inode_ptr[inode_ind].size = 2 * (sizeof(dir_ent_t));
        dir_ent1[0].inum = inode_ind;
        strcpy(dir_ent1[0].name, ".");
        dir_ent1[1].inum = pinum; 
        strcpy(dir_ent1[1].name, "..");
        
    }
    else 
    {
        data_ind = -1;
        for (int i = 0; i<(DIRECT_PTRS); i++){
            for (int j = 0; j < s->data_region_len; j++){
                if(get_bit((unsigned int *)daBit, j) == 0) {data_ind = j; break;}
            }
            set_bit(daBit, data_ind);
            inode_ptr[inode_ind].direct[i] = data_ind + s->data_region_addr;
        }
        
    }
    printf("CREAT END\n");
    return 0;
}


/**
 * reads nbytes of data (max size 4096 bytes) specified by the byte offset offset into the buffer from file specified 
 * by inum. The routine should work for either a file or directory; directories should return data in the format specified 
 * by MFS_DirEnt_t
*/
int mfs_read(int inum, char *buffer, int offset, int nbytes) {
    // if (inum<0 || inum >= s->num_inodes) {
    //     return -1;
    // }
    if (inum<0) {
        fprintf(stderr,"Server: mfs_read inum<0 \n");
        return -1;
    }
    if(offset/UFS_BLOCK_SIZE >= 30) {
        return -1;
    }
    inode_t *inode = &inode_ptr[inum];
    if (inode == 0) {
        fprintf(stderr,"Server: mfs_read &inode == 0");
        return -1;
    }


    // Check if the read spans across one or two blocks
    if (offset % UFS_BLOCK_SIZE + nbytes <= UFS_BLOCK_SIZE) {
        // One block
        char* block = (char*)mapaddr + (inode->direct[offset / UFS_BLOCK_SIZE]) * UFS_BLOCK_SIZE;
        memcpy((void*)buffer, (void*)(block + offset % UFS_BLOCK_SIZE), nbytes);
    } else {
        // Two blocks
        char* block1 = (char*)mapaddr + (inode->direct[offset / UFS_BLOCK_SIZE]) * UFS_BLOCK_SIZE;
        char* block2 = (char*)mapaddr + (inode->direct[offset / UFS_BLOCK_SIZE + 1]) * UFS_BLOCK_SIZE;
        memcpy((void*)buffer, (void*)(block1 + offset % UFS_BLOCK_SIZE), UFS_BLOCK_SIZE - offset % UFS_BLOCK_SIZE);
        memcpy((void*)(buffer + UFS_BLOCK_SIZE - offset % UFS_BLOCK_SIZE), (void*)(block2), nbytes - (UFS_BLOCK_SIZE - offset % UFS_BLOCK_SIZE));
    }
    // int dataSize = offset%UFS_BLOCK_SIZE+nbytes;
    // if (UFS_BLOCK_SIZE < dataSize ) { //size is over 1 block -> 2 blocks
    //     fprintf(stderr,"Server: mfs_read 2 blocks\n");
    //     char* firstBlock = (char*)mapaddr+(inode->direct[offset/UFS_BLOCK_SIZE]) * UFS_BLOCK_SIZE;
    //     char* firstBlockLoc = (char*)firstBlock+(offset%UFS_BLOCK_SIZE);
    //     int dif = UFS_BLOCK_SIZE-(offset%UFS_BLOCK_SIZE);
    //     char* secBlock = (char*)mapaddr+(inode->direct[offset/UFS_BLOCK_SIZE+1]) * UFS_BLOCK_SIZE;

    //     memcpy((void*)buffer, (void*)firstBlockLoc, dif);
    //     memcpy((void*)buffer+dif, (void*)secBlock, nbytes-dif);
    // } else { //size is 1 block
    //     fprintf(stderr,"Server: mfs_read 1 block\n");
    //     char* block = (char*)mapaddr+(inode->direct[offset/UFS_BLOCK_SIZE]) * UFS_BLOCK_SIZE;
    //     char* blockLoc = (char*)block+(offset%UFS_BLOCK_SIZE);
    //     memcpy((void*)buffer, (void*)blockLoc, nbytes);
    // }
    return 0;

}

/**
 * removes the file or directory name from the directory specified by pinum. 0 on success, -1 on failure. 
 * Failure modes: pinum does not exist, directory is NOT empty. 
 * Note that the name not existing is NOT a failure by our definition (think about why this might be).
*/
int mfs_unlink(int pinum, char *name) {
    if (pinum > s->num_inodes || pinum < 0) return -1; 
    inode_t *p_inode = &inode_ptr[pinum];
    if(p_inode->type != UFS_DIRECTORY) return -1; 
    for(int i = 0; i < DIRECT_PTRS; i++) {
        if(p_inode->direct[i] == -1) continue;
        for (int j = 0; j < UFS_BLOCK_SIZE; j+= sizeof(dir_ent_t)){
            dir_ent_t *dir_ent = (dir_ent_t *)((char*)mapaddr + (p_inode->direct[i])*UFS_BLOCK_SIZE + j); // could be wrong TODOs
            if(strcmp(dir_ent->name, name) == 0) {
                inode_t *inode = &inode_ptr[dir_ent->inum];
                //fprintf(stderr,"Server mfs_unlink: before check, inode type %d\n", inode->type);
                if (inode->type == UFS_DIRECTORY) {
                    fprintf(stderr,"Server mfs_unlink: inode is dir, max val %d\n", inode->size/sizeof(dir_ent_t));
                    for (int k = 2; k < UFS_BLOCK_SIZE/sizeof(dir_ent_t); k++){
                        dir_ent_t *nodeEntry = (dir_ent_t *)((char*)mapaddr+(inode->direct[i]*UFS_BLOCK_SIZE)+k*sizeof(dir_ent_t));
                        //fprintf(stderr,"Server mfs_unlink: inside k for loop %s\n", nodeEntry->name);
                        //fprintf(stderr,"Server mfs_unlink: nodeEntry %d inum\n",k, nodeEntry->inum);
                        if (nodeEntry->inum != -1) return -1;
                    }
                }

                dir_ent->inum = -1;
                for(int k = 0; k<=inode->size/UFS_BLOCK_SIZE; k++) {
                    clear_bit((unsigned int*)((char*)mapaddr+(s->data_bitmap_addr*UFS_BLOCK_SIZE)), inode->direct[k]-s->data_region_addr);
                }
                clear_bit((unsigned int*)((char*)mapaddr+(s->inode_bitmap_addr*UFS_BLOCK_SIZE)), dir_ent->inum);
                return 0;
            }
        }
    }

    
    return 0;

}