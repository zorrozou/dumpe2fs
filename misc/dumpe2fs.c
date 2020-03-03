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

static int extent_tree_travel(struct ext3_extent_idx *ei)
{
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


	newpath = &path;
	retval = ext2fs_get_mem(handle->fs->blocksize,
			&newpath->buf);
	/* printf("magic: %d, handle->fs->blocksize: %d, type: %d, level: %d, max_depth: %d, max_paths: %d\n", 
		 handle->magic, handle->fs->blocksize, handle->type, 
 		 handle->level, handle->max_depth, handle->max_paths);
	*/
	if (retval)
		return retval;

	blk = ext2fs_le32_to_cpu(ix->ei_leaf) +
		((__u64) ext2fs_le16_to_cpu(ix->ei_leaf_hi) << 32);
	if ((handle->fs->flags & EXT2_FLAG_IMAGE_FILE) &&
			(handle->fs->io != handle->fs->image_io))
		memset(newpath->buf, 0, handle->fs->blocksize);
	else {
		retval = io_channel_read_blk64(handle->fs->io,
				blk, 1, newpath->buf);
		if (retval)
			return retval;
	}
	handle->level++;

	eh = (struct ext3_extent_header *) newpath->buf;

	retval = ext2fs_extent_header_verify(eh, handle->fs->blocksize);
	if (retval) {
		handle->level--;
		return retval;
	}

	if (!(handle->fs->flags & EXT2_FLAG_IGNORE_CSUM_ERRORS) &&
			!ext2fs_extent_block_csum_verify(handle->fs, handle->ino,
				eh))
		failed_csum = 1;

	newpath->left = newpath->entries =
		ext2fs_le16_to_cpu(eh->eh_entries);
	newpath->max_entries = ext2fs_le16_to_cpu(eh->eh_max);
	ee = eh + 1;
	newpath->end_blk = ext2fs_le32_to_cpu(ix->ei_block);

	//printf("newpath->left||newpath->entries: %d, newpath->max_entries: %d, newpath->end_blk: %d eh->eh_depth: %d\n",
	//	newpath->left, newpath->max_entries, newpath->end_blk, eh->eh_depth);

	__u32 i, ee_block, ee_len;
	__u64 ee_start;

	if (eh->eh_depth != 0) {
		return 1;
	}
	for (i = 1;i<newpath->entries+1;i++) {
		ee_block = ext2fs_le32_to_cpu(ee->ee_block);
		ee_len = ext2fs_le32_to_cpu(ee->ee_len);
		ee_start = ((__u64) (ext2fs_le16_to_cpu(ee->ee_start_hi) << 32) + 
				(__u64) ext2fs_le32_to_cpu(ee->ee_start));
		printf("%u %u %llu\n", ee_block, ee_len, ee_start);
		ee++;
	}
/*
	handle->path = newpath;
        while (1) {
                errcode = ext2fs_extent_get(handle, op, &extent);

		printf("xxxxxxxxxxxxxxx\n");
                if (errcode)
                        break;

                op = EXT2_EXTENT_NEXT;

                if (extent.e_flags & EXT2_EXTENT_FLAGS_SECOND_VISIT)
                        continue;

                if (extent.e_flags & EXT2_EXTENT_FLAGS_LEAF) {
                        continue;
                } else {
                        continue;
                }

                errcode = ext2fs_extent_get_info(handle, &info);
                if (errcode)
                        continue;

                if (!(extent.e_flags & EXT2_EXTENT_FLAGS_LEAF)) {
                        if (extent.e_flags & EXT2_EXTENT_FLAGS_SECOND_VISIT)
                                continue;
                        fprintf(stdout, "%s(ETB%d):%lld",
                                printed ? ", " : "", info.curr_level,
                                extent.e_pblk);
                        printed = 1;
                        continue;
                }
                if (extent.e_len == 0)
                        continue;
                else if (extent.e_len == 1)
                        fprintf(stdout,
                                "%s(%lld%s):%lld",
                                printed ? ", " : "",
                                extent.e_lblk,
                                extent.e_flags & EXT2_EXTENT_FLAGS_UNINIT ?
                                "[u]" : "",
                                extent.e_pblk);
                else
                        fprintf(stdout,
                                "%s(%lld-%lld%s):%lld-%lld",
                                printed ? ", " : "",
                                extent.e_lblk,
                                extent.e_lblk + (extent.e_len - 1),
                                extent.e_flags & EXT2_EXTENT_FLAGS_UNINIT ?
				"[u]" : "",
				extent.e_pblk,
				extent.e_pblk + (extent.e_len - 1));
		printed = 1;
	}
	if (printed)
		fprintf(stdout, "\n");
	ext2fs_extent_free(handle);
*/
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
		
//	printf("%u\n", icount);
		retval = ext2fs_read_inode(fs, icount,  &inode);
		if (retval) {
			com_err(program_name, retval, "%s",
					_("while reading journal inode"));
			continue;
		}
		if (!is_inode_extent_clear(&inode)) {
			extent_dump(fs, icount, ei);
			continue;
		}
	}

	exit(0);
}
