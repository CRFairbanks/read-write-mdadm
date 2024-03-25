#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "jbod.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "jbod.h"

int global = 0;

// **HE RUNS THROUGH THE TEST CASES AT 44:00 IN THE ASSMT2 VIDEO**

uint32_t encode_op(jbod_cmd_t CMD, int diskID, int blockID){
  uint32_t op = CMD << 26 | diskID << 22 | blockID; // each field shifted to its appropriate chunk of bits
  return op; // return the full 32 bit uint
}

int mdadm_mount(void) {
  if (global == 0){
  global++;
  uint32_t op = encode_op(0,0,0);
  jbod_operation(op,NULL);
  return 1;
  }

  else{return -1;} // will fail if already mounted
}

int mdadm_unmount(void) {
  if(global == 0) {return -1;} // wont allow any other function to run before mount has run
  else{
    uint32_t op = encode_op(JBOD_UNMOUNT,0,0);
    jbod_operation(op,NULL);
    global = 0;
    return 1;
  }
} 

 /// BELOW AREA COMMENTED OUT TO PREVENT ERRORS IN MAKE FILE DURING INITIAL 3 METHOD TESTING

void translated_addr(uint32_t linear_addr, int *diskID, int *blockID, int *offset){
  // to be used for the read func
  *diskID = linear_addr / JBOD_DISK_SIZE;
  int local_addr = linear_addr % JBOD_DISK_SIZE;
  *blockID = local_addr / JBOD_BLOCK_SIZE;
  *offset = local_addr % JBOD_BLOCK_SIZE;
  // void so no return -> passing by reference so actual read values will be manipulated
} // DONE

void seek(int diskID, int blockID) {
  assert(0 <= diskID && diskID <= 15);              // assures the logic of disk from translate_address
  assert(0 <= blockID && blockID <= 255);           // assures the logic of block from translate_address
  uint32_t op = encode_op(JBOD_SEEK_TO_DISK, diskID, 0);
  jbod_operation(op,NULL);
  op = encode_op(JBOD_SEEK_TO_BLOCK, 0, blockID);
  jbod_operation(op, NULL);
}


// address can be between 0 : 1MB-1
int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
  if(global == 0) {return -1;}                        // wont allow any other function to run before mount has run
  else if (addr < 0 || addr > (1048576-1)) {return -1;} // read should fail on an out-of-bound linear address
  else if (len + addr >= (1048576-1)) {return -1;}    // read should fail if it goes beyond the end of the linear address space
  else if (len > 1024) {return -1;}                   // read should fail on larger than 1024-byte I/O sizes
  else if (len != 0 && buf == NULL) {return -1;}      // read should fail when passed a NULL pointer and non-zero length
  else if ((len == 0) && (buf == NULL)) {return len;} // read should succeed with a NULL pointer

  int diskID, blockID, offset;                       // decalre these ints to track the disk block and offset
  translated_addr(addr, &diskID, &blockID, &offset); // calls translate function to get disk, block, and offset
  seek(diskID, blockID);                             // seek and set current block to the one desired
  uint32_t op = encode_op(JBOD_READ_BLOCK, 0, 0);    // make readblock the active command
  uint8_t buf2[256];                                 // the buffer to read the whole block
  uint32_t templen = len;                            // creates a temporary length templen to track how many bytes are left
  

  // *PASSES READ WITHIN BLOCK*
  if(len <= 256-offset){
    jbod_operation(op,buf2);                           // read the contents of current block to buf2 
    memcpy(buf, buf2 + offset, len);                   // copy from temp buf to user's buf of size len
  }

  // PASSES READ ACROSS BLOCKS && READ ACROSS 3 BLOCKS
  else{
    // INITIAL CASE
    jbod_operation(op, buf2);                           // read the info to the middle-man buf
    uint32_t newlen = 256-offset;                       // a new length to store the initial block remainder
    memcpy(buf+0, buf2 + offset, newlen);               // copy the memory from m-m buf to the actual buf
    templen -= (newlen);                                // updates templen to the remaining bytes to copy

    if (blockID == 255){
      seek(diskID+1, 0);                                //seek to next disk if you're on the last block
    }

    // ALL CASES WITH FULL BLOCKS
    while (templen > 256){                              
      jbod_operation(op, buf2);
      memcpy(buf+newlen, buf2, 256);
      templen -= 256;                                   // updates templen for remaining length tracking
      newlen += 256;                                    // updates newlen to be used as the buf addition
    }

    if (blockID == 255){
      seek(diskID+1, 0);                                //seek to next disk if you're on the last block
    }

    // FINAL CASE
    jbod_operation(op, buf2);
    memcpy(buf+newlen, buf2, templen);
  } //end else 

  return len;
} //end mdadm_read




//___________________________________________________________________________________________________________________



// write function will write the info from u-p buffer, of length "len" to the u-p address "addr"
int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {
  if      (global == 0)                 {return -1;}  // wont allow any other function to run before mount has run
  else if (addr < 0 || addr > (1048576)){return -1;}  // read should fail on an out-of-bound linear address
  else if (len + addr > (1048576))      {return -1;}  // read should fail if it goes beyond the end of the linear address space
  else if (len > 1024)                  {return -1;}  // read should fail on larger than 1024-byte I/O sizes
  else if (len != 0 && buf == NULL)     {return -1;}  // read should fail when passed a NULL pointer and non-zero length
  else if ((len == 0) && (buf == NULL)) {return len;} // read should succeed with a NULL pointer
  
  int diskID, blockID, offset;                        // declare these ints to track the disk block and offset
  uint8_t buf2[256];                                  // the buffer to write to the whole block
  uint32_t read = encode_op(JBOD_READ_BLOCK,0,0);     // read operation
  uint32_t write = encode_op(JBOD_WRITE_BLOCK,0,0);   // write operation
  uint8_t tmpbuf[len];                                // a temp for buf so that we dont get the buf const warning?
  memcpy(tmpbuf, buf, len);                           // copy the info from buf to tempbuf to avoid modifying buf

  /*
  NOTE: *Completely reworked this because i feel like i screwed my original loops so badly attempting to fix my mistakes
        *Didn't get a response from piazza quick enough to be able to fix it before 11:59EST
        *Will leave my original code commented out under this attempt so i can see where i went wrong
        *maybe take the bit-by-bit approach from scratch, because it will require the same amount of loops
         (i.e. same runtime given nothing is logarithmic) and it will improve upon my mdadm_read code.
        
  UPDATE: It worked, and for the time being im going to see why my pas
  */

 
  // LOOP FOR THE SIZE OF LEN -- SHOULD HANDLE ALL SIZES IF DONE PROPERLY, UNLIKE MY PREVIOUS IMPLEMENTATION
  for (int i = 0; i < len; i++){
    translated_addr(addr+i, &diskID, &blockID, &offset); // get the next translated address's diskID, blockID, & offset
    seek(diskID, blockID);                               // seek to the next disk&block
    jbod_operation(read, buf2);                          // read the infomration to buf2
    seek(diskID, blockID);                               // seek again **i think this is where i screwed up in old code!!**          
    buf2[offset] = tmpbuf[i];                            // copy user-provided buf info to buf2 starting at offset
    jbod_operation(write, buf2);                         // now call the write operation
  }
 
return len;
} // end mdadm write




  // OLD CODE TAKING THE SAME APPROACH AS MY ORIGINAL READ FUNCTION

 /*
  uint32_t templen = len;
  translated_addr(addr, &diskID, &blockID, &offset);   // get to the desired diskID/blockID/offset values given the addr
  seek(diskID, blockID);                               // seek to desired disk/block 

 // *PASS WRITE WITHIN BLOCK*
  if(len <= 256-offset){
    jbod_operation(write, buf);                        // we can just write buf here and it works -- i couldn't seem to get memcpy to work for me
  }   // this was the only way i could get the 13/17 in case i needed to just hand that in and get points

 // PASSES WRITE ACROSS BLOCKS && WRITE ACROSS 3 BLOCKS
  else{
    // INITIAL CASE
    jbod_operation(read, buf2);                         // read the info of block to block-sized-buf
    uint32_t newlen = 256-offset;                       // a new length to store the initial block remainder
    memcpy(buf2, tmpbuf+offset, newlen);                // copy the memory from the user-provided buf to the block-sized-buf
    jbod_operation(write, buf2);                        // write from the tmpbuf
    templen -= (newlen);                                // updates templen to the remaining bytes to copy

    if (blockID == 255){
      seek(diskID+1, 0);                                // seek to next disk if you're on the last block
    }

    // ALL CASES WITH FULL BLOCKS
    while (templen > 256){                              
      jbod_operation(read, buf2);
      memcpy(buf2+newlen, tmpbuf, 256);                 // copies the info of a whole block's worth to the block-sized buf of user-provided buf contents
      jbod_operation(write, buf2);
      templen -= 256;                                   // updates templen for remaining length tracking
      newlen += 256;                                    // updates newlen to be used as the buf addition
    }

    if (blockID == 255){
      seek(diskID+1, 0);                                // seek to next disk if you're on the last block
    }

    // FINAL CASE
    jbod_operation(read, buf2);
    memcpy(buf2+newlen, tmpbuf, templen);
    jbod_operation(write, buf2);
  } //end else 
 */