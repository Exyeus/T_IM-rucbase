// Copyright (c) 2023-2027 Renmin University of China
// SPDX-License-Identifier: MulanPSL-2.0

#include "rm_scan.h"

#include "rm_file_handle.h"

/**
 * @brief 初始化file_handle和rid
 * @param file_handle
 */
RmScan::RmScan(const RmFileHandle* file_handle) : file_handle_(file_handle) {
    // Todo:
    // 初始化file_handle和rid（指向第一个存放了记录的位置）
    // first_free_page 是对于分配任务而言
    // 像这样, 对于已经有了的, 就要考虑 RM_FIRST_RECORD_PAGE
    
    // rid_.slot_no = file_handle_->file_hdr_.


    for (int page_no = RM_FIRST_RECORD_PAGE; 
         page_no < file_handle_->file_hdr_.num_pages;
         page_no++) {
        
        auto page_guard = file_handle_->fetch_page_guard(page_no);
        
        if (!page_guard.valid()) {
            throw InternalError("failed to fetch record page");
        }

        int slot_no = Bitmap::first_bit(
            true, 
            page_guard->bitmap, 
            file_handle_->file_hdr_.num_records_per_page
        );
        /*
            Bitmap::first_bit(true, 
                            page_guard->bitmap, 
                            page_guard->file_hdr->num_records_per_page
                            ) < page_guard->file_hdr->num_records_per_page
        */
        if (slot_no < file_handle->file_hdr_.num_records_per_page) {
            rid_.slot_no = slot_no;
            rid_.page_no = page_no;
            break;
        }
    }
    
}

/**
 * @brief 找到文件中下一个存放了记录的位置
 */
void RmScan::next() {
    // Todo:
    // 找到文件中下一个存放了记录的非空闲位置，用rid_来指向这个位置
    if (rid_.page_no == INVALID_PAGE_ID) {
        return;
    }
    
    for (int page_no = rid_.page_no; 
         page_no < file_handle_->file_hdr_.num_pages;
         page_no++) {
        auto page_guard =
            file_handle_->fetch_page_guard(page_no);

        if (!page_guard.valid()) {
            throw InternalError("failed to fetch record page");
        }

        int curr = (page_no == rid_.page_no)? rid_.slot_no: -1;

        // 寻找当前页面中的下一条有效记录
        int next_slot = Bitmap::next_bit(
            true,
            page_guard->bitmap,
            page_guard->file_hdr->num_records_per_page, // - rid_.slot_no,
            curr
        );

        if (next_slot < page_guard->file_hdr->num_records_per_page) {
            rid_.slot_no = next_slot;
            rid_.page_no = page_no;
            return;
        }

    }

    rid_.page_no = INVALID_PAGE_ID;

}

/**
 * @brief ​ 判断是否到达文件末尾
 */
bool RmScan::is_end() const {
    // Todo: 修改返回值

    // 这个条件只能说明当前 Slot 编号已经超过本页的有效范围，不能说明整个文件都遍历完了。
    // rid_.slot_no >= file_handle_->file_hdr_.num_records_per_page

    return (rid_.page_no == INVALID_PAGE_ID);

}

/**
 * @brief RmScan内部存放的rid
 */
Rid RmScan::rid() const { return rid_; }