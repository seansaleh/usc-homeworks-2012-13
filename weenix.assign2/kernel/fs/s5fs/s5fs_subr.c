/*
 *   FILE: s5fs_subr.c
 * AUTHOR: afenn
 *  DESCR:
 *  $Id: s5fs_subr.c,v 1.1.2.1 2006/06/04 01:02:15 afenn Exp $
 */

#include "kernel.h"
#include "util/debug.h"
#include "mm/kmalloc.h"
#include "globals.h"
#include "proc/sched.h"
#include "proc/kmutex.h"
#include "errno.h"
#include "util/string.h"
#include "util/printf.h"
#include "mm/pframe.h"
#include "mm/mmobj.h"
#include "drivers/dev.h"
#include "drivers/blockdev.h"
#include "fs/stat.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/s5fs/s5fs_subr.h"
#include "fs/s5fs/s5fs.h"
#include "mm/mm.h"
#include "mm/page.h"

#define dprintf(...) dbg(DBG_S5FS, __VA_ARGS__)

#define s5_dirty_super(fs)                                           \
        do {                                                         \
                pframe_t *p;                                         \
                int err;                                             \
                pframe_get(S5FS_TO_VMOBJ(fs), S5_SUPER_BLOCK, &p);   \
                KASSERT(p);                                          \
                err = pframe_dirty(p);                               \
                KASSERT(!err                                         \
                        && "shouldn\'t fail for a page belonging "   \
                        "to a block device");                        \
        } while (0)


static void s5_free_block(s5fs_t *fs, int block);
static int s5_alloc_block(s5fs_t *);


/*
 * Return the disk-block number for the given seek pointer (aka file
 * position).
 *
 * If the seek pointer refers to a sparse block, and alloc is false,
 * then return 0. If the seek pointer refers to a sparse block, and
 * alloc is true, then allocate a new disk block (and make the inode
 * point to it) and return it.
 *
 * Be sure to handle indirect blocks!
 *
 * If there is an error, return -errno.
 *
 * You probably want to use pframe_get, pframe_pin, pframe_unpin, pframe_dirty.
 */
int
s5_seek_to_block(vnode_t *vnode, off_t seekptr, int alloc)
{
	/*seekptr is a number such as 0,1,2,3,4,5,6,7,8,9,10,etc. It comes from S5_DATA_BLOCK(seek), 
	so it refers to s5_inode.s5_direct_blocks[seekptr], unless it is indirect block... 
			then it is the thing in the indirect block*/	
	uint32_t ret; 
	s5_inode_t *inode = VNODE_TO_S5INODE(vnode);
	
	if (seekptr<S5_NDIRECT_BLOCKS) { /*Direct Block*/
		ret = inode->s5_direct_blocks[seekptr];
	}
	else { /*Indirect block*/
		break_point();
		pframe_t *pf;
		pframe_get(S5FS_TO_VMOBJ(VNODE_TO_S5FS(vnode)), inode->s5_indirect_block, &pf);
		
		/*Make sure we don't try to get more blocks than is possible*/
		KASSERT(seekptr-S5_NDIRECT_BLOCKS < S5_NDIRECT_BLOCKS);
		if(seekptr-S5_NDIRECT_BLOCKS >= S5_NDIRECT_BLOCKS) {
			return -ENOSPC;
		}
		
		ret = ((uint32_t *)(pf->pf_addr))[seekptr-S5_NDIRECT_BLOCKS];
	}
	if (ret == 0) { /*Sparse Block*/
		/*break_point();*/
	}
	
	return ret;
}


/*
 * Locks the mutex for the whole file system
 */
static void
lock_s5(s5fs_t *fs)
{
        kmutex_lock(&fs->s5f_mutex);
}

/*
 * Unlocks the mutex for the whole file system
 */
static void
unlock_s5(s5fs_t *fs)
{
        kmutex_unlock(&fs->s5f_mutex);
}


/*
 * Write len bytes to the given inode, starting at seek bytes from the
 * beginning of the inode. On success, return the number of bytes
 * actually written (which should be 'len', unless there's only enough
 * room for a partial write); on failure, return -errno.
 *
 * This function should allow writing to files or directories, treating
 * them identically.
 *
 * Writing to a sparse block of the file should cause that block to be
 * allocated.  Writing past the end of the file should increase the size
 * of the file. Blocks between the end and where you start writing will
 * be sparse.
 *
 * Do not call s5_seek_to_block() directly from this function.  You will
 * use the vnode's pframe functions, which will eventually result in a
 * call to s5_seek_to_block().
 *
 * You will need pframe_dirty(), pframe_get(), memcpy().
 pframe_dirty(pframe_t *pf)
 pframe_get(struct mmobj *o, uint32_t pagenum, pframe_t **result)
 memcpy(dest, pf->pf_addr + S5_DATA_OFFSET(seek), ret);
 
 */
int
s5_write_file(vnode_t *vnode, off_t seek, const char *bytes, size_t len)
{
	s5fs_t *s5fs = FS_TO_S5FS(vnode->vn_fs);
	pframe_t *pf;
	s5_inode_t *inode = VNODE_TO_S5INODE(vnode);
	lock_s5(s5fs);
	
	uint32_t data_block_num = S5_DATA_BLOCK(seek);
	pframe_get(&vnode->vn_mmobj, data_block_num, &pf);
	
	/*KASSERT not writing past  the end of a block, not supported yet*/
	KASSERT(len+S5_DATA_OFFSET(seek)<=S5_BLOCK_SIZE);
	
	memcpy(pf->pf_addr + S5_DATA_OFFSET(seek), bytes, len);
	
	vnode->vn_len=MAX(VNODE_TO_S5INODE(vnode)->s5_size, seek + len);
	VNODE_TO_S5INODE(vnode)->s5_size= MAX(VNODE_TO_S5INODE(vnode)->s5_size, seek + len);
	
	unlock_s5(s5fs);
	return len;
}

/*
 * Read up to len bytes from the given inode, starting at seek bytes
 * from the beginning of the inode. On success, return the number of
 * bytes actually read, or 0 if the end of the file has been reached; on
 * failure, return -errno.
 *
 * This function should allow reading from files or directories,
 * treating them identically.
 *
 * Reading from a sparse block of the file should act like reading
 * zeros; it should not cause the sparse blocks to be allocated.
 *
 * Similarly as in s5_write_file(), do not call s5_seek_to_block()
 * directly from this function.
 *
 * If the region to be read would extend past the end of the file, less
 * data will be read than was requested.
 *
 * You probably want to use pframe_get(), memcpy().
 */
int
s5_read_file(struct vnode *vnode, off_t seek, char *dest, size_t len)
{
	s5fs_t *s5fs = FS_TO_S5FS(vnode->vn_fs);
	pframe_t *pf;
	s5_inode_t *inode = VNODE_TO_S5INODE(vnode);
	
	uint32_t pagenum;
	off_t curpos = 0;
	
	uint32_t data_block_num = S5_DATA_BLOCK(seek);
	
	lock_s5(s5fs);
	pframe_get(&vnode->vn_mmobj, data_block_num, &pf);

	/* While we only support small files */
	KASSERT(S5_DATA_OFFSET(seek) + len <= S5_BLOCK_SIZE);

	int ret = MAX(0, MIN((off_t)len, inode->s5_size - seek));
		
	memcpy(dest, pf->pf_addr + S5_DATA_OFFSET(seek), ret);
	unlock_s5(s5fs);
	return ret;
}

/*
 * Allocate a new disk-block off the block free list and return it. If
 * there are no free blocks, return -ENOSPC.
 *
 * This will not initialize the contents of an allocated block; these
 * contents are undefined.
 *
 * If the super block's s5s_nfree is 0, you need to refill 
 * s5s_free_blocks and reset s5s_nfree.  You need to read the contents 
 * of this page using the pframe system in order to obtain the next set of
 * free block numbers.
 *
 * Don't forget to dirty the appropriate blocks!
 *
 * You'll probably want to use lock_s5(), unlock_s5(), pframe_get(),
 * and s5_dirty_super()
 */
static int
s5_alloc_block(s5fs_t *fs)
{
        NOT_YET_IMPLEMENTED("S5FS: s5_alloc_block");
        return -1;
}


/*
 * Given a filesystem and a block number, frees the given block in the
 * filesystem.
 *
 * This function may potentially block.
 *
 * The caller is responsible for ensuring that the block being placed on
 * the free list is actually free and is not resident.
 */
static void
s5_free_block(s5fs_t *fs, int blockno)
{
        s5_super_t *s = fs->s5f_super;


        lock_s5(fs);

        KASSERT(S5_NBLKS_PER_FNODE > s->s5s_nfree);

        if ((S5_NBLKS_PER_FNODE - 1) == s->s5s_nfree) {
                /* get the pframe where we will store the free block nums */
                pframe_t *prev_free_blocks = NULL;
                KASSERT(fs->s5f_bdev);
                pframe_get(&fs->s5f_bdev->bd_mmobj, blockno, &prev_free_blocks);
                KASSERT(prev_free_blocks->pf_addr);

                /* copy from the superblock to the new block on disk */
                memcpy(prev_free_blocks->pf_addr, (void *)(s->s5s_free_blocks),
                       S5_NBLKS_PER_FNODE * sizeof(int));
                pframe_dirty(prev_free_blocks);

                /* reset s->s5s_nfree and s->s5s_free_blocks */
                s->s5s_nfree = 0;
                s->s5s_free_blocks[S5_NBLKS_PER_FNODE - 1] = blockno;
        } else {
                s->s5s_free_blocks[s->s5s_nfree++] = blockno;
        }

        s5_dirty_super(fs);

        unlock_s5(fs);
}

/*
 * Creates a new inode from the free list and initializes its fields.
 * Uses S5_INODE_BLOCK to get the page from which to create the inode
 *
 * This function may block.
 */
int
s5_alloc_inode(fs_t *fs, uint16_t type, devid_t devid)
{
        s5fs_t *s5fs = FS_TO_S5FS(fs);
        pframe_t *inodep;
        s5_inode_t *inode;
        int ret = -1;

        KASSERT((S5_TYPE_DATA == type)
                || (S5_TYPE_DIR == type)
                || (S5_TYPE_CHR == type)
                || (S5_TYPE_BLK == type));


        lock_s5(s5fs);

        if (s5fs->s5f_super->s5s_free_inode == (uint32_t) -1) {
                unlock_s5(s5fs);
                return -ENOSPC;
        }

        pframe_get(&s5fs->s5f_bdev->bd_mmobj,
                   S5_INODE_BLOCK(s5fs->s5f_super->s5s_free_inode),
                   &inodep);
        KASSERT(inodep);

        inode = (s5_inode_t *)(inodep->pf_addr)
                + S5_INODE_OFFSET(s5fs->s5f_super->s5s_free_inode);

        KASSERT(inode->s5_number == s5fs->s5f_super->s5s_free_inode);

        ret = inode->s5_number;

        /* reset s5s_free_inode; remove the inode from the inode free list: */
        s5fs->s5f_super->s5s_free_inode = inode->s5_next_free;
        pframe_pin(inodep);
        s5_dirty_super(s5fs);
        pframe_unpin(inodep);


        /* init the newly-allocated inode: */
        inode->s5_size = 0;
        inode->s5_type = type;
        inode->s5_linkcount = 0;
        memset(inode->s5_direct_blocks, 0, S5_NDIRECT_BLOCKS * sizeof(int));
        if ((S5_TYPE_CHR == type) || (S5_TYPE_BLK == type))
                inode->s5_indirect_block = devid;
        else
                inode->s5_indirect_block = 0;

        s5_dirty_inode(s5fs, inode);

        unlock_s5(s5fs);

        return ret;
}


/*
 * Free an inode by freeing its disk blocks and putting it back on the
 * inode free list.
 *
 * You should also reset the inode to an unused state (eg. zero-ing its
 * list of blocks and setting its type to S5_FREE_TYPE).
 *
 * Don't forget to free the indirect block if it exists.
 *
 * You probably want to use s5_free_block().
 */
void
s5_free_inode(vnode_t *vnode)
{
        uint32_t i;
        s5_inode_t *inode = VNODE_TO_S5INODE(vnode);
        s5fs_t *fs = VNODE_TO_S5FS(vnode);

        KASSERT((S5_TYPE_DATA == inode->s5_type)
                || (S5_TYPE_DIR == inode->s5_type)
                || (S5_TYPE_CHR == inode->s5_type)
                || (S5_TYPE_BLK == inode->s5_type));

        /* free any direct blocks */
        for (i = 0; i < S5_NDIRECT_BLOCKS; ++i) {
                if (inode->s5_direct_blocks[i]) {
                        dprintf("freeing block %d\n", inode->s5_direct_blocks[i]);
                        s5_free_block(fs, inode->s5_direct_blocks[i]);

                        s5_dirty_inode(fs, inode);
                        inode->s5_direct_blocks[i] = 0;
                }
        }

        if (((S5_TYPE_DATA == inode->s5_type)
             || (S5_TYPE_DIR == inode->s5_type))
            && inode->s5_indirect_block) {
                pframe_t *ibp;
                uint32_t *b;

                pframe_get(S5FS_TO_VMOBJ(fs),
                           (unsigned)inode->s5_indirect_block,
                           &ibp);
                KASSERT(ibp
                        && "because never fails for block_device "
                        "vm_objects");
                pframe_pin(ibp);

                b = (uint32_t *)(ibp->pf_addr);
                for (i = 0; i < S5_NIDIRECT_BLOCKS; ++i) {
                        KASSERT(b[i] != inode->s5_indirect_block);
                        if (b[i])
                                s5_free_block(fs, b[i]);
                }

                pframe_unpin(ibp);

                s5_free_block(fs, inode->s5_indirect_block);
        }

        inode->s5_indirect_block = 0;
        inode->s5_type = S5_TYPE_FREE;
        s5_dirty_inode(fs, inode);

        lock_s5(fs);
        inode->s5_next_free = fs->s5f_super->s5s_free_inode;
        fs->s5f_super->s5s_free_inode = inode->s5_number;
        unlock_s5(fs);

        s5_dirty_inode(fs, inode);
        s5_dirty_super(fs);
}

/*
 * Locate the directory entry in the given inode with the given name,
 * and return its inode number. If there is no entry with the given
 * name, return -ENOENT.
 *
 * You'll probably want to use s5_read_file and name_match
 *
 * You can either read one dirent at a time or optimize and read more.
 * Either is fine.
 */
int
s5_find_dirent(vnode_t *vnode, const char *name, size_t namelen)
{
	/*Make an actual dirent*/
	s5_dirent_t d;
	off_t offset = 0;
	while (0 != s5_read_file(vnode, offset, &d, sizeof(s5_dirent_t))) {
		offset += sizeof(s5_dirent_t);
		/*dbg_print("Directory Name: %s \n",d.s5d_name);*/
		if (name_match(d.s5d_name, name, namelen))
			return d.s5d_inode;
	}
	return -ENOENT;
}

/*
 * Locate the directory entry in the given inode with the given name,
 * and delete it. If there is no entry with the given name, return
 * -ENOENT.
 *
 * In order to ensure that the directory entries are contiguous in the
 * directory file, you will need to move the last directory entry into
 * the remove dirent's place.
 *
 * When this function returns, the inode refcount on the removed file
 * should be decremented.
 *
 * It would be a nice extension to free blocks from the end of the
 * directory file which are no longer needed.
 *
 * Don't forget to dirty appropriate blocks!
 *
 * You probably want to use vget(), vput(), s5_read_file(),
 * s5_write_file(), and s5_dirty_inode().
 */
int
s5_remove_dirent(vnode_t *vnode, const char *name, size_t namelen)
{
	s5_dirent_t dirent;
	off_t offset = 0;
	while (0 != s5_read_file(vnode, offset, &dirent, sizeof(s5_dirent_t))) {
		if (name_match(dirent.s5d_name, name, namelen)) {
			vnode_t * vn_child = vget(vnode->vn_fs, dirent.s5d_inode);
			
			if (VNODE_TO_S5INODE(vn_child)->s5_type != S5_TYPE_DATA) {
				vput(vn_child);
				return -EISDIR;
			}
			
			/*move the directory entry*/
			/*Read the last directory entry into dirent*/
			s5_read_file(vnode, VNODE_TO_S5INODE(vnode)->s5_size-sizeof(s5_dirent_t), &dirent, sizeof(s5_dirent_t));
			/*Write that dirent into the old dirent's spot (at offset)*/
			s5_write_file(vnode, offset, &dirent, sizeof(s5_dirent_t));
			
			VNODE_TO_S5INODE(vn_child)->s5_linkcount--;
			vnode->vn_len-=sizeof(s5_dirent_t);
			VNODE_TO_S5INODE(vnode)->s5_size-= sizeof(s5_dirent_t);
			
			s5_dirty_inode(FS_TO_S5FS(vnode->vn_fs), VNODE_TO_S5INODE(vnode));
			vput(vn_child);
			return 0;
		}
		offset += sizeof(s5_dirent_t);
	} /*else it doesn't exist*/
	return -ENOENT;
}

/*
 * Create a new directory entry in directory 'parent' with the given name, which
 * refers to the same file as 'child'.
 *
 * When this function returns, the inode refcount on the file that was linked to
 * should be incremented.
 *
 * Remember to incrament the ref counts appropriately
 *
 * You probably want to use s5_find_dirent(), s5_write_file(), and s5_dirty_inode().
 s5_write_file(vnode_t *vnode, off_t seek, const char *bytes, size_t len)
 s5_find_dirent(vnode_t *vnode, const char *name, size_t namelen)
 s5_dirty_inode(fs, inode) 

 
 */
int
s5_link(vnode_t *parent, vnode_t *child, const char *name, size_t namelen)
{
	/*Make sure this link doesn't exist */
	KASSERT(0>=s5_find_dirent(parent, name, namelen));
	/* THIS BREAKS EVERYTHING!!!!
	if (VNODE_TO_S5INODE(child)->s5_type == S5_TYPE_DIR) {
		return -EISDIR;
	}*/
	
	s5_dirent_t entry;
	entry.s5d_inode = VNODE_TO_S5INODE(child)->s5_number;
	strncpy(&entry.s5d_name, name, MIN(namelen, S5_NAME_LEN - 1));
	entry.s5d_name[MIN(namelen, S5_NAME_LEN - 1)] = '\0';
	
	int written_amount = s5_write_file(parent, VNODE_TO_S5INODE(parent)->s5_size, &entry, sizeof(s5_dirent_t));
	KASSERT(written_amount == sizeof(s5_dirent_t));
	
	VNODE_TO_S5INODE(child)->s5_linkcount++;
	
	/*Don't dirty yet*/
	/*s5_dirty_inode(FS_TO_S5FS(parent->vn_fs), VNODE_TO_S5INODE(parent));*/
	return 0;
}

/*
 * Return the number of blocks that this inode has allocated on disk.
 * This should include the indirect block, but not include sparse
 * blocks.
 *
 * This is only used by s5fs_stat().
 *
 * You'll probably want to use pframe_get().
 */
int
s5_inode_blocks(vnode_t *vnode)
{
        NOT_YET_IMPLEMENTED("S5FS: s5_inode_blocks");
        return -1;
}

