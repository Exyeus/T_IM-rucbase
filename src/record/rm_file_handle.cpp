// Copyright (c) 2023-2027 Renmin University of China
// SPDX-License-Identifier: MulanPSL-2.0

#include "rm_file_handle.h"

/**
 * @description: 获取当前表中记录号为rid的记录. 这个记录包括 record)_size 和从 page 返回的数据指针
 * @param {Rid&} rid 记录号，指定记录的位置
 * @param {Context*} context. 竟然没有用到
 * @return {unique_ptr<RmRecord>} rid对应的记录对象指针
 */
std::unique_ptr<RmRecord> RmFileHandle::get_record(const Rid& rid, Context* context) const {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 初始化一个指向RmRecord的指针（赋值其内部的data和size）

    if (rid.page_no < RM_FIRST_RECORD_PAGE || rid.page_no >= file_hdr_.num_pages) {
        throw PageNotExistError("record", rid.page_no);
    }

    if (rid.slot_no < 0 || rid.slot_no >= file_hdr_.num_records_per_page) {
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }

    auto guard = fetch_page_guard(rid.page_no);

    if (!Bitmap::is_set(guard->bitmap, rid.slot_no)) {
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }

    return std::make_unique<RmRecord>(file_hdr_.record_size, guard->get_slot(rid.slot_no));
}

/**
 * @description: 在当前表中插入一条记录，不指定插入位置
 * @param {const char*} buf 要插入的记录数据，只读取 record_size 字节
 * @param {Context*} context
 * @return {Rid} 插入的记录的记录号（位置）
 */
Rid RmFileHandle::insert_record(const char* buf, Context* context) {
    // Todo:
    // 1. 获取当前未满的page handle
    // 2. 在page handle中找到空闲slot位置
    // 3. 将buf复制到空闲slot位置
    // 4. 更新page_handle.page_hdr中的数据结构
    // 注意考虑插入一条记录后页面已满的情况，需要更新file_hdr_.first_free_page_no

    auto guard = create_page_handle();

    int slot_no = Bitmap::first_bit(false, guard.bitmap, file_hdr_.num_records_per_page);

    // 把 caller 提供的 buf 写入合适的 slot 位置.
    memcpy(guard.get_slot(slot_no), buf, file_hdr_.record_size);
    Bitmap::set(guard.bitmap, slot_no);

    guard.page_hdr->num_records++;

    // RM_MAX_RECORD_SIZE 是对于一条记录, 最大的字节数, 而不是每页的记录数.
    // RM_FIRST_RECORD_PAGE 是 constexpr 常量了, 是页面开始编号的地方
    // 这里是在满了的时候，确认这一事实, 然后更新 first and next
    if (guard.page_hdr->num_records == file_hdr_.num_records_per_page) {
        int next_page_no = guard.page_hdr->next_free_page_no;
        file_hdr_.first_free_page_no = next_page_no;
    }

    PageId page_id = guard.page->get_page_id();
    buffer_pool_manager_->unpin_page(
        page_id, 
        true
    );

    return Rid{.page_no = page_id.page_no, .slot_no = slot_no};
}

/**
 * @description: 在当前表中的指定位置插入一条记录
 * @param {Rid&} rid 要插入记录的位置
 * @param {const char*} buf 要插入的记录数据，只读取 record_size 字节
 */
void RmFileHandle::insert_record(const Rid& rid, const char* buf) {
    // 后续恢复/回滚实验使用的扩展接口，不属于 Lab1 评分任务。
}

/**
 * @description: 删除记录文件中记录号为rid的记录
 * @param {Rid&} rid 要删除的记录的记录号（位置）
 * @param {Context*} context
 */
void RmFileHandle::delete_record(const Rid& rid, Context* context) {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 更新page_handle.page_hdr中的数据结构
    // 注意考虑删除一条记录后页面未满的情况，需要调用release_page_handle()
    if (rid.page_no < RM_FIRST_RECORD_PAGE || rid.page_no >= file_hdr_.num_pages) {
        throw PageNotExistError("record", rid.page_no);
    }

    if (rid.slot_no < 0 || rid.slot_no >= file_hdr_.num_records_per_page) {
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }

    // page_handle vs page_guard?
    // RmPageHandle：页面数据的 View，允许访问 bitmap、page_hdr 等，但不自动管理 pin。
    // RmPageGuard：具有 RAII 生命周期管理的页面访问对象，离开作用域时自动释放 pin。

    auto page_guard = fetch_page_guard(rid.page_no);

    if (!page_guard.valid()) {
        throw InternalError("failed to fetch record page");
    }

    if (!Bitmap::is_set(page_guard->bitmap, rid.slot_no)) {
        // This record DNE.
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }
    
    bool at_first_full = (page_guard->page_hdr->num_records == file_hdr_.num_records_per_page);

    // Logically delete, no need to physically reset. 
    // But at the beginning, validation.

    Bitmap::reset(page_guard->bitmap, rid.slot_no);
    page_guard->page_hdr->num_records--;

    if (at_first_full) {
        release_page_handle(page_guard.handle());
    }

    // buffer_pool_manager_->mark_dirty(page_guard->page);
    page_guard.mark_dirty();
}

/**
 * @description: 更新记录文件中记录号为rid的记录
 * @param {Rid&} rid 要更新的记录的记录号（位置）
 * @param {const char*} buf 新记录的数据，只读取 record_size 字节
 * @param {Context*} context
 */
void RmFileHandle::update_record(const Rid& rid, const char* buf, Context* context) {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 更新记录
    if (rid.page_no < RM_FIRST_RECORD_PAGE || rid.page_no >= file_hdr_.num_pages) {
        throw PageNotExistError("record", rid.page_no);
    }

    if (rid.slot_no < 0 || rid.slot_no >= file_hdr_.num_records_per_page) {
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }

    auto page_guard = fetch_page_guard(rid.page_no);

    if (!page_guard.valid()) {
        throw InternalError("Failed to fetch record page");
    }

    if (!Bitmap::is_set(page_guard->bitmap, rid.slot_no)) {
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }

    // rid.slot_no is better than base + slot_no * record_size
    memcpy(page_guard->get_slot(rid.slot_no), buf, file_hdr_.record_size);
    page_guard.mark_dirty();
}

/**
 * 以下函数为辅助函数，仅提供参考，可以选择完成如下函数，也可以删除如下函数，在单元测试中不涉及如下函数接口的直接调用
 */
/**
 * @description: 获取指定页面的页面句柄
 * @param {int} page_no 页面号
 * @return {RmPageHandle} 指定页面的句柄
 */
RmPageHandle RmFileHandle::fetch_page_handle(int page_no) const {
    // Todo:
    // 使用缓冲池获取指定页面，并生成page_handle返回给上层
    // if page_no is invalid, throw PageNotExistError exception
    // RmRecord 竟然持有一个 fd_
    if (page_no < RM_FIRST_RECORD_PAGE || page_no >= file_hdr_.num_pages) {
        throw PageNotExistError("record", page_no);
    }
    Page *page = buffer_pool_manager_->fetch_page(
        PageId{.fd = fd_, .page_no = page_no});
    
    if (page == nullptr) {
        throw PageNotExistError("record", page_no);
    }
    
    RmPageHandle result(&file_hdr_, page);

    // int old_first_free =  file_hdr_.first_free_page_no;
    // result.page_hdr->next_free_page_no = old_first_free;
    // file_hdr_.first_free_page_no = page_no;

    return result;
}

/**
 * @description: 创建一个新的page handle
 * @return {RmPageHandle} 新的PageHandle
 */
RmPageHandle RmFileHandle::create_new_page_handle() {
    // Todo:
    // 1.使用缓冲池来创建一个新page
    // 2.更新page handle中的相关信息
    // 3.更新file_hdr_

    PageId pageid;
    pageid.fd = fd_;
    Page* new_page = buffer_pool_manager_->new_page(&pageid);

    if (new_page == nullptr) {
        throw std::runtime_error("Failed to allocate record page");
    }

    RmPageHandle result(&file_hdr_, new_page);

    /* 上面的初始化函数导致下面这三行其实没有意义了
    result.file_hdr = &file_hdr_;
    result.page = new_page;
    result.bitmap = (char *) (new_page + new_page->OFFSET_PAGE_HDR);
    */
    
    result.page_hdr->next_free_page_no = file_hdr_.first_free_page_no;
    file_hdr_.first_free_page_no = pageid.page_no;
    file_hdr_.num_pages++;
    result.page_hdr->num_records = 0;

    Bitmap::init(result.bitmap, file_hdr_.bitmap_size); /*also .num_records_per_page?*/

    return result;
}

/**
 * @brief 创建或获取一个空闲的page handle
 *
 * @return RmPageHandle 返回生成的空闲page handle
 * @note pin the page, remember to unpin it outside!
 */
RmPageHandle RmFileHandle::create_page_handle() {
    // Todo:
    // 1. 判断file_hdr_中是否还有空闲页
    //     1.1 没有空闲页：使用缓冲池来创建一个新page；可直接调用create_new_page_handle()
    //     1.2 有空闲页：直接获取第一个空闲页
    // 2. 生成page handle并返回给上层

    if (file_hdr_.first_free_page_no != RM_NO_PAGE) {
        return fetch_page_handle(file_hdr_.first_free_page_no);
    }

    return create_new_page_handle();
}

/**
 * @description: 当一个页面从没有空闲空间的状态变为有空闲空间状态时，更新文件头和页头中空闲页面相关的元数据
 */
void RmFileHandle::release_page_handle(RmPageHandle& page_handle) {
    // Todo:
    // 当page从已满变成未满，考虑如何更新：
    // 1. page_handle.page_hdr->next_free_page_no
    // 2. file_hdr_.first_free_page_no
    int current_first = file_hdr_.first_free_page_no;
    file_hdr_.first_free_page_no = page_handle.page->get_page_id().page_no;
    page_handle.page_hdr->next_free_page_no = current_first;
}
