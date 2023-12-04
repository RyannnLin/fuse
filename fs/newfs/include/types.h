#ifndef _TYPES_H_
#define _TYPES_H_

typedef int          boolean;
typedef uint16_t     flag16;

typedef enum newfs_file_type {
    NFS_REG_FILE,
    NFS_DIR,
    NFS_SYM_LINK
} NFS_FILE_TYPE;

#define TRUE                    1
#define FALSE                   0
#define UINT32_BITS             32
#define UINT8_BITS              8

#define NFS_MAGIC_NUM           0x52415453  
#define NFS_SUPER_OFS           0
#define NFS_ROOT_INO            0

#define NFS_ERROR_NONE          0
#define NFS_ERROR_ACCESS        EACCES
#define NFS_ERROR_SEEK          ESPIPE     
#define NFS_ERROR_ISDIR         EISDIR
#define NFS_ERROR_NOSPACE       ENOSPC
#define NFS_ERROR_EXISTS        EEXIST
#define NFS_ERROR_NOTFOUND      ENOENT
#define NFS_ERROR_UNSUPPORTED   ENXIO
#define NFS_ERROR_IO            EIO     /* Error Input/Output */
#define NFS_ERROR_INVAL         EINVAL  /* Invalid Args */

#define NFS_MAX_FILE_NAME       128
#define NFS_INODE_PER_FILE      1
#define NFS_DATA_PER_FILE       6
#define NFS_DEFAULT_PERM        0777

#define NFS_SUPER_BLKS          1           /* 超级块块数 */
#define NFS_MAP_INODE_BLKS      1           /* 索引块位图块数 */
#define NFS_MAP_DATA_BLKS       1           /* 数据块位图块数 */
#define NFS_INODE_BLKS          585         /* 索引块数 */
#define NFS_DATA_BLKS           3508        /* 数据块数 */


#define NFS_IO_SZ()                     (newfs_super.sz_io)
#define NFS_DISK_SZ()                   (newfs_super.sz_disk)
#define NFS_BLK_SZ()                    (newfs_super.sz_blk)
#define NFS_DRIVER()                    (newfs_super.driver_fd)

#define NFS_ROUND_DOWN(value, round)    (value % round == 0            ? value : (value / round) * round)
#define NFS_ROUND_UP(value, round)      (value % round == 0 ? value : (value / round + 1) * round)

#define NFS_BLKS_SZ(blks)               (blks * NFS_BLK_SZ()) // 计算所占空间大小
#define NFS_ASSIGN_FNAME(pnfs_dentry, _fname) memcpy(pnfs_dentry->fname, _fname, strlen(_fname))
#define NFS_INO_OFS(ino)                (newfs_super.data_offset + ino * NFS_BLKS_SZ((\
                                        NFS_INODE_PER_FILE + NFS_DATA_PER_FILE)))
#define NFS_DATA_OFS(ino)               (NFS_INO_OFS(ino) + NFS_BLKS_SZ(NFS_INODE_PER_FILE))

#define NFS_IS_DIR(pinode)              (pinode->dentry->ftype == NFS_DIR)
#define NFS_IS_REG(pinode)              (pinode->dentry->ftype == NFS_REG_FILE)
#define NFS_IS_SYM_LINK(pinode)         (pinode->dentry->ftype == NFS_SYM_LINK)

/******************************************************************************
* SECTION: FS Specific Structure - In memory structure
*******************************************************************************/
struct newfs_dentry;
struct newfs_inode;
struct newfs_super;

struct custom_options
{
    const char *device;
};


// 索引
struct newfs_inode
{
    uint32_t           ino;                             /* 在inode位图中的下标 */
    int                size;                            /* 文件已占用空间 */
    int                dir_cnt;
    struct newfs_dentry* dentry;                        /* 指向该inode的dentry */
    struct newfs_dentry* dentrys;                       /* 所有目录项 */
    uint8_t*           block_pointer[NFS_DATA_PER_FILE];// 指向数据块的指针
    uint8_t*           data;
    int                bno[NFS_DATA_PER_FILE];             /* 数据块在磁盘中的块号 */           
}; 


// 目录项
struct newfs_dentry
{
    char                fname[NFS_MAX_FILE_NAME];
    uint32_t            ino;                            // inode编号
    struct newfs_dentry* parent;                        // 父亲Inode的dentry
    struct newfs_dentry* brother;                       // 兄弟
    struct newfs_inode*  inode;                         // 指向inode
    NFS_FILE_TYPE       ftype;
};



// 超级块
struct newfs_super
{
    /* TODO: Define yourself */
    uint32_t           magic;
    int                driver_fd;
    int                sz_io;//IO块大小
    int                sz_disk;//磁盘总容量
    int                sz_usage;
    int                sz_blk; //逻辑块大小
    int                blks_nums; //逻辑块数

    int                max_ino;             /* 最大索引块数 */
    int                max_data;            /* 最大数据块数 */


    uint8_t*           map_inode;           /*  */
    int                map_inode_blks;      /* 索引位图块数 */
    int                map_inode_offset;

    uint8_t*            map_data;           /* 指向数据块位图的内存起点 */ 
    int                 map_data_blks;      /* 数据块位图占用的块数 */
    int                 map_data_offset;    /* 数据块位图在磁盘上的偏移 */
    
    int                inode_offset;        /* 索引结点的偏移 */
    int                data_offset;         /* 数据块偏移 */

    boolean            is_mounted;          /* 是否已经挂载 */

    struct newfs_dentry* root_dentry;
};


/**
 * @brief         创建一个目录项
 * @param fname   文件名
 * @param ftype   文件类型
 * @return        创建的目录项
*/
static inline struct newfs_dentry* new_dentry(char * fname, NFS_FILE_TYPE ftype) {
    struct newfs_dentry * dentry = (struct newfs_dentry *)malloc(sizeof(struct newfs_dentry)); /* dentry 在内存空间也是随机分配 */
    memset(dentry, 0, sizeof(struct newfs_dentry));
    NFS_ASSIGN_FNAME(dentry, fname);
    dentry->ftype   = ftype;
    dentry->ino     = -1;
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL;
    return dentry;                                            
}

/******************************************************************************
* SECTION: FS Specific Structure - Disk structure
*******************************************************************************/

// 要刷回磁盘的超级块
struct newfs_super_d
{
    uint32_t           magic_num;
    int                sz_usage;
    
    int         map_inode_blks;     /* inode 位图占用的块数 */
    int         map_inode_offset;   /* inode 位图在磁盘上的偏移 */

    int         map_data_blks;      /* data 位图占用的块数 */
    int         map_data_offset;    /* data 位图在磁盘上的偏移 */

    int         inode_offset;       /* 索引结点的偏移 */
    int         data_offset;        /* 数据块的偏移*/
};

struct newfs_inode_d
{
    int                ino;                                /* 在inode位图中的下标 */
    int                size;                               /* 文件已占用空间 */
    int                dir_cnt;
    /* 数据块的索引 */
    int                block_pointer[NFS_DATA_PER_FILE];   // 数据块指针（可固定分配）
    NFS_FILE_TYPE      ftype;   
    int                bno[NFS_DATA_PER_FILE];             /* 数据块在磁盘中的块号 */  
};  

struct newfs_dentry_d
{
    char               fname[NFS_MAX_FILE_NAME];
    NFS_FILE_TYPE      ftype;
    int                ino;                           /* 指向的ino号 */
};  


#endif /* _TYPES_H_ */