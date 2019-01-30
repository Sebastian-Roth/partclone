/**
 * apfsclone.c - part of Partclone project
 *
 * Copyright (c) 2007~ Thomas Tsai <thomas at nchc org tw>
 *
 * read apfs super block and bitmap
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdint.h>
#include <malloc.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <linux/types.h>

#include "partclone.h"
#include "apfsclone.h"
#include "progress.h"
#include "fs_common.h"

int APFSDEV;
struct APFS_Superblock_NXSB nxsb;

/// get_apfs_free_count
static uint64_t get_apfs_free_count(){

    struct APFS_Block_8_5_Spaceman apfs_spaceman;
    char *buf;
    int buffer_size = 4096;
    size_t size = 0;

    buf = (char *)malloc(buffer_size);
    memset(buf, 0, buffer_size);

    size = pread(APFSDEV, buf, buffer_size, nxsb.bid_spaceman_area_start*buffer_size);

    memcpy(&apfs_spaceman, buf, sizeof(APFS_Block_8_5_Spaceman));

    // printf("blocks_total %lld\n", apfs_spaceman.blocks_total);
    // printf("blocks_free %lld\n", apfs_spaceman.blocks_free);
    return apfs_spaceman.blocks_free;
}

/// open device
static void fs_open(char* device){

    unsigned long *buf;
    size_t buffer_size = 4096;
    size_t read_size = 0;

    APFSDEV = open(device, O_RDONLY);
    if (APFSDEV == -1){
        log_mesg(0, 1, 1, fs_opt.debug, "%s: Cannot open the partition (%s).\n", __FILE__, device);
    }
    buf = (char *)malloc(buffer_size);
    memset(buf, 0, buffer_size);
    read_size = read(APFSDEV, buf, buffer_size);
    memcpy(&nxsb, buf, sizeof(APFS_Superblock_NXSB));
    // todo : add apfs check!

    // printf("0x%jX\n", nxsb.hdr.checksum);
    // printf("block size %x\n", nxsb.block_size);
    // printf("block count %x\n", nxsb.block_count);
    // printf("next_xid %x\n", nxsb.next_xid);
    // printf("current_sb_start %x\n", nxsb.current_sb_start);
    // printf("current_sb_len %x\n", nxsb.current_sb_len);
    // printf("current_spaceman_start %x\n", nxsb.current_spaceman_start);
    // printf("current_spaceman_len %x\n", nxsb.current_spaceman_len);
    // printf("bid_spaceman_area_start  %x\n", nxsb.bid_spaceman_area_start);

    if(lseek(APFSDEV, 0, SEEK_SET) != 0) {
	log_mesg(0, 1, 1, fs_opt.debug, "%s: device %s seek fail\n", __FILE__, device);
    }

    free(buf);

}

/// close device
static void fs_close(){
    close(APFSDEV);
}

void read_bitmap(char* device, file_system_info fs_info, unsigned long* bitmap, int pui)
{
    unsigned long          *fs_bitmap;
    unsigned long long     bit, block, bused = 0, bfree = 0;
    unsigned long long apfs_block;
    int start = 0;
    int bit_size = 1;

    unsigned long *spaceman_buf;
    unsigned long *bitmap_buf;
    unsigned long *bitmap_entry_buf;
    size_t buffer_size = 4096;
    size_t size = 0;

    struct APFS_BlockHeader apfs_bh;
    struct APFS_TableHeader apfs_th;
    struct APFS_Block_8_5_Spaceman apfs_spaceman;
    size_t k; 

    fs_open(device);

    /// init progress
    progress_bar   prog;	/// progress_bar structure defined in progress.h
    progress_init(&prog, start, fs_info.totalblock, fs_info.totalblock, BITMAP, bit_size);

    //size_t id_mapping_block = nxsb.bid_spaceman_area_start;

    spaceman_buf = (char *)malloc(buffer_size);
    memset(spaceman_buf, 0, buffer_size);
    size = pread(APFSDEV, spaceman_buf, buffer_size, nxsb.bid_spaceman_area_start*buffer_size);
    
    memcpy(&apfs_bh, spaceman_buf, sizeof(APFS_BlockHeader));
    memcpy(&apfs_th, spaceman_buf+sizeof(APFS_BlockHeader), sizeof(APFS_TableHeader));
    memcpy(&apfs_spaceman, spaceman_buf, sizeof(APFS_Block_8_5_Spaceman));

    // printf("block nid %x\n", apfs_bh.nid);
    // printf("block xid %x\n", apfs_bh.xid);
    // printf("block type %x\n", apfs_bh.type);

    // printf("block table entries_cnt %x\n", apfs_th.entries_cnt);

    // printf("blocks_total %lld\n", apfs_spaceman.blocks_total);
    // printf("blocks_free %lld\n", apfs_spaceman.blocks_free);
    // printf("blocks blockid_vol_bitmap_hdr %X\n", apfs_spaceman.blockid_vol_bitmap_hdr);

    // read bitmap
    struct APFS_Block_4_7_Bitmaps apfs_4_7;
    bitmap_buf = (char *)malloc(buffer_size);
    size = pread(APFSDEV, bitmap_buf, buffer_size, apfs_spaceman.blockid_vol_bitmap_hdr*buffer_size);
    memcpy(&apfs_4_7, bitmap_buf, sizeof(APFS_Block_4_7_Bitmaps));
    // printf("block type %x\n", apfs_4_7.hdr.type);
    // printf("block type %x\n", apfs_4_7.hdr.nid);


    for (block = 0; block < fs_info.totalblock; block++){
        pc_clear_bit(block, bitmap, fs_info.totalblock);
    }
    for (k = 0; k < apfs_4_7.tbl.entries_cnt; k++) { 
        // printf("%X, %X, %X, %X, %X\n", apfs_4_7.bmp[k].xid, apfs_4_7.bmp[k].offset, apfs_4_7.bmp[k].bits_total, apfs_4_7.bmp[k].bits_avail, apfs_4_7.bmp[k].block);
        bitmap_entry_buf = (char *)malloc(buffer_size);
        memset(bitmap_entry_buf, 0, buffer_size);
        size = pread(APFSDEV, bitmap_entry_buf, buffer_size, buffer_size*apfs_4_7.bmp[k].block);
        if (apfs_4_7.bmp[k].bits_avail != 0x8000){
            for (block = 0; block < apfs_4_7.bmp[k].bits_total; block++){
                apfs_block = apfs_4_7.bmp[k].offset+block;
                pc_set_bit(apfs_block, bitmap, fs_info.totalblock);
            }
        }
        // for (block = 0; block < apfs_4_7.bmp[k].bits_total; block++){
            // if (pc_test_bit(block, bitmap_entry_buf, fs_info.totalblock) == 0){
            //     apfs_block = apfs_4_7.bmp[k].offset+block;
            //     pc_clear_bit(apfs_block, bitmap, fs_info.totalblock);
            // }
        // }
        /// update progress
        update_pui(&prog, bit, bit, 0);
    } 
    unsigned long used = 0;
    // for (block = 0; block < fs_info.totalblock; block++){
    //     used = pc_test_bit(block, bitmap, fs_info.totalblock);
    //     printf("block status %X:%i \n", block, used);
    // }


    //if(bfree != reiser4_format_get_free(fs->format))
    //    log_mesg(0, 1, 1, fs_opt.debug, "%s: bitmap free count err, bfree:%llu, sfree=%llu\n", __FILE__, bfree, reiser4_format_get_free(fs->format));

    fs_close();
    ///// update progress
    update_pui(&prog, 1, 1, 1);
}

void read_super_blocks(char* device, file_system_info* fs_info)
{
    unsigned long      *fs_bitmap;
    unsigned long long free_blocks = 0;

    fs_open(device);
    free_blocks = get_apfs_free_count();

    strncpy(fs_info->fs, apfs_MAGIC, FS_MAGIC_SIZE);
    fs_info->block_size  = nxsb.block_size;
    fs_info->totalblock  = nxsb.block_count;
    fs_info->usedblocks  = fs_info->totalblock-free_blocks;
    fs_info->device_size = fs_info->totalblock*fs_info->block_size;
    fs_close();
}

