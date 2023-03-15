#include "mfs.h"
#include "udp.h"
#include "message.h"
#include <sys/select.h>
#include <time.h>
#include <stdio.h>

int sd;
int rc;
struct sockaddr_in addrSnd, addrRcv;
message_t rMes;
struct timeval tv;

int sendRequest(message_t sMes, message_t rMes){
    fd_set rfds;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    int count = 0;
    while (count < 3) {
        FD_ZERO(&rfds);
        FD_SET(sd, &rfds);
        
        rc = UDP_Write(sd, &addrSnd, (void*) &sMes, sizeof(message_t));
        if (rc<0) {
            printf("Client UDP_Write failed");
            return -1;
        }
        int selectRet = select(sd+1, &rfds, NULL, NULL, &tv);
        if (selectRet < 0) {
            printf("select failed");
            return -1;
        }

        rc = UDP_Read(sd, &addrRcv, (void*) &rMes, sizeof(message_t));
        if (rc<0) {
            count++;
        } else {
            return rc;
        }
    }
    return -1;
}

int MFS_Init(char *hostname, int port) {
    if (hostname == NULL || port < 0) {
        return -1;
    }
    int MIN_PORT = 20000;
    int MAX_PORT = 40000;
    srand(time(0));
    int port_num = (rand() % (MAX_PORT - MIN_PORT) + MIN_PORT); 

    sd = UDP_Open(port_num);
    if (sd < 0) return -1;
    rc = UDP_FillSockAddr(&addrSnd, hostname, port);
    if (rc < 0) return -1;
    return rc;
}
/**
 * takes the parent inode number (which should be the inode number of a directory) and looks up the entry name in it.
 * The inode number of name is returned. 
 * Success: return inode number of name; failure: return -1. 
 * Failure modes: invalid pinum, name does not exist in pinum
*/
int MFS_Lookup(int pinum, char *name) {
    if (pinum < 0 || name == NULL) {
        return -1;
    }
    message_t m;
    m.inum = pinum;
    m.mesType = MFS_LOOKUP;
    strcpy(m.name, name);
    rc = sendRequest(m, rMes);
    if(rMes.mesType != MFS_LOOKUP && rMes.inum < 0) return -1;
    return rMes.inum;
}

/**
 * Returns some information about the file specified by inum. Upon success, return 0, otherwise -1. 
 * The exact info returned is defined by MFS_Stat_t. 
 * Failure modes: inum does not exist. File and directory sizes are described below.
*/
int MFS_Stat(int inum, MFS_Stat_t *m) {
    if (inum < 0 || m == NULL) {
        return -1;
    }
    message_t mess;
    mess.inum = inum;
    mess.mesType = MFS_STAT;
    rc = sendRequest(mess, rMes);
    m->size = rMes.nbytes;
    m->type = rMes.type;
    return 0;
}

/**
 * writes a buffer of size nbytes (max size: 4096 bytes) at the byte offset specified by offset.
 * Returns 0 on success, -1 on failure. 
 * Failure modes: invalid inum, invalid nbytes, invalid offset, 
 * not a regular file (because you can't write to directories).
*/
int MFS_Write(int inum, char *buffer, int offset, int nbytes) {
    if (inum<0 || buffer == NULL || offset<0 || nbytes>4096 || nbytes<0) {
        return -1;
    }
    message_t m;
    m.inum = inum;
    m.offset = offset;
    m.nbytes = nbytes;
    m.mesType = MFS_WRITE;
    strcpy(m.buffer, buffer);
    rc = sendRequest(m, rMes);
    return 0;
}

/**
 * reads nbytes of data (max size 4096 bytes) specified by the byte offset offset into the buffer from file 
 * specified by inum. The routine should work for either a file or directory; 
 * directories should return data in the format specified by MFS_DirEnt_t. 
 * Success: 0, failure: -1. Failure modes: invalid inum, invalid offset, invalid nbytes.
*/
int MFS_Read(int inum, char *buffer, int offset, int nbytes) {
    if (inum<0 || buffer == NULL || offset<0 || nbytes>4096 || nbytes<0) {
        return -1;
    }
    message_t m;
    m.inum = inum;
    m.offset = offset;
    m.nbytes = nbytes;
    m.mesType = MFS_READ;
    strcpy(m.buffer, buffer);
    rc = sendRequest(m, rMes);
    strcpy(buffer, rMes.buffer);
    return 0;
}

/**
 * makes a file (type == MFS_REGULAR_FILE) or directory (type == MFS_DIRECTORY) in the parent directory specified by pinum
 * of name name. Returns 0 on success, -1 on failure. 
 * Failure modes: pinum does not exist, or name is too long. If name already exists, return success.
*/
int MFS_Creat(int pinum, int type, char *name) {
    if (pinum<0 || type<0 || name == NULL || strlen(name) > 28) {
        return -1;
    }
    message_t m;
    m.inum = pinum;
    m.type = type;
    m.mesType = MFS_CREAT;
    strcpy(m.name, name);

    rc = sendRequest(m, rMes);
    return 0;
}

/**
 * removes the file or directory name from the directory specified by pinum. 0 on success, -1 on failure. 
 * Failure modes: pinum does not exist, directory is NOT empty. 
 * Note that the name not existing is NOT a failure by our definition (think about why this might be).
*/
int MFS_Unlink(int pinum, char *name) {
    if (pinum<0 || name == NULL || strlen(name) > 28) {
        return -1;
    }
    message_t m;
    m.inum = pinum;
    m.mesType = MFS_UNLINK;
    strcpy(m.name, name);
    rc = sendRequest(m, rMes);
    return 0;
}

/**
 * just tells the server to force all of its data structures to disk and shutdown by calling exit(0). 
 * This interface will mostly be used for testing purposes.
*/
int MFS_Shutdown() {
    message_t m;
    m.mesType = MFS_SHUTDOWN;
    rc = sendRequest(m, rMes);
    return 0;
}