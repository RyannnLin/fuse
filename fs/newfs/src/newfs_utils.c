#include "../include/newfs.h"

extern struct newfs_super      newfs_super; 
extern struct custom_options newfs_options;


/**
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* newfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}



/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path 
 * @return int 路径的层级
 */
int newfs_calc_lvl(const char * path) {
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    char* str = path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    while (*str != NULL) {
        if (*str == '/') {
            lvl++;
        }
        str++;
    }
    return lvl;
}



/**
 * @brief 驱动读
 * 
 * @param offset 偏移量
 * @param out_content 缓冲区，用于存放读出的内容
 * @param size 读取内容的大小
 * @return int 
 */
int newfs_driver_read(int offset, uint8_t *out_content, int size) {
    int      offset_aligned = NFS_ROUND_DOWN(offset, NFS_BLK_SZ());
    int      bias           = offset - offset_aligned; // 计算对齐后的剩下的偏移量
    int      size_aligned   = NFS_ROUND_UP((size + bias), NFS_BLK_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    // lseek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // read(NFS_DRIVER(), cur, NFS_IO_SZ());
        ddriver_read(NFS_DRIVER(), cur, NFS_IO_SZ());
        cur          += NFS_IO_SZ();
        size_aligned -= NFS_IO_SZ();   
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NFS_ERROR_NONE;
}


/**
 * @brief 驱动写
 * 照抄
 * @param offset 
 * @param in_content 
 * @param size 
 * @return int 
 */
int newfs_driver_write(int offset, uint8_t *in_content, int size) {
    int      offset_aligned = NFS_ROUND_DOWN(offset, NFS_BLK_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NFS_ROUND_UP((size + bias), NFS_BLK_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    newfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);
    
    // lseek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // write(NFS_DRIVER(), cur, NFS_IO_SZ());
        ddriver_write(NFS_DRIVER(), cur, NFS_IO_SZ());
        cur          += NFS_IO_SZ();
        size_aligned -= NFS_IO_SZ();   
    }

    free(temp_content);
    return NFS_ERROR_NONE;
}



/**
 * @brief 为一个inode分配dentry的bro，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int newfs_alloc_dentry(struct newfs_inode* inode, struct newfs_dentry* dentry) {
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    return inode->dir_cnt;
}

/**
 * @brief 将dentry从inode的dentrys中取出
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int newfs_drop_dentry(struct newfs_inode * inode, struct newfs_dentry * dentry) {
    boolean is_find = FALSE;
    struct newfs_dentry* dentry_cursor;
    dentry_cursor = inode->dentrys;
    
    if (dentry_cursor == dentry) {
        inode->dentrys = dentry->brother;
        is_find = TRUE;
    }
    else {
        while (dentry_cursor)
        {
            if (dentry_cursor->brother == dentry) {
                dentry_cursor->brother = dentry->brother;
                is_find = TRUE;
                break;
            }
            dentry_cursor = dentry_cursor->brother;
        }
    }
    if (!is_find) {
        return -NFS_ERROR_NOTFOUND;
    }
    inode->dir_cnt--;
    return inode->dir_cnt;
}

/**
 * @brief 新建一个索引
 * 
 * @param dentry 要分配索引的dentry
 * @return newfs_inode
 */
struct newfs_inode* newfs_alloc_inode(struct newfs_dentry * dentry) {
    struct newfs_inode* inode;
    int byte_cursor  = 0; // 字节游标
    int bit_cursor   = 0; // 比特游标
    int ino_cursor   = 0; 
    int bno_cursor   = 0;
    int data_blk_cnt = 0;
    boolean is_find_free_entry = FALSE; // 是否已经在位图中找到空闲位
    boolean is_enough_data_blk = FALSE;

    // 从索引位图中取空闲
    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(newfs_super.map_inode_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((newfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                /* 当前ino_cursor位置空闲 */
                newfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == newfs_super.max_ino)
        return -NFS_ERROR_NOSPACE;

    // 分配一个 inode
    inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;

    // 从数据块位图中找空位
    if(dentry->ftype == NFS_DIR)
        for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(newfs_super.map_data_blks); 
            byte_cursor++)
        {
            for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
                if((newfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                    // 当前ino_cursor位置空闲
                    newfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);

                    inode->bno[data_blk_cnt++] = bno_cursor;
                        is_enough_data_blk = TRUE;
                        break;
                }
                bno_cursor++;
            }
            if (is_enough_data_blk) {
                break;
            }
        }

    if (!is_enough_data_blk || bno_cursor == newfs_super.max_data)
        return -NFS_ERROR_NOSPACE;

    /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
    
    /* inode指回dentry */
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;
    
    /* 对于文件，还需要预分配 pointer，指向内存中的随机块 */
    if (NFS_IS_REG(inode)) {
        for(int p_count = 0; p_count < NFS_DATA_PER_FILE; p_count++){
            inode->block_pointer[p_count] = (uint8_t *)malloc(NFS_BLK_SZ());
        }
    }

    return inode;
}




/**
 * @brief 将内存中的索引inode刷回磁盘（递归）
 * 
 * @param inode 
 * @return int 
 */
int newfs_sync_inode(struct newfs_inode * inode) {
    struct newfs_inode_d  inode_d;
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry_d dentry_d;
    int ino             = inode->ino;
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;

    int blk_cnt = 0;
    for(blk_cnt = 0; blk_cnt < NFS_DATA_PER_FILE; blk_cnt++)
        inode_d.bno[blk_cnt] = inode->bno[blk_cnt]; /* 数据块的块号也要赋值 */

    int offset, offset_limit;  /* 用于密集写回 dentry */
    
    /* inode 非密集写回，间隔一个 BLK */
    if (newfs_driver_write(NFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct newfs_inode_d)) != NFS_ERROR_NONE) {
        NFS_DBG("[%s] io error\n", __func__);
        return -NFS_ERROR_IO;
    }

    // 如果inode指向一个目录，则需要递归进入其子目录进行刷回操作
    if (NFS_IS_DIR(inode)) {      
        blk_cnt = 0;                    
        dentry_cursor = inode->dentrys; // 指针指向当前索引对应的目录项

        while(dentry_cursor != NULL && blk_cnt < NFS_DATA_PER_FILE){
            offset = NFS_DATA_OFS(inode->bno[blk_cnt]); // dentry 从 inode 分配的首个数据块开始存
            offset_limit = NFS_DATA_OFS(inode->bno[blk_cnt] + 1);
            /* 写满一个 blk 时换到下一个 bno */
            while (dentry_cursor != NULL)
            {
                memcpy(dentry_d.fname, dentry_cursor->fname, NFS_MAX_FILE_NAME);
                dentry_d.ftype = dentry_cursor->ftype;
                dentry_d.ino = dentry_cursor->ino;
                /* dentry 密集写回 */
                if (newfs_driver_write(offset, (uint8_t *)&dentry_d, 
                                    sizeof(struct newfs_dentry_d)) != NFS_ERROR_NONE) {
                    NFS_DBG("[%s] io error\n", __func__);
                    return -NFS_ERROR_IO;                     
                }
                
                if (dentry_cursor->inode != NULL) {
                    newfs_sync_inode(dentry_cursor->inode); // 递归刷新下一个节点
                }

                dentry_cursor = dentry_cursor->brother; /* 深搜 */
                offset += sizeof(struct newfs_dentry_d);
                if(offset + sizeof(struct newfs_dentry_d) > offset_limit)
                    break;
            }
            blk_cnt++; /* 访问下一个指向的数据块 */
        }
    }
    else if (NFS_IS_REG(inode)) {
        for(blk_cnt = 0; blk_cnt < NFS_DATA_PER_FILE; blk_cnt++){
            if (newfs_driver_write(NFS_DATA_OFS(inode->bno[blk_cnt]), 
                    inode->block_pointer[blk_cnt], NFS_BLK_SZ()) != NFS_ERROR_NONE) {
                NFS_DBG("[%s] io error\n", __func__);
                return -NFS_ERROR_IO;
            }
        }
    }
    return NFS_ERROR_NONE;
}


/**
 * @brief 删除内存中的一个inode， 暂时不释放
 * Case 1: Reg File
 * 
 *                  Inode
 *                /      \
 *            Dentry -> Dentry (Reg Dentry)
 *                       |
 *                      Inode  (Reg File)
 * 
 *  1) Step 1. Erase Bitmap     
 *  2) Step 2. Free Inode                      (Function of sfs_drop_inode)
 * ------------------------------------------------------------------------
 *  3) *Setp 3. Free Dentry belonging to Inode (Outsider)
 * ========================================================================
 * Case 2: Dir
 *                  Inode
 *                /      \
 *            Dentry -> Dentry (Dir Dentry)
 *                       |
 *                      Inode  (Dir)
 *                    /     \
 *                Dentry -> Dentry
 * 
 *   Recursive
 * @param inode 
 * @return int 
 */
int newfs_drop_inode(struct newfs_inode * inode) {
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry*  dentry_to_free;
    struct newfs_inode*   inode_cursor;

    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find = FALSE;

    if (inode == newfs_super.root_dentry->inode) {
        return NFS_ERROR_INVAL;
    }

    if (NFS_IS_DIR(inode)) {
        dentry_cursor = inode->dentrys;
        /* 递归向下drop */
        while (dentry_cursor)
        {   
            inode_cursor = dentry_cursor->inode;
            newfs_drop_inode(inode_cursor);
            newfs_drop_dentry(inode, dentry_cursor);
            dentry_to_free = dentry_cursor;
            dentry_cursor = dentry_cursor->brother;
            free(dentry_to_free);
        }
    }
    else if (NFS_IS_REG(inode) || NFS_IS_SYM_LINK(inode)) {
        for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(newfs_super.map_inode_blks); 
            byte_cursor++)                            /* 调整inodemap */
        {
            for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
                if (ino_cursor == inode->ino) {
                     newfs_super.map_inode[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
                     is_find = TRUE;
                     break;
                }
                ino_cursor++;
            }
            if (is_find == TRUE) {
                break;
            }
        }
        if (inode->data)
            free(inode->data);
        free(inode);
    }
    return NFS_ERROR_NONE;
}


/**
 * @brief 
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct newfs_inode* 
 */
struct newfs_inode* newfs_read_inode(struct newfs_dentry * dentry, int ino) {
    struct newfs_inode* inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    struct newfs_inode_d inode_d;
    struct newfs_dentry* sub_dentry; /* 指向 子dentry 数组 */
    struct newfs_dentry_d dentry_d;
    int    blk_cnt = 0; /* 用于读取多个 bno */
    int    dir_cnt = 0, offset, offset_limit; /* 用于读取目录项 不连续的 dentrys */

    if (newfs_driver_read(NFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct newfs_inode_d)) != NFS_ERROR_NONE) {
        NFS_DBG("[%s] io error\n", __func__);
        return NULL;
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    inode->dentry = dentry;     /* 指回父级 dentry*/
    inode->dentrys = NULL;
    for(blk_cnt = 0; blk_cnt < NFS_DATA_PER_FILE; blk_cnt++)
        inode->bno[blk_cnt] = inode_d.bno[blk_cnt]; /* 数据块的块号也要赋值 */
    
    if (NFS_IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;
        blk_cnt = 0; /* 离散的块号 */
        
        while(dir_cnt != 0){
            offset = NFS_DATA_OFS(inode->bno[blk_cnt]); // dentry 从 inode 分配的首个数据块开始存
            offset_limit = NFS_DATA_OFS(inode->bno[blk_cnt] + 1);
            /* 写满一个 blk 时换到下一个 bno */
            while (offset + sizeof(struct newfs_dentry_d) < offset_limit)
            {
                if (newfs_driver_read(offset, (uint8_t *)&dentry_d, 
                                    sizeof(struct newfs_dentry_d)) != NFS_ERROR_NONE) {
                    NFS_DBG("[%s] io error\n", __func__);
                    return NULL;                    
                }
                
                sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
                sub_dentry->parent = inode->dentry;
                sub_dentry->ino    = dentry_d.ino; 
                newfs_alloc_dentry(inode, sub_dentry);

                offset += sizeof(struct newfs_dentry_d);
                dir_cnt--;
                if(dir_cnt == 0) 
                    break;  /* 减到 0 后提前退出 */
            }
            blk_cnt++; /* 访问下一个指向的数据块 */
        }
    }
    else if (NFS_IS_REG(inode)) {
        for(blk_cnt = 0; blk_cnt < NFS_DATA_PER_FILE; blk_cnt++){
            inode->block_pointer[blk_cnt] = (uint8_t *)malloc(NFS_BLK_SZ()); /* 只分配一个块 */
            if (newfs_driver_read(NFS_DATA_OFS(inode->bno[blk_cnt]), inode->block_pointer[blk_cnt], 
                                NFS_BLK_SZ()) != NFS_ERROR_NONE) {
                NFS_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }
        }
    }
    return inode;
}



/**
 * @brief 
 * 
 * @param inode 
 * @param dir [0...]
 * @return struct newfs_dentry* 
 */
struct newfs_dentry* newfs_get_dentry(struct newfs_inode * inode, int dir) {
    struct newfs_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt) {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}



/**
 * @brief 
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry 
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 * 
 * @param path 
 * @return struct newfs_inode* 
 */
struct newfs_dentry* newfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct newfs_dentry* dentry_cursor = newfs_super.root_dentry;
    struct newfs_dentry* dentry_ret = NULL;
    struct newfs_inode*  inode; 
    int   total_lvl = newfs_calc_lvl(path);
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);
    
    if (total_lvl == 0) {                           /* 根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = newfs_super.root_dentry;
    }
    fname = strtok(path_cpy, "/");       
    while (fname)
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制，如果没有可能是被换出了 */
            newfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }
        /* 注意，这里有 bug，因为没考虑 dentry 块也会被换出 */
        /* 但是因为这里根本没有实现换出（假设内存大于 4MiB）所以没事 */

        inode = dentry_cursor->inode;

        if (NFS_IS_REG(inode) && lvl < total_lvl) {
            NFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        if (NFS_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;

            while (dentry_cursor)
            {
                if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0) {
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother; /* 遍历目录下的子文件 */
            }
            
            if (!is_hit) {
                *is_find = FALSE;
                NFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }

            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/"); 
    }

    /* 如果对应 dentry 的 inode 还没读进来（因为 cache），则重新读 */
    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = newfs_read_inode(dentry_ret, dentry_ret->ino);
    }
    return dentry_ret;
}



/**
 * @brief 挂载
 * 
 * Layout
 * @brief | Super | Inode Map | Data Map | Data |
 * 
 *  BLK_SZ = 2 * IO_SZ
 * 
 * 每个Inode占用一个Blk
 * @param options 
 * @return int 
 */
int newfs_mount(struct custom_options options){
    int                 ret = NFS_ERROR_NONE;
    int                 driver_fd;
    struct newfs_super_d  newfs_super_d; // 读出的磁盘超级块
    struct newfs_dentry*  root_dentry;
    struct newfs_inode*   root_inode;
    
    boolean             is_init = FALSE; // 判断是否进行了重建，用于后续判断是否需要初始化根节点root_inode

    newfs_super.is_mounted = FALSE;

    driver_fd = ddriver_open(options.device);

    if (driver_fd < 0) {
        return driver_fd;
    }

    newfs_super.driver_fd = driver_fd;
    // 读取磁盘大小    
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_SIZE,  &newfs_super.sz_disk);
    // 读取IO块大小
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &newfs_super.sz_io);
    // 初始化逻辑块大小
    newfs_super.sz_blk = newfs_super.sz_io * 2;

    root_dentry = new_dentry("/", NFS_DIR);

    // 从磁盘读取超级块到newfs_super_d
    if (newfs_driver_read(NFS_SUPER_OFS, (uint8_t *)(&newfs_super_d), 
                        sizeof(struct newfs_super_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }   
    // 如果磁盘超级块未设置幻数，说明还未进行过挂载
    if (newfs_super_d.magic_num != NFS_MAGIC_NUM) {   
        // 初始化磁盘超级块数据
        // 注：这里其实不需要初始化这些变量，直接使用宏定义也未尝不可，但进行初始化更加符合实际的文件系统功能
        newfs_super.max_ino = NFS_INODE_BLKS;

        newfs_super.max_data = NFS_DATA_BLKS;

        newfs_super_d.magic_num = NFS_MAGIC_NUM;

        newfs_super_d.map_inode_offset = NFS_SUPER_OFS + NFS_BLKS_SZ(NFS_SUPER_BLKS); // 索引位图偏移量

        newfs_super_d.map_data_offset  = newfs_super_d.map_inode_offset + NFS_BLKS_SZ(NFS_MAP_INODE_BLKS); // 数据位图偏移量 = 索引位图偏移量 ＋ 索引位图大小

        newfs_super_d.inode_offset = newfs_super_d.map_data_offset + NFS_BLKS_SZ(NFS_MAP_DATA_BLKS);
        
        newfs_super_d.data_offset  = newfs_super_d.inode_offset + NFS_BLKS_SZ(newfs_super.max_ino);

        newfs_super_d.map_inode_blks  = NFS_MAP_INODE_BLKS;
        newfs_super_d.map_data_blks   = NFS_MAP_DATA_BLKS;

        newfs_super_d.sz_usage        = 0;

        is_init = TRUE;
    }

    // 将磁盘超级块的内容写入内存超级块
    newfs_super.sz_usage   = newfs_super_d.sz_usage;     
    
    // 初始化索引位图
    newfs_super.map_inode = (uint8_t *)malloc(NFS_BLKS_SZ(newfs_super_d.map_inode_blks));
    newfs_super.map_inode_blks = newfs_super_d.map_inode_blks;
    newfs_super.map_inode_offset = newfs_super_d.map_inode_offset;

    // 初始化数据块位图
    newfs_super.map_data = (uint8_t *)malloc(NFS_BLKS_SZ(newfs_super_d.map_data_blks));
    newfs_super.map_data_blks = newfs_super_d.map_data_blks;
    newfs_super.map_data_offset = newfs_super_d.map_data_offset;

    // 初始化索引块和数据块
    newfs_super.inode_offset = newfs_super_d.inode_offset;
    newfs_super.data_offset = newfs_super_d.data_offset;

    // 读取索引位图和数据块位图到内存
    if (newfs_driver_read(newfs_super_d.map_inode_offset, (uint8_t *)(newfs_super.map_inode), 
                        NFS_BLKS_SZ(newfs_super_d.map_inode_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    if (newfs_driver_read(newfs_super_d.map_data_offset, (uint8_t *)(newfs_super.map_data), 
                        NFS_BLKS_SZ(newfs_super_d.map_data_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }


    // 如果挂载时进行了初始化，则需要将初始化的根节点写入磁盘
    if (is_init) {
        root_inode = newfs_alloc_inode(root_dentry);    
        newfs_sync_inode(root_inode);  
    }
    
    /* 如果磁盘有数据，则先读入根结点，其他暂时不读 (Cache) */
    root_inode            = newfs_read_inode(root_dentry, NFS_ROOT_INO); 
    root_dentry->inode    = root_inode;
    newfs_super.root_dentry = root_dentry;
    newfs_super.is_mounted  = TRUE;
    printf("**********完成挂载**********\n");
    return ret;
}



/**
 * @brief 卸载
 * 
 * @return int 
 */
int newfs_umount() {
    struct newfs_super_d  newfs_super_d; 

    if (!newfs_super.is_mounted) {
        return NFS_ERROR_NONE;
    }

    newfs_sync_inode(newfs_super.root_dentry->inode); // 刷回根节点

    // 将内存超级块中的信息写到磁盘超级块中                             
    newfs_super_d.magic_num           = NFS_MAGIC_NUM;
    newfs_super_d.sz_usage            = newfs_super.sz_usage;

    newfs_super_d.map_inode_blks      = newfs_super.map_inode_blks;
    newfs_super_d.map_inode_offset    = newfs_super.map_inode_offset;
    newfs_super_d.map_data_blks       = newfs_super.map_data_blks;
    newfs_super_d.map_data_offset     = newfs_super.map_data_offset;

    newfs_super_d.inode_offset        = newfs_super.inode_offset;
    newfs_super_d.data_offset         = newfs_super.data_offset;
    
    // 将超级块写回磁盘
    if (newfs_driver_write(NFS_SUPER_OFS, (uint8_t *)&newfs_super_d, 
                     NFS_BLKS_SZ(NFS_SUPER_BLKS)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    // 将索引位图写回磁盘
    if (newfs_driver_write(newfs_super_d.map_inode_offset, (uint8_t *)(newfs_super.map_inode), 
                         NFS_BLKS_SZ(newfs_super_d.map_inode_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    // 将数据块位图写回磁盘
    if (newfs_driver_write(newfs_super_d.map_data_offset, (uint8_t *)(newfs_super.map_data), 
                         NFS_BLKS_SZ(newfs_super_d.map_data_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    free(newfs_super.map_inode);
    free(newfs_super.map_data);
    ddriver_close(NFS_DRIVER());
    printf("**********完成卸载**********\n");
    return NFS_ERROR_NONE;
}