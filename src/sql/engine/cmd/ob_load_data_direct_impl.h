// Copyright (c) 2022-present Oceanbase Inc. All Rights Reserved.
// Author:
//   suzhi.yt <>

#pragma once

#include "lib/allocator/page_arena.h"
#include "observer/table_load/ob_table_load_object_allocator.h"
#include "observer/table_load/ob_table_load_task.h"
#include "share/table/ob_table_load_array.h"
#include "share/table/ob_table_load_row_array.h"
#include "share/table/ob_table_load_define.h"
#include "sql/engine/cmd/ob_load_data_impl.h"
#include "sql/engine/cmd/ob_load_data_parser.h"
#include "common/storage/ob_io_device.h"
#include "observer/table_load/ob_table_load_exec_ctx.h"
#include "observer/table_load/ob_table_load_instance.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadParam;
class ObTableLoadTableCtx;
class ObTableLoadExecCtx;
class ObTableLoadCoordinator;
class ObITableLoadTaskScheduler;
class ObTableLoadInstance;
} // namespace observer
namespace sql
{
/**
 * LOAD DATA接入direct load路径
 *
 * - 输入行必须包含表的所有列数据, 除了堆表的隐藏主键列
 * - 不支持SET子句
 * - 不支持表达式
 */
class ObLoadDataDirectImpl : public ObLoadDataBase
{
  static const int64_t MAX_DATA_MEM_USAGE_LIMIT = 64;

public:
  ObLoadDataDirectImpl();
  virtual ~ObLoadDataDirectImpl();
  int execute(ObExecContext &ctx, ObLoadDataStmt &load_stmt) override;

private:
  class Logger;

  struct DataAccessParam
  {
  public:
    DataAccessParam();
    bool is_valid() const;
    TO_STRING_KV(K_(file_location), K_(file_column_num), K_(file_cs_type));
  public:
    ObLoadFileLocation file_location_;
    share::ObBackupStorageInfo access_info_;
    int64_t file_column_num_; // number of column in file
    ObDataInFileStruct file_format_;
    common::ObCollationType file_cs_type_;
  };

  struct LoadExecuteParam
  {
  public:
    LoadExecuteParam();
    bool is_valid() const;
    TO_STRING_KV(K_(tenant_id), K_(database_id), K_(table_id), K_(combined_name), K_(parallel),
                 K_(batch_row_count), K_(data_mem_usage_limit), K_(need_sort), K_(online_opt_stat_gather),
                 K_(max_error_rows), K_(ignore_row_num), K_(data_access_param), K_(store_column_idxs));
  public:
    uint64_t tenant_id_;
    uint64_t database_id_;
    uint64_t table_id_;
    uint64_t sql_mode_;
    common::ObString database_name_;
    common::ObString table_name_;
    common::ObString combined_name_; // database name + table name
    int64_t parallel_; // number of concurrent threads
    int64_t batch_row_count_;
    int64_t data_mem_usage_limit_; // limit = data_mem_usage_limit * MAX_BUFFER_SIZE
    bool need_sort_;
    bool online_opt_stat_gather_;
    int64_t max_error_rows_; // max allowed error rows
    int64_t ignore_row_num_; // number of rows to ignore per file
    sql::ObLoadDupActionType dup_action_;
    DataAccessParam data_access_param_;
    common::ObSEArray<int64_t, 16>
      store_column_idxs_; // Mapping of stored columns to source data columns
  };

  struct LoadExecuteContext : public observer::ObTableLoadExecCtx
  {
  public:
    LoadExecuteContext();
    bool is_valid() const;
    TO_STRING_KV(KP_(exec_ctx), KP_(allocator), KP_(direct_loader), KP_(job_stat), KP_(logger));
  public:
    observer::ObTableLoadInstance *direct_loader_;
    sql::ObLoadDataStat *job_stat_;
    Logger *logger_;
  };

private:
  class Logger
  {
    static const char *log_file_column_names;
    static const char *log_file_row_fmt;
  public:
    Logger();
    ~Logger();
    int init(const common::ObString &load_info);
    int log_error_line(const common::ObString &file_name, int64_t line_no, int err_code);
  private:
    int create_log_file(const common::ObString &load_info);
    static int generate_log_file_name(char *buf, int64_t size, common::ObString &file_name);
  private:
    lib::ObMutex mutex_;
    ObFileAppender file_appender_;
    bool is_oracle_mode_;
    char *buf_;
    bool is_create_log_succ_;
    bool is_inited_;
    DISALLOW_COPY_AND_ASSIGN(Logger);
  };

private:
  struct DataDesc
  {
  public:
    DataDesc() : file_idx_(0), start_(0), end_(-1) {}
    ~DataDesc() {}
    TO_STRING_KV(K_(file_idx), K_(filename), K_(start), K_(end));
  public:
    int64_t file_idx_;
    ObString filename_;
    int64_t start_;
    int64_t end_;
  };

  class DataDescIterator
  {
  public:
    DataDescIterator();
    ~DataDescIterator();
    int64_t count() const { return data_descs_.count(); }
    int copy(const ObLoadFileIterator &file_iter);
    int copy(const DataDescIterator &desc_iter);
    int add_data_desc(const DataDesc &data_desc);
    int get_next_data_desc(DataDesc &data_desc);
    TO_STRING_KV(K_(data_descs), K_(pos));
  private:
    common::ObSEArray<DataDesc, 64> data_descs_;
    int64_t pos_;
  };

  class IRandomIODevice
  {
  public:
    virtual ~IRandomIODevice() = default;
    virtual int open(const DataAccessParam &data_access_param, const ObString &filename) = 0;
    virtual int pread(char *buf, int64_t count, int64_t offset, int64_t &read_size) = 0;
    virtual int get_file_size(int64_t &file_size) = 0;
  };

  class RandomFileReader : public IRandomIODevice
  {
  public:
    RandomFileReader();
    virtual ~RandomFileReader();
    int open(const DataAccessParam &data_access_param, const ObString &filename) override;
    int pread(char *buf, int64_t count, int64_t offset, int64_t &read_size) override;
    int get_file_size(int64_t &file_size) override;
  private:
    ObString filename_;
    ObFileReader file_reader_;
    bool is_inited_;
  };

  class RandomOSSReader : public IRandomIODevice
  {
  public:
    RandomOSSReader();
    virtual ~RandomOSSReader();
    int open(const DataAccessParam &data_access_param, const ObString &filename) override;
    int pread(char *buf, int64_t count, int64_t offset, int64_t &read_size) override;
    int get_file_size(int64_t &file_size) override;
  private:
    ObIODevice *device_handle_;
    ObIOFd fd_;
    bool is_inited_;
  };

  class SequentialDataAccessor
  {
  public:
    SequentialDataAccessor();
    ~SequentialDataAccessor();
    int init(const DataAccessParam &data_access_param, const ObString &filename);
    int read(char *buf, int64_t count, int64_t &read_size);
    int get_file_size(int64_t &file_size);
    void seek(int64_t offset) { offset_ = offset; }
    int64_t get_offset() const { return offset_; }
  private:
    RandomFileReader random_file_reader_;
    RandomOSSReader random_oss_reader_;
    IRandomIODevice *random_io_device_;
    int64_t offset_;
    bool is_inited_;
  };

  struct DataBuffer
  {
  public:
    DataBuffer();
    ~DataBuffer();
    void reuse();
    void reset();
    int init(int64_t capacity = ObLoadFileBuffer::MAX_BUFFER_SIZE);
    bool is_valid() const;
    int64_t get_data_length() const;
    int64_t get_remain_length() const;
    bool empty() const;
    char *data() const;
    void advance(int64_t length);
    void update_data_length(int64_t length);
    int squash();
    void swap(DataBuffer &other);
    TO_STRING_KV(KPC_(file_buffer), K_(pos));
  public:
    ObLoadFileBuffer *file_buffer_;
    int64_t pos_; // left pos
    bool is_end_file_;
  private:
    DISALLOW_COPY_AND_ASSIGN(DataBuffer);
  };

  // Read the buffer and align it by row.
  class DataReader
  {
  public:
    DataReader();
    int init(const DataAccessParam &data_access_param, LoadExecuteContext &execute_ctx,
             const DataDesc &data_desc, bool read_raw = false);
    int get_next_buffer(ObLoadFileBuffer &file_buffer, int64_t &line_count,
                        int64_t limit = INT64_MAX);
    int get_next_raw_buffer(DataBuffer &data_buffer);
    int64_t get_lines_count() const { return data_trimer_.get_lines_count(); }
    bool has_incomplate_data() const { return data_trimer_.has_incomplate_data(); }
    bool is_end_file() const { return io_accessor_.get_offset() >= end_offset_; }
    ObCSVGeneralParser &get_csv_parser() { return csv_parser_; }
  private:
    LoadExecuteContext *execute_ctx_;
    ObCSVGeneralParser csv_parser_; // 用来计算完整行
    ObLoadFileDataTrimer data_trimer_; // 缓存不完整行的数据
    SequentialDataAccessor io_accessor_;
    int64_t end_offset_;
    bool read_raw_;
    bool is_iter_end_;
    bool is_inited_;
    DISALLOW_COPY_AND_ASSIGN(DataReader);
  };

  // Parse the data in the buffer into a string array by column.
  class DataParser
  {
  public:
    DataParser();
    ~DataParser();
    int init(const DataAccessParam &data_access_param, Logger &logger);
    int parse(const common::ObString &file_name, int64_t start_line_no, DataBuffer &data_buffer);
    int get_next_row(common::ObNewRow &row);
  private:
    void log_error_line(int err_ret, int64_t err_line_no);
  private:
    ObCSVGeneralParser csv_parser_;
    DataBuffer escape_buffer_;
    DataBuffer *data_buffer_;
    // 以下参数是为了打错误日志
    common::ObString file_name_;
    int64_t start_line_no_;
    int64_t pos_;
    Logger *logger_;
    bool is_inited_;
    DISALLOW_COPY_AND_ASSIGN(DataParser);
  };

  class SimpleDataSplitUtils
  {
  public:
    static bool is_simple_format(const ObDataInFileStruct &file_format,
                                 common::ObCollationType file_cs_type);
    static int split(const DataAccessParam &data_access_param, const DataDesc &data_desc,
                     int64_t count, DataDescIterator &data_desc_iter);
  };

  struct TaskResult
  {
    TaskResult()
      : ret_(OB_SUCCESS),
        created_ts_(0),
        start_process_ts_(0),
        finished_ts_(0),
        proccessed_row_count_(0),
        parsed_bytes_(0)
    {
    }
    void reset()
    {
      ret_ = OB_SUCCESS;
      created_ts_ = 0;
      start_process_ts_ = 0;
      finished_ts_ = 0;
      proccessed_row_count_ = 0;
      parsed_bytes_ = 0;
    }
    int ret_;
    int64_t created_ts_;
    int64_t start_process_ts_;
    int64_t finished_ts_;
    int64_t proccessed_row_count_;
    int64_t parsed_bytes_;
    TO_STRING_KV(K_(ret), K_(created_ts), K_(start_process_ts), K_(finished_ts),
                 K_(proccessed_row_count), K_(parsed_bytes));
  };

  struct TaskHandle
  {
    TaskHandle() : task_id_(common::OB_INVALID_ID), session_id_(0), start_line_no_(0) {}
    int64_t task_id_;
    DataBuffer data_buffer_;
    int32_t session_id_;
    DataDesc data_desc_;
    int64_t start_line_no_; // 从1开始
    TaskResult result_;
    TO_STRING_KV(K_(task_id), K_(data_buffer), K_(session_id), K_(data_desc), K_(start_line_no),
                 K_(result));
  private:
    DISALLOW_COPY_AND_ASSIGN(TaskHandle);
  };

  class FileLoadExecutor
  {
  public:
    FileLoadExecutor();
    virtual ~FileLoadExecutor();
    virtual int init(const LoadExecuteParam &execute_param, LoadExecuteContext &execute_ctx,
                     const DataDescIterator &data_desc_iter) = 0;
    int execute();
    int alloc_task(observer::ObTableLoadTask *&task);
    void free_task(observer::ObTableLoadTask *task);
    void task_finished(TaskHandle *handle);
    int process_task_handle(int64_t worker_idx, TaskHandle *handle, int64_t &line_count);
  protected:
    virtual int prepare_execute() = 0;
    virtual int get_next_task_handle(TaskHandle *&handle) = 0;
    virtual int fill_task(TaskHandle *handle, observer::ObTableLoadTask *task) = 0;
  protected:
    int inner_init(const LoadExecuteParam &execute_param, LoadExecuteContext &execute_ctx,
                   int64_t handle_count);
    int init_worker_ctx_array();
    int fetch_task_handle(TaskHandle *&handle);
    int handle_task_result(int64_t task_id, TaskResult &result);
    int handle_all_task_result();
    void wait_all_task_finished();
  protected:
    struct WorkerContext
    {
    public:
      DataParser data_parser_;
      table::ObTableLoadArray<ObObj> objs_;
    };
  protected:
    const LoadExecuteParam *execute_param_;
    LoadExecuteContext *execute_ctx_;
    observer::ObTableLoadObjectAllocator<observer::ObTableLoadTask> task_allocator_;
    observer::ObITableLoadTaskScheduler *task_scheduler_;
    WorkerContext *worker_ctx_array_;
    // task ctrl
    ObParallelTaskController task_controller_;
    ObConcurrentFixedCircularArray<TaskHandle *> handle_reserve_queue_;
    common::ObSEArray<TaskHandle *, 64> handle_resource_; // 用于释放资源
    bool is_inited_;
  private:
    DISALLOW_COPY_AND_ASSIGN(FileLoadExecutor);
  };

  class FileLoadTaskCallback;

private:
  /**
   * Large File
   */

  class LargeFileLoadExecutor : public FileLoadExecutor
  {
  public:
    LargeFileLoadExecutor();
    virtual ~LargeFileLoadExecutor();
    int init(const LoadExecuteParam &execute_param, LoadExecuteContext &execute_ctx,
             const DataDescIterator &data_desc_iter) override;
  protected:
    int prepare_execute() override;
    int get_next_task_handle(TaskHandle *&handle) override;
    int fill_task(TaskHandle *handle, observer::ObTableLoadTask *task) override;
  private:
    int32_t get_session_id();
    int skip_ignore_rows();
  private:
    DataDesc data_desc_;
    DataBuffer expr_buffer_;
    DataReader data_reader_;
    int32_t next_session_id_;
    int64_t total_line_count_;
    DISALLOW_COPY_AND_ASSIGN(LargeFileLoadExecutor);
  };

  class LargeFileLoadTaskProcessor;

private:
  /**
   * Multi Files
   */

  class MultiFilesLoadExecutor : public FileLoadExecutor
  {
  public:
    MultiFilesLoadExecutor() = default;
    virtual ~MultiFilesLoadExecutor() = default;
    int init(const LoadExecuteParam &execute_param, LoadExecuteContext &execute_ctx,
             const DataDescIterator &data_desc_iter) override;
  protected:
    int prepare_execute() override;
    int get_next_task_handle(TaskHandle *&handle) override;
    int fill_task(TaskHandle *handle, observer::ObTableLoadTask *task) override;
  private:
    DataDescIterator data_desc_iter_;
    DISALLOW_COPY_AND_ASSIGN(MultiFilesLoadExecutor);
  };

  class MultiFilesLoadTaskProcessor;

private:
  int init_file_iter();
  // init execute param
  int init_store_column_idxs(common::ObIArray<int64_t> &store_column_idxs);
  int init_execute_param();
  // init execute context
  int init_logger();
  int init_execute_context();

private:
  ObExecContext *ctx_;
  ObLoadDataStmt *load_stmt_;
  LoadExecuteParam execute_param_;
  LoadExecuteContext execute_ctx_;
  observer::ObTableLoadInstance direct_loader_;
  Logger logger_;
  DISALLOW_COPY_AND_ASSIGN(ObLoadDataDirectImpl);
};

} // namespace sql
} // namespace oceanbase
