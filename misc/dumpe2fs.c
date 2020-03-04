/*
 * dumpe2fs.c		- List the control structures of a second
 *			  extended filesystem
 *
 * Copyright (C) 1992, 1993, 1994  Remy Card <card@masi.ibp.fr>
 *                                 Laboratoire MASI, Institut Blaise Pascal
 *                                 Universite Pierre et Marie Curie (Paris VI)
 *
 * Copyright 1995, 1996, 1997 by Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

/*
 * History:
 * 94/01/09	- Creation
 * 94/02/27	- Ported to use the ext2fs library
 */

#include "config.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
extern char *optarg;
extern int optind;
#endif
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ext2fs/ext2_fs.h"

#include "ext2fs/ext2fs.h"
#include "e2p/e2p.h"
#include "ext2fs/kernel-jbd.h"
#include <uuid/uuid.h>

#include "support/nls-enable.h"
#include "support/plausible.h"
#include "../version.h"

#include "debugfs.h"
#include "uuid/uuid.h"
#include <ext2fs/ext2_ext_attr.h>
#include <ext2fs/ext2fs.h>

#define in_use(m, x)	(ext2fs_test_bit ((x), (m)))
struct extent_path {
        char            *buf;
        int             entries;
        int             max_entries;
        int             left;
        int             visit_num;
        int             flags;
        blk64_t         end_blk;
        void            *curr;
};

struct ext2_extent_handle {
        errcode_t               magic;
        ext2_filsys             fs;
        ext2_ino_t              ino;
        struct ext2_inode       *inode;
        struct ext2_inode       inodebuf;
        int                     type;
        int                     level;
        int                     max_depth;
        int                     max_paths;
        struct extent_path      *path;
};
static const char * program_name = "dumpe2fs";
static char * device_name = NULL;
static int hex_format = 0;
static int blocks64 = 0;
static int flag = 0;
static __u32 icount;
static struct ext3_extent_header *eh;
static struct ext3_extent_idx *ei;
static struct ext3_extent *ee;

static int is_inode_extent_clear(struct ext2_inode * inode)
{
	errcode_t               retval;

        eh = (struct ext3_extent_header *)inode->i_block;
        ei = (struct ext3_extent_idx *)eh + 1;
        ee = (struct ext3_extent *)eh + 2;
        if (ei->ei_leaf > 1 && LINUX_S_ISREG(inode->i_mode) && inode->i_links_count == 0) {
                fprintf(stdout, "inode: %u extent_block: %d\n", icount, ei->ei_leaf);
		// not clear
		return 0;
        }

	return 1;
}

static int dump_dir_extent(ext2_extent_handle_t handle, struct ext3_extent_header *eh)
{
	struct ext3_extent *ee;
	int i;
	__le32  ee_block;
	__le16 ee_len;
	__u64	ee_start;
	char * buf;
	__u32 headbuflen = 4;
	int retval;

/*
	retval = ext2fs_get_mem(headbuflen, &buf);
	if (retval)
		return;
*/
	ee = eh + 1;
//printf("eh->eh_entries: %d\n", eh->eh_entries);
//printf("eh->eh_max: %d\n", eh->eh_max);
        if (eh->eh_entries > 340) {
                return 0;
        }
	for (i = 1;i < eh->eh_entries + 1;i++) {
//		printf("i: %d\n", i);
		ee_block = ext2fs_le32_to_cpu(ee->ee_block);
/*
		retval = io_channel_read_blk64(handle->fs->io,
				ee_block, 1, buf);
		// printf("bbbbbbbbbbbbbb\n");
		if (retval)
			return retval;

		if (buf != 1852400382) {
			break;
		}
*/
		ee_len = ext2fs_le32_to_cpu(ee->ee_len);
		ee_start = ((__u64) (ext2fs_le16_to_cpu(ee->ee_start_hi) << 32) + 
				(__u64) ext2fs_le32_to_cpu(ee->ee_start));
		printf("%u %u %u %llu\n", icount, ee_block, ee_len, ee_start);
		ee++;
	}
	ext2fs_free_mem(&buf);
	return 1;
}

static int extent_tree_travel(ext2_extent_handle_t handle, struct ext3_extent_header *eh)
{
	struct ext3_extent_header *next;
	struct ext3_extent_idx *ei;
	int i, retval;
	char *buf;
        blk64_t blk;
        int blocksize;

	blocksize = handle->fs->blocksize;
	//printf("Enter extent_tree_travel\n");
	if (eh->eh_depth == 0) {
	//printf("dump_dir_extent\n");
		dump_dir_extent(handle, eh);
	} else if (eh->eh_depth < 4) {
		flag = 1;
	printf("eh->eh_depth < 4\n");
		for (i=1;i<eh->eh_entries+1;i++) {
			retval = ext2fs_get_mem(blocksize, &buf);
			if (retval)
				return;

			memset(buf, 0, blocksize);
			ei = eh + i;
			blk = ext2fs_le32_to_cpu(ei->ei_leaf) +
				((__u64) ext2fs_le16_to_cpu(ei->ei_leaf_hi) << 32);
			retval = io_channel_read_blk64(handle->fs->io,
					blk, 1, buf);
			if (retval)
				return retval;
			next = buf; 
	printf("Recursive\n");
			extent_tree_travel(handle, next);
	printf("Recursive end\n");
			ext2fs_free_mem(&buf);
		}
	} else {
		/* xxxxxxxxxxxxxxx */
		return 0;
	}
	return 1;
}
static int prase_ino_extent(ext2_extent_handle_t handle)
{
	int i, retval;
	struct ext3_extent_idx *ix;
	struct ext3_extent_header *next;
	char *buf;
	blk64_t blk;
	int blocksize;
	struct ext3_extent_header * eh;

	blocksize = handle->fs->blocksize;
	eh = handle->inode->i_block;

//	printf("aaaaaaaaaaaaaa\n");
	retval = ext2fs_get_mem(blocksize, &buf);
	if (retval)
		return;
//		printf("i:%d\n", i);
//		printf("ix->ei_leaf: %d\n", ix->ei_leaf);
//		printf("next->eh_max: %d\n", next->eh_max);
//		printf("next->eh_entries: %d\n", next->eh_entries);
	memset(buf, 0, blocksize);
	for (i=1;i<=4;i++) {
		ix = eh + i;


		blk = ext2fs_le32_to_cpu(ix->ei_leaf) +
			((__u64) ext2fs_le16_to_cpu(ix->ei_leaf_hi) << 32);
		retval = io_channel_read_blk64(handle->fs->io,
				blk, 1, buf);
		if (retval)
			return retval;

		next = buf;
	//printf("bbbbbbbbbbbbbb\n");
		extent_tree_travel(handle, next);
	//printf("cccccccccccccccccc\n");
		/* for last extent */
		if (next->eh_entries < 340) {
//		printf("dddddddddddddddddddd\n");
			break;
		}
	};
	return 1;
}

static int extent_dump(ext2_filsys current_fs, __u32 ino, struct ext3_extent_idx *ei)
{
	ext2_extent_handle_t    handle;
        struct ext2fs_extent    extent;
        struct ext2_extent_info info;
        int                     op = EXT2_EXTENT_NEXT;
        unsigned int            printed = 0;
        errcode_t               errcode;
        blk64_t                         blk;
        struct ext3_extent_idx          *ix = ei;
        errcode_t                       retval;
	struct extent_path      path, *newpath;
	int 			failed_csum;

	errcode = ext2fs_extent_open(current_fs, ino, &handle);
	if (errcode)
		return;

	prase_ino_extent(handle);

	return 1;
}

int main(int argc, char **argv)
{
	errcode_t       retval;
        errcode_t       retval_csum = 0;
        const char      *error_csum = NULL;
        int             print_badblocks = 0;
        blk64_t         use_superblock = 0;
	ext2_filsys     fs;
        int             use_blocksize = 0;
        int             image_dump = 0;
        int             mmp_check = 0;
        int             mmp_info = 0;
        int             force = 0;
        int             flags;
        int             header_only = 0;
        int             c;
        int             grp_only = 0;
	__u32		imax;
	struct ext2_inode       inode;

	if (argc != 2) {
		fprintf(stdout, "argument error!\n");
		exit(1);
	}

	device_name = argv[1];
	flags = EXT2_FLAG_JOURNAL_DEV_OK | EXT2_FLAG_SOFTSUPP_FEATURES |
                EXT2_FLAG_64BITS;
	retval = ext2fs_open(device_name, flags, use_superblock,
        	use_blocksize, unix_io_manager, &fs);

        if (retval) {
                com_err(program_name, retval, _("while trying to open %s"),
                        device_name);
                printf("%s", _("Couldn't find valid filesystem superblock.\n"));
                if (retval == EXT2_ET_BAD_MAGIC)
                        check_plausibility(device_name, CHECK_FS_EXIST, NULL);
                exit(retval);;
        }
	fs->default_bitmap_type = EXT2FS_BMAP64_RBTREE;
        if (ext2fs_has_feature_64bit(fs->super))
                blocks64 = 1;	

//	printf("%u\n", fs->super->s_inodes_count);

	imax = fs->super->s_inodes_count;
	for (icount = 0;icount < imax + 2;icount++) {
		flag = 0;

		//	printf("%u\n", icount);
		retval = ext2fs_read_inode(fs, icount,  &inode);
		if (retval) {
			com_err(program_name, retval, "%s",
					_("while reading journal inode"));
			continue;
		}
		if (!is_inode_extent_clear(&inode)) {
			extent_dump(fs, icount, ei);
			if (flag) {
				printf("treeeeeeeeeeeeeeeeee\n");
			}
			continue;
		}
	}

	exit(0);
}
