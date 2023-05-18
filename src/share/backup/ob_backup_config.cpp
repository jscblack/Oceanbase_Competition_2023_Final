/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX SHARE
#include "ob_backup_config.h"
#include "ob_backup_data_table_operator.h"
#include "ob_backup_helper.h"
#include "ob_backup_store.h"
#include "ob_archive_persist_helper.h"
#include "ob_backup_connectivity.h"
#include "sql/parser/parse_node.h"
#include "observer/ob_server_struct.h"
#include "share/schema/ob_schema_mgr.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/backup/ob_backup_connectivity.h"
#include "share/backup/ob_tenant_archive_mgr.h"
#include "share/restore/ob_log_restore_source_mgr.h"  // ObLogRestoreSourceMgr
#include "share/ob_log_restore_proxy.h"  // ObLogRestoreProxyUtil

using namespace oceanbase;
using namespace share;
using namespace common;

const char *const ObBackupConfigType::type_str[ObBackupConfigType::Type::MAX_CONFIG_NAME] = {
  "data_backup_dest",
  "log_archive_dest",
  "log_archive_dest_state",
  "log_archive_dest_1",
  "log_archive_dest_state_1",
  "log_archive_dest_2",
  "log_archive_dest_state_2",
  "log_archive_dest_3",
  "log_archive_dest_state_3",
  "log_archive_dest_4",
  "log_archive_dest_state_4",
  "log_archive_dest_5",
  "log_archive_dest_state_5",
  "log_archive_dest_6",
  "log_archive_dest_state_6",
  "log_archive_dest_7",
  "log_archive_dest_state_7",
  "log_archive_dest_8",
  "log_archive_dest_state_8",
  "log_restore_source",
};

int ObBackupConfigType::set_backup_config_type(const common::ObString& str) {
  int ret = OB_SUCCESS;
  char tmp_str[common::OB_MAX_CONFIG_NAME_LEN] = { 0 };
  if (str.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(str));
  } else if (OB_FAIL(databuff_printf(tmp_str, sizeof(tmp_str), "%.*s", str.length() ,str.ptr()))) {
    LOG_WARN("fail to set config value", K(ret), K(str));
  } else if (OB_FALSE_IT(str_tolower(tmp_str, strlen(tmp_str)))) {
  } else {
    Type tmp_type = Type::MAX_CONFIG_NAME;
    STATIC_ASSERT(Type::MAX_CONFIG_NAME == ARRAYSIZEOF(type_str), "types count mismatch");
    for (int64_t i = 0; i < ARRAYSIZEOF(type_str); i++) {
      if (0 == STRCMP(type_str[i], tmp_str)) {
        tmp_type = static_cast<Type>(i);
      }
    }
    if (Type::MAX_CONFIG_NAME == tmp_type) {
      ret = OB_ENTRY_NOT_EXIST;
    } else {
      type_ = tmp_type;
    }
  }
  return ret;
}

const char *ObBackupConfigType::get_backup_config_type_str()
{
  const char *str = "UNKNOWN";
  STATIC_ASSERT(Type::MAX_CONFIG_NAME == ARRAYSIZEOF(type_str), "types count mismatch");
  if (type_ < Type::DATA_BACKUP_DEST || type_ >= Type::MAX_CONFIG_NAME) {
    LOG_ERROR_RET(OB_INVALID_ARGUMENT, "invalid backup config type", K(type_));
  } else {
    str = type_str[type_];
  }
  return str;
}

int BackupConfigItemPair::assign(const BackupConfigItemPair &that)
{
  int ret = OB_SUCCESS;
  if (!that.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid backup config item", K(ret));
  } else if (OB_FAIL(key_.assign(that.key_))) {
    LOG_WARN("fail to assign key", K(ret), K(that.key_));
  } else if (OB_FAIL(value_.assign(that.value_))) {
    LOG_WARN("fail to assign key", K(ret), K(that.value_));
  }
  return ret;
}

bool BackupConfigItemPair::is_valid() const
{
  return key_.is_valid() && value_.is_valid();
}

void BackupConfigItemPair::reset()
{
  key_.reset();
  value_.reset();
}

int BackupConfigItemPair::set_value(const int64_t &value)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(value_.assign_fmt("%ld", value))) {
    LOG_WARN("failed to set value", K(ret), K(value));
  }
  return ret;
}

int ObBackupConfigParserGenerator::set(const ObBackupConfigType &type, const uint64_t tenant_id, const common::ObSqlString &value)
{
  int ret = OB_SUCCESS;
  if (is_setted_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("config parser generator has been setted", K(ret));
  } else if (!type.is_valid() || !is_valid_tenant_id(tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(type), K(tenant_id));
  } else if (nullptr != config_parser_) {
    config_parser_->~ObIBackupConfigItemParser();
    allocator_.free(config_parser_);
    config_parser_ = nullptr;
  }
  if (OB_SUCC(ret)) {
    if ((ObBackupConfigType::Type::LOG_RESTORE_SOURCE == type.get_type()) && OB_FAIL(set_restore_source_type_(value))) {
      LOG_WARN("fail to get restore source type");
    } else if (OB_FAIL(generate_parser_(type, tenant_id))) {
      LOG_WARN("fail to generate_parser", K(ret));
    }
  }
  return ret;
}

/*
As LOG_RESTORE_SOURCE supports two kind of type now, this function is to figure out
whether the LOG_RESTORE_SOURCE is SERVICE or LOCATION.
If the value is "SERVICE=127.0.0.1:1000;127.0.0.1:1001;127.0.0.1:1002 USER=ziqi_user@ziqi_tenant PASSWORD=123",
the restore_source_type_ will be parsed into SERVICE type.
If the value is "LOCATION=file:///data/1/zhaoyongheng.zyh/archivelog";
the restore_source_type_ will be parsed into LOCATION type.
*/
int ObBackupConfigParserGenerator::set_restore_source_type_(const common::ObSqlString &value)
{
  int ret = OB_SUCCESS;
  char tmp_str[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
  char *token = nullptr;
  char *tmp_token = nullptr;
  char *saveptr = nullptr;
  char *tmp_saveptr = nullptr;
  bool is_location = false;
  bool is_service = false;

  if (value.empty()) {
    restore_source_type_ = share::ObLogRestoreSourceType::LOCATION;
  } else if (OB_FAIL(databuff_printf(tmp_str, sizeof(tmp_str), "%.*s", static_cast<int>(value.length()), value.ptr()))) {
    LOG_WARN("fail to set config value", K(value));
  } else {
    saveptr = tmp_str;
    for (char *str = saveptr; OB_SUCC(ret); str = nullptr) {
      token = ::STRTOK_R(str, " ", &saveptr);
      if (nullptr == token) {
        break;
      } else if (OB_ISNULL(tmp_token = ::STRTOK_R(token, "=", &tmp_saveptr))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to split config str", K(ret), KP(tmp_token), K(tmp_saveptr));
      } else if (OB_FALSE_IT(str_tolower(tmp_token, strlen(tmp_token)))) {
      } else if (0 == STRCASECMP(tmp_token, OB_STR_LOCATION)) {
        is_location = true;
      } else if (0 == STRCASECMP(tmp_token, OB_STR_SERVICE)) {
        is_service = true;
      }
      LOG_INFO("log restore source parse token", K(tmp_token), K(str), K(token));
    }
    if (OB_FAIL(ret)) {
      LOG_WARN("fail to parse value", K(value));
    } else if ((is_location && is_service) || (!is_location && !is_service)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("fail to parse restore source type", K(ret), K(value));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "log restore source type");
    } else if (is_location) {
      restore_source_type_ = share::ObLogRestoreSourceType::LOCATION;
    } else if (is_service) {
      restore_source_type_ = share::ObLogRestoreSourceType::SERVICE;
    }
    LOG_DEBUG("log restore source type", K(is_location), K(is_service));
  }
  return ret;
}

#define GENERATE_LOG_ARCHIVE_PARSER(parserClassName, channelNum, configType, tenantId)  \
    case ObBackupConfigType::Type::configType: {                                        \
      int64_t size = sizeof(ObLogArchive##parserClassName##ConfigParser);               \
      void *tmp_ptr = nullptr;                                                          \
      if (OB_ISNULL(tmp_ptr = (allocator_.alloc(size)))) {                              \
        ret = OB_ALLOCATE_MEMORY_FAILED;                                                \
        LOG_WARN("parser generator alloc memory failed", K(ret));                       \
      } else {                                                                          \
        config_parser_ = new(tmp_ptr) ObLogArchive##parserClassName##ConfigParser(ObBackupConfigType::Type::configType, tenantId, channelNum); \
      }                                                                                 \
      break;                                                                            \
    }

int ObBackupConfigParserGenerator::generate_parser_(const ObBackupConfigType &type, const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  switch (type.get_type()) {
    case ObBackupConfigType::Type::DATA_BACKUP_DEST: {
      int64_t size = sizeof(ObDataBackupDestConfigParser);
      void *tmp_ptr = nullptr;
      if (OB_ISNULL(tmp_ptr = (allocator_.alloc(size)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("parser generator alloc memory failed", K(ret));
      } else {
        config_parser_ = new(tmp_ptr) ObDataBackupDestConfigParser(tenant_id);
      }
      break;
    }
    case ObBackupConfigType::Type::LOG_RESTORE_SOURCE: {
      if (share::ObLogRestoreSourceType::SERVICE == restore_source_type_) {
        int64_t size = sizeof(ObLogRestoreSourceServiceConfigParser);
        void *tmp_ptr = nullptr;
        if (OB_ISNULL(tmp_ptr = (allocator_.alloc(size)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("parser generator alloc memory failed", K(ret));
        } else {
          config_parser_ = new(tmp_ptr) ObLogRestoreSourceServiceConfigParser(ObBackupConfigType::LOG_RESTORE_SOURCE, tenant_id);
        }
        break;
      } else if (share::ObLogRestoreSourceType::LOCATION == restore_source_type_) {
        int64_t size = sizeof(ObLogRestoreSourceLocationConfigParser);
        void *tmp_ptr = nullptr;
        if (OB_ISNULL(tmp_ptr = (allocator_.alloc(size)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("parser generator alloc memory failed", K(ret));
        } else {
          config_parser_ = new(tmp_ptr) ObLogRestoreSourceLocationConfigParser(ObBackupConfigType::LOG_RESTORE_SOURCE, tenant_id, 0);
        }
        break;
      }
    }
    GENERATE_LOG_ARCHIVE_PARSER(Dest, 0, LOG_ARCHIVE_DEST, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(Dest, 1, LOG_ARCHIVE_DEST_1, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(Dest, 2, LOG_ARCHIVE_DEST_2, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(Dest, 3, LOG_ARCHIVE_DEST_3, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(Dest, 4, LOG_ARCHIVE_DEST_4, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(Dest, 5, LOG_ARCHIVE_DEST_5, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(Dest, 6, LOG_ARCHIVE_DEST_6, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(Dest, 7, LOG_ARCHIVE_DEST_7, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(Dest, 8, LOG_ARCHIVE_DEST_8, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(DestState, 0, LOG_ARCHIVE_DEST_STATE, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(DestState, 1, LOG_ARCHIVE_DEST_STATE_1, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(DestState, 2, LOG_ARCHIVE_DEST_STATE_2, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(DestState, 3, LOG_ARCHIVE_DEST_STATE_3, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(DestState, 4, LOG_ARCHIVE_DEST_STATE_4, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(DestState, 5, LOG_ARCHIVE_DEST_STATE_5, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(DestState, 6, LOG_ARCHIVE_DEST_STATE_6, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(DestState, 7, LOG_ARCHIVE_DEST_STATE_7, tenant_id);
    GENERATE_LOG_ARCHIVE_PARSER(DestState, 8, LOG_ARCHIVE_DEST_STATE_8, tenant_id);
    default: {
      ret = OB_ERR_SYS;
      LOG_ERROR("invalid config type", K(ret), K(type));
    }
  }
  return ret;
}

void ObBackupConfigParserGenerator::reset()
{
  if (nullptr != config_parser_) {
    config_parser_->~ObIBackupConfigItemParser();
    allocator_.free(config_parser_);
    config_parser_ = nullptr;
  }
  is_setted_ = false;
}

int ObBackupConfigChecker::check_config_name(const common::ObString & name, bool &is_backup_config)
{
  int ret = OB_SUCCESS;
  type_.reset();
  is_backup_config = true;
  if (name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(name));
  } else if (OB_FAIL(type_.set_backup_config_type(name.ptr()))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      is_backup_config = false;
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to set backup config type", K(ret), K(name.ptr()));
    }
  }
  return ret;
}

ObBackupConfigParserMgr::ObBackupConfigParserMgr()
  : is_inited_(false),
    parser_generator_()
{
}

int ObBackupConfigParserMgr::init(const common::ObSqlString &name, const common::ObSqlString &value, const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  parser_generator_.reset();
  share::ObBackupConfigChecker checker;
  share::ObBackupConfigType type;
  share::ObIBackupConfigItemParser *config_parser = nullptr;
  bool is_valid = false;
  if (name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid backup config argumnet", K(ret), K(name));
  } else if (OB_FAIL(checker.check_config_name(name.ptr(), is_valid))) {
    LOG_WARN("fail to check backup config is valid", K(ret));
  } else if (!is_valid) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid backup config argumnet", K(ret), K(name), K(value));
  } else if (OB_FAIL(type.set_backup_config_type(name.ptr()))) {
    LOG_WARN("fail to set backup config type", K(ret), K(name));
  } else if (OB_FAIL(parser_generator_.set(type, tenant_id, value))) {
    LOG_WARN("fail to set backup parser generator", K(ret), K(type));
  } else if (OB_ISNULL(config_parser = parser_generator_.get_parser())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("config parser must not be nullptr", K(ret));
  } else if (OB_FAIL(config_parser->parse_from(value))) {
    LOG_WARN("fail to parse value", K(ret), K(type), K(value));
  } else {
    is_inited_ = true;
  }

  if (OB_FAIL(ret)) {
    reset();
  }
  return ret;
}

int ObBackupConfigParserMgr::update_inner_config_table(obrpc::ObSrvRpcProxy &rpc_proxy, common::ObISQLClient &trans)
{
  int ret = OB_SUCCESS;
  ObIBackupConfigItemParser *parser = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("backup parser mgr not init", K(ret));
  } else if (OB_ISNULL(parser = parser_generator_.get_parser())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parser must not be nullptr", K(ret));
  } else if (OB_FAIL(parser->check_before_update_inner_config(rpc_proxy, trans))) {
    LOG_WARN("fail to check before update inner config", K(ret));
  } else if (OB_FAIL(parser->update_inner_config_table(trans))) {
    LOG_WARN("fail to update inner config table", K(ret));
  }
  return ret;
}

int ObBackupConfigParserMgr::only_check_before_update(ObCompatibilityMode &compat_mode)
{
  int ret = OB_SUCCESS;
  ObIBackupConfigItemParser *parser = nullptr;
  compat_mode = ObCompatibilityMode::OCEANBASE_MODE;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("backup parser mgr not init", KR(ret));
  } else if (OB_ISNULL(parser = parser_generator_.get_parser())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parser must not be nullptr", KR(ret));
  } else if (OB_FAIL(parser->check_before_update_inner_config(true /* for_verify */, compat_mode))) {
    LOG_WARN("fail to check_before_update_inner_config", KR(ret));
  }
  return ret;
}

void ObBackupConfigParserMgr::reset()
{
  parser_generator_.reset();
  is_inited_ = false;
}

int ObDataBackupDestConfigParser::parse_from(const common::ObSqlString &value)
{
  int ret = OB_SUCCESS;
  share::ObBackupDest backup_dest;
  share::BackupConfigItemPair pair;
  ObBackupPathString path;
  if (value.empty()) { // allow user to set backup_data_dest = "";
  } else if (OB_FAIL(backup_dest.set(value.ptr()))) {
    LOG_WARN("fail to set backup dest", K(ret));
  } else if (OB_FAIL(backup_dest.get_backup_dest_str(path.ptr(), path.capacity()))) {
    LOG_WARN("fail to get path", K(ret));
  } 
  
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(pair.key_.assign(type_.get_backup_config_type_str()))) {
    LOG_WARN("fail to assign backup config type str", K(ret));
  } else if (OB_FAIL(pair.value_.assign(path.ptr()))) {
    LOG_WARN("fail to assign backup dest root path", K(ret));
  } else if (OB_FAIL(config_items_.push_back(pair))) {
    LOG_WARN("fail to push backup item", K(ret));
  }
  return ret;
}


int ObDataBackupDestConfigParser::check_before_update_inner_config(obrpc::ObSrvRpcProxy &rpc_proxy, common::ObISQLClient &trans)
{
  int ret = OB_SUCCESS;
  bool is_doing = false;
  ObBackupDest dest;
  bool is_exist = false;
  share::ObBackupPathString backup_dest;
  share::ObBackupStore store;
  int64_t dest_id = 0;
  ObBackupDestMgr dest_mgr;
  bool is_empty = true;
  ObBackupDestType::TYPE dest_type = ObBackupDestType::TYPE::DEST_TYPE_BACKUP_DATA;
  if (!type_.is_valid() || 1 != config_items_.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid parser", K(ret), KPC(this));
  } else if (OB_FAIL(check_doing_backup_(trans, is_doing))) {
    LOG_WARN("fail to check doing backup", K(ret));
  } else if (is_doing) {
    ret = OB_BACKUP_IN_PROGRESS;
    LOG_WARN("backup is in progress, can't change backup dest", K(ret), KPC(this));
  } else if (!config_items_.at(0).value_.empty()) {
    if (OB_FAIL(backup_dest.assign(config_items_.at(0).value_.ptr()))) {
      LOG_WARN("fail to assign backup dest", K(ret), K_(tenant_id), K_(config_items));
    } else if (OB_FAIL(dest_mgr.init(tenant_id_, dest_type, backup_dest, trans))) {
      LOG_WARN("fail to init dest manager", K(ret), K_(tenant_id), K(backup_dest));
    } else if (OB_FAIL(dest_mgr.check_dest_validity(rpc_proxy, false/*need_format_file*/))) {
      LOG_WARN("fail to check dest validity", K(ret), K_(tenant_id), K(backup_dest));
    } else {
      LOG_INFO("succ to check data dest config", K_(tenant_id), K(backup_dest)); 
    }
  }
  return ret;
}

int ObDataBackupDestConfigParser::update_data_backup_dest_config_(common::ObISQLClient &trans)
{
  int ret = OB_SUCCESS;
  share::ObBackupHelper helper;
  share::ObBackupDest dest;
  char backup_dest_str[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
  if (!type_.is_valid() || 1 != config_items_.count() ) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid parser", K(ret), KPC(this));
  } else if (!config_items_.at(0).value_.empty()) {
    if (OB_FAIL(dest.set(config_items_.at(0).value_.ptr()))) {
      LOG_WARN("fail to set backup dest", K(ret), K_(tenant_id), K_(config_items));
    } else if (OB_FAIL(dest.get_backup_dest_str(backup_dest_str, sizeof(backup_dest_str)))) {
      LOG_WARN("fail to get_backup_dest_str", K(ret), K(dest));
    } 
  }
  
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(helper.init(tenant_id_, trans))) {
    LOG_WARN("fail to init backup help", K(ret), K(tenant_id_), K(dest));
  } else if (OB_FAIL(helper.set_backup_dest(backup_dest_str))) {
    LOG_WARN("fail to set backup dest", K(ret), K(backup_dest_str));
  }
  return ret;
} 

int ObDataBackupDestConfigParser::update_inner_config_table(common::ObISQLClient &trans)
{
  int ret = OB_SUCCESS;
  ObBackupDestMgr dest_mgr;
  share::ObBackupPathString backup_dest;
  ObBackupDestType::TYPE dest_type = ObBackupDestType::TYPE::DEST_TYPE_BACKUP_DATA;
  // TODO: handle trans failed after write format file.
  if (!type_.is_valid() || 1 != config_items_.count() ) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid parser", K(ret), KPC(this));
  } else if (!config_items_.at(0).value_.empty()) {
    // allow set empty data backup dest
    if (OB_FAIL(backup_dest.assign(config_items_.at(0).value_.ptr()))) {
      LOG_WARN("fail to assign backup dest", K(ret), K_(tenant_id), K_(config_items));
    } else if (OB_FAIL(dest_mgr.init(tenant_id_, dest_type, backup_dest, trans))) {
      LOG_WARN("fail to init dest manager", K(ret), K_(tenant_id));
    } else if (OB_FAIL(dest_mgr.write_format_file())) {
      LOG_WARN("fail to assign backup dest", K(ret), K_(tenant_id));
    }
  }
  
  if (OB_FAIL(update_data_backup_dest_config_(trans))) {
    LOG_WARN("fail to update data backup dest config", K(ret), K_(tenant_id));
  } else {
    LOG_INFO("succeed to set backup dest", K(ret), KPC(this));
  }
  return ret;
}

int ObDataBackupDestConfigParser::check_doing_backup_(common::ObISQLClient &trans, bool &is_doing)
{
  int ret = OB_SUCCESS;
  common::ObSArray<share::ObBackupJobAttr> job_attrs;
  is_doing = false;
  if (is_sys_tenant(tenant_id_)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("sys tenant no need to set backup dest", K(ret));
  } else if (OB_FAIL(share::ObBackupJobOperator::get_jobs(trans, tenant_id_, false/*no update*/, job_attrs))) {
    LOG_WARN("fail to get job_attrs", K(ret), K(tenant_id_));
  } else if (!job_attrs.empty()) {
    is_doing = true;
  }
  return ret;
}

int ObDataBackupDestConfigParser::check_backup_dest_has_been_used_(bool &is_used)
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObLogArchiveDestConfigParser::parse_from(const common::ObSqlString &value)
{
  int ret = OB_SUCCESS;
  char tmp_str[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
  char *token = nullptr;
  char *saveptr = nullptr;
  char *p_end = nullptr;
  is_empty_ = false;
  if (value.empty()) {
    // allow set empty archive dest
    is_empty_ = true;
  } else if (OB_FAIL(databuff_printf(tmp_str, sizeof(tmp_str), "%.*s", static_cast<int>(value.length()), value.ptr()))) {
    LOG_WARN("fail to set config value", K(ret), K(value));
  } else {
    token = tmp_str;
    for (char *str = token; OB_SUCC(ret); str = nullptr) {
      token = ::STRTOK_R(str, " ", &saveptr);
      if (nullptr == token) {
        break;
      } else if (OB_FAIL(do_parse_sub_config_(token))) {
        LOG_WARN("fail to do parse log archve dest sub config", K(ret));
      }
    }
  }
  return ret;
}

int ObLogArchiveDestConfigParser::update_archive_dest_config_(common::ObISQLClient &trans)
{
  int ret = OB_SUCCESS;
  share::ObArchivePersistHelper helper;
  share::BackupConfigItemPair dest_id_pair;
  ObBackupDest dest;
  int64_t dest_id = 0;
  bool is_exist = false;
  bool is_running = false;
  if (OB_FAIL(helper.init(tenant_id_))) {
    LOG_WARN("fail to init backup help", K(ret), K(tenant_id_));
  } else if (OB_FAIL(helper.lock_archive_dest(trans, dest_no_, is_exist))) {
    LOG_WARN("fail to lock archive dest", K(ret), K_(dest_no));
  } else if (OB_FAIL(ObTenantArchiveMgr::is_archive_running(trans, tenant_id_, dest_no_, is_running))) {
    LOG_WARN("failed to check archive running.", K(ret), K_(backup_dest));
  } else if (is_running) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("cannot change archive dest when archive is running.", K(ret), K_(backup_dest));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "change archive dest when archive is running is");
  } else if (is_empty_) {
    if (OB_FAIL(helper.del_dest(trans, dest_no_))) {
      LOG_WARN("fail to del dest", K(ret), K(dest_no_));
    }
  } else {
    if (OB_FAIL(dest.set(backup_dest_.ptr()))) {
      LOG_WARN("fail to set dest", K(ret));
    } else if (OB_FAIL(ObBackupStorageInfoOperator::get_dest_id(trans, tenant_id_, dest, dest_id))) {
      LOG_WARN("fail to get dest id", K(ret)); 
    } else if (OB_FALSE_IT(archive_dest_.dest_id_ = dest_id)) {
    } else if (OB_FAIL(archive_dest_.gen_config_items(config_items_))) {
      LOG_WARN("fail to gen archive config items", K(ret)); 
    }

  // TODO: handle trans failed after write format file.
    ARRAY_FOREACH_X(config_items_, i, cnt, OB_SUCC(ret)) {
      const BackupConfigItemPair &config_item = config_items_.at(i);
      if (OB_FAIL(helper.set_kv_item(trans, dest_no_, config_item.key_, config_item.value_))) {
        LOG_WARN("fail to set log archive dest state", K(ret), K(*this));
      }
    }
  }
  return ret;
}


int ObLogArchiveDestConfigParser::update_inner_config_table(common::ObISQLClient &trans)
{
  int ret = OB_SUCCESS;
  ObBackupDestMgr dest_mgr;
  ObBackupDestType::TYPE dest_type = ObBackupDestType::TYPE::DEST_TYPE_ARCHIVE_LOG; 
  if (!type_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid parser", K(ret), KPC(this));
  } else if (!is_empty_) {
    if (OB_FAIL(dest_mgr.init(tenant_id_, dest_type, backup_dest_, trans))) {
      LOG_WARN("fail to init dest manager", K(ret), K_(tenant_id));
    } else if (OB_FAIL(dest_mgr.write_format_file())) {
      LOG_WARN("fail to write formate file", K(ret), K_(tenant_id));
    }
  } 
  
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(update_archive_dest_config_(trans))) {
    LOG_WARN("fail to update archive dest config", K(ret), K_(tenant_id)); 
  }
  return ret;
}

int ObLogArchiveDestConfigParser::check_before_update_inner_config(obrpc::ObSrvRpcProxy &rpc_proxy, common::ObISQLClient &trans)
{
  int ret = OB_SUCCESS;
  ObBackupDestType::TYPE dest_type = ObBackupDestType::TYPE::DEST_TYPE_ARCHIVE_LOG;
  ObBackupDestMgr dest_mgr;
  bool is_running = false;
  if (is_empty_) {
  } else if (!type_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid parser", K(ret), KPC(this));
  } else if (backup_dest_.is_empty()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("cannot set archive dest without location.", K(ret), K_(backup_dest));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "set archive dest without location is");
  } else if (OB_FAIL(ObTenantArchiveMgr::is_archive_running(trans, tenant_id_, dest_no_, is_running))) {
    LOG_WARN("failed to check archive running.", K(ret), K_(backup_dest));
  } else if (is_running) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("cannot change archive dest when archive is running.", K(ret), K_(backup_dest));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "change archive dest when archive is running is");
  } else if (OB_FAIL(dest_mgr.init(tenant_id_, dest_type, backup_dest_, trans))) {
    LOG_WARN("fail to update archive dest config", K(ret), K_(tenant_id)); 
  } else if (OB_FAIL(dest_mgr.check_dest_validity(rpc_proxy, false/*need_format_file*/))) {
    LOG_WARN("fail to update archive dest config", K(ret), K_(tenant_id)); 
  }
  return ret;
}

int ObLogArchiveDestConfigParser::do_parse_sub_config_(const common::ObString &config_str)
{
  int ret = OB_SUCCESS;
  const char *target= nullptr;
  if (config_str.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty log archve dest sub config is not allowed", K(ret), K(config_str));
  } else {
    char tmp_str[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
    char *token = nullptr;
    char *saveptr = nullptr;
    char *p_end = nullptr;
    if (OB_FAIL(databuff_printf(tmp_str, sizeof(tmp_str), "%.*s", config_str.length(), config_str.ptr()))) {
      LOG_WARN("fail to set config value", K(ret), K(config_str));
    } else if (OB_ISNULL(token = ::STRTOK_R(tmp_str, "=", &saveptr))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to split config str", K(ret), KP(token));
    } else if (OB_FALSE_IT(str_tolower(token, strlen(token)))) {
    } else if (0 == STRCASECMP(token, OB_STR_BINDING)) {
      if (OB_FAIL(do_parse_log_archive_mode_(token, saveptr))) {
        LOG_WARN("fail to do parse log archive mode", K(token), K(saveptr));
      }
    } else if (0 == STRCASECMP(token, OB_STR_LOCATION)) {
      if (OB_FAIL(do_parse_log_archive_dest_(token, saveptr))) {
        LOG_WARN("fail to do parse log archive dest", K(ret), K(token), K(saveptr));
      }
    } else if (0 == STRCASECMP(token, OB_STR_PIECE_SWITCH_INTERVAL)) {
      if (OB_FAIL(do_parse_piece_switch_interval_(token, saveptr))) {
        LOG_WARN("fail to do parse piece switch interval", K(ret), K(token), K(saveptr));
      }
    } else if (0 == STRCASECMP(token, OB_STR_LAG_TARGET)) {
      if (OB_FAIL(do_parse_lag_target_(token, saveptr))) {
        LOG_WARN("fail to do parse lag target", K(ret), K(token), K(saveptr));
      }
    } else if (0 == STRCASECMP(token, OB_STR_COMPRESSION)) {
      if (OB_FAIL(do_parse_compression_(token, saveptr))) {
        LOG_WARN("fail to do parse compression", K(ret), K(token), K(saveptr));
      }
    } else {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("log archive dest does not has this config", K(ret), K(token));
    }
  }
  return ret;
}

int ObLogArchiveDestConfigParser::do_parse_log_archive_mode_(const common::ObString &name, const common::ObString &value)
{
  int ret = OB_SUCCESS;
  if (value.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid log archve binding config", K(ret), K(name), K(value));
  } else if (OB_FAIL(archive_dest_.set_binding(value.ptr()))) {
    LOG_WARN("failed to set binding", K(ret), K(name), K(value));
  }
  return ret;
}

int ObLogArchiveDestConfigParser::do_parse_log_archive_dest_(const common::ObString &name, const common::ObString &value)
{
  int ret = OB_SUCCESS;
  if (value.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid log archve dest config", K(ret), K(value));
  } else if (OB_FAIL(backup_dest_.assign(value.ptr()))) {
    LOG_WARN("fail to set log archive dest", K(ret));
  } else if (OB_FAIL(archive_dest_.set_log_archive_dest(value))) {
    LOG_WARN("fail to set log archive dest", K(ret));
  }
  return ret;
}

int ObLogArchiveDestConfigParser::do_parse_piece_switch_interval_(const common::ObString &name, const common::ObString &value)
{
  int ret = OB_SUCCESS;
  const int64_t MIN_LOG_ARCHIVE_PIECE_SWITCH_INTERVAL = 24 * 3600 * 1000LL * 1000LL; //1d
  const int64_t MAX_LOG_ARCHIVE_PIECE_SWITCH_INTERVAL = 7 * 24 * 3600 * 1000LL * 1000LL; //7d
  if (name.empty() || value.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid log archve dest config", K(ret), K(name), K(value));
  } else if (OB_FAIL(archive_dest_.set_piece_switch_interval(value.ptr()))) {
    LOG_WARN("fail to set piece switch interval", K(ret), K(value));
  } else if (!archive_dest_.is_piece_switch_interval_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("piece switch interval is not valid", K(ret), K(value));
  } else if (archive_dest_.piece_switch_interval_ > MAX_LOG_ARCHIVE_PIECE_SWITCH_INTERVAL
            || archive_dest_.piece_switch_interval_ < MIN_LOG_ARCHIVE_PIECE_SWITCH_INTERVAL) {
#ifndef ERRSIM
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("invalid piece_switch_interval", K(ret), K(value));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "invalid piece_switch_interval out of range [1d,7d] is");
#endif
  }
  return ret;
}

int ObLogArchiveDestConfigParser::do_parse_lag_target_(const common::ObString &name, const common::ObString &value)
{
  int ret = OB_SUCCESS;
  if (name.empty() || value.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid log archve dest config", K(ret), K(name), K(value));
  } else if (OB_FAIL(archive_dest_.set_lag_target(value.ptr()))) {
    LOG_WARN("fail to set piece switch interval", K(ret), K(value));
  } else if (!archive_dest_.is_lag_target_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("piece switch interval is not valid", K(ret), K(value));
  }
  return ret;
}

int ObLogArchiveDestConfigParser::do_parse_compression_(const common::ObString &name, const common::ObString &value)
{
  int ret = OB_SUCCESS;
  if (name.empty() || value.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid log archve dest config", K(ret), K(name), K(value));
  } else if (0 == STRCASECMP(value.ptr(), OB_STR_ENABLE)
      || 0 == STRCASECMP(value.ptr(), OB_STR_DISABLE)) {
    share::BackupConfigItemPair pair;
    if (OB_FAIL(pair.key_.assign(name))) {
      LOG_WARN("fail to assign compression", K(ret), K(name));
    } else if (OB_FAIL(pair.value_.assign(value))) {
      LOG_WARN("fail to assign value", K(ret), K(value));
    } else if (OB_FAIL(config_items_.push_back(pair))) {
      LOG_WARN("fail to push back pair", K(ret), K(pair));
    }
  } else {
  // TODO:  when log archive support compression, remove this.
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("compression not support value", K(ret), K(value));
  }
  return ret;
}

int ObLogArchiveDestStateConfigParser::parse_from(const common::ObSqlString &value)
{
  int ret = OB_SUCCESS;
  ObLogArchiveDestState dest_state;
  share::BackupConfigItemPair pair;
  if (value.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid log archve dest config", K(ret), K(value));
  } else if (OB_FAIL(dest_state.set_state(value.ptr()))) {
    LOG_WARN("fail to set piece switch interval", K(ret), K(value));
  } else if (!dest_state.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dest state is not valid", K(ret), K(value));
  } else if (OB_FAIL(pair.key_.assign(OB_STR_STATE))) {
    LOG_WARN("fail to assign check point", K(ret), K(value));
  } else if (OB_FAIL(pair.value_.assign(value))) {
    LOG_WARN("fail to assign value", K(ret), K(value));
  } else if (OB_FAIL(config_items_.push_back(pair))) {
    LOG_WARN("fail to push back pair", K(ret), K(pair));
  }
  return ret;
}

int ObLogArchiveDestStateConfigParser::update_inner_config_table(common::ObISQLClient &trans)
{
  int ret = OB_SUCCESS;
  share::ObArchivePersistHelper helper;
  if (!type_.is_valid() || 1 != config_items_.count() ) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid parser", K(ret), KPC(this));
  } else if (OB_FAIL(helper.init(tenant_id_))) {
    LOG_WARN("fail to init backup help", K(ret), K(tenant_id_));
  } else {
    const BackupConfigItemPair &config_item = config_items_.at(0);
    if (OB_FAIL(helper.set_kv_item(trans, dest_no_, config_item.key_, config_item.value_))) {
      LOG_WARN("fail to set log archive dest state", K(ret), K(*this));
    }
  }
  return ret;
}


int ObLogArchiveDestStateConfigParser::check_before_update_inner_config(obrpc::ObSrvRpcProxy &rpc_proxy, common::ObISQLClient &trans)
{
  int ret = OB_SUCCESS;
  // do nothing
  return ret;
}

int ObLogRestoreSourceLocationConfigParser::update_inner_config_table(common::ObISQLClient &trans)
{
  int ret = OB_SUCCESS;
  ObBackupDestMgr dest_mgr;
  ObLogRestoreSourceMgr restore_source_mgr;
  ObAllTenantInfo tenant_info;

  if (!type_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid type", KR(ret), KPC(this));
  } else if (OB_FAIL(restore_source_mgr.init(tenant_id_, &trans))) {
    LOG_WARN("failed to init restore_source_mgr", KR(ret), KPC(this));
  } else if (is_empty_) {
    if (OB_FAIL(restore_source_mgr.delete_source())) {
      LOG_WARN("failed to delete restore source", KR(ret), KPC(this));
    }
  } else {
    if (!archive_dest_.is_dest_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid restore source", KR(ret), KPC(this));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "log_restore_source");
    } else if (OB_FAIL(archive_dest_.gen_path_config_items(config_items_))) {
      LOG_WARN("fail to gen archive config items", KR(ret), KPC(this));
    } else {
      ObString key_string = ObString::make_string(config_items_.at(0).key_.ptr());
      ObString path_string = ObString::make_string(OB_STR_PATH);
      ObString value_string = ObString::make_string(config_items_.at(0).value_.ptr());
      if (1 != config_items_.count()
          || path_string != key_string
          || config_items_.at(0).value_.empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid archive source", KR(ret), KPC(this));
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "set log_restore_source");
      } else if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(tenant_id_, &trans,
                                                                true /* for update */, tenant_info))) {
        LOG_WARN("failed to load tenant info", KR(ret), K_(tenant_id));
      } else if (OB_FAIL(restore_source_mgr.add_location_source(tenant_info.get_recovery_until_scn(),
                                                                value_string))) {
        LOG_WARN("failed to add log restore source", KR(ret), K(tenant_info), K(value_string), KPC(this));
      }
    }
  }

  return ret;
}

int ObLogRestoreSourceLocationConfigParser::check_before_update_inner_config(
    obrpc::ObSrvRpcProxy &rpc_proxy,
    common::ObISQLClient &trans)
{
  int ret = OB_SUCCESS;

  if (is_empty_) {
  } else if (!type_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid parser", KR(ret), KPC(this));
  }
  //TODO (zbf271370) need supports format check, checks whether the directory exists, and is a log backup

  //TODO (wenjinyu.wjy) need support access permission check
  //
  return ret;
}

int ObLogRestoreSourceLocationConfigParser::do_parse_sub_config_(const common::ObString &config_str)
{
  int ret = OB_SUCCESS;
  const char *target= nullptr;
  if (config_str.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty log restore source is not allowed", KR(ret), K(config_str));
  } else {
    char tmp_str[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
    char *token = nullptr;
    char *saveptr = nullptr;
    char *p_end = nullptr;
    if (OB_FAIL(databuff_printf(tmp_str, sizeof(tmp_str), "%.*s", config_str.length(), config_str.ptr()))) {
      LOG_WARN("fail to set config value", KR(ret), K(config_str));
    } else if (OB_ISNULL(token = ::STRTOK_R(tmp_str, "=", &saveptr))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to split config str", K(ret), KP(token));
    } else if (OB_FALSE_IT(str_tolower(token, strlen(token)))) {
    } else if (0 == STRCASECMP(token, OB_STR_LOCATION)) {
      if (OB_FAIL(do_parse_log_archive_dest_(token, saveptr))) {
        LOG_WARN("fail to do parse log archive dest", KR(ret), K(token), K(saveptr));
      }
    } else {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("log restore source does not has this config", KR(ret), K(token));
    }
  }
  return ret;
}

int ObLogRestoreSourceServiceConfigParser::parse_from(const common::ObSqlString &value)
{
  int ret = OB_SUCCESS;
  char tmp_str[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
  char *token = nullptr;
  char *saveptr = nullptr;
  is_empty_ = false;

  if (value.empty()) {
    is_empty_ = true;
  } else if (value.length() > OB_MAX_BACKUP_DEST_LENGTH) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("config value is too long");
  } else if (OB_FAIL(databuff_printf(tmp_str, sizeof(tmp_str), "%.*s", static_cast<int>(value.length()), value.ptr()))) {
    LOG_WARN("fail to set config value", K(value));
  } else {
    token = tmp_str;
    for (char *str = token; OB_SUCC(ret); str = nullptr) {
      token = ::STRTOK_R(str, " ", &saveptr);
      if (nullptr == token) {
        break;
      } else if (OB_FAIL(do_parse_sub_config_(token))) {
        LOG_WARN("fail to do parse log restore source server sub config");
      }
    }
  }
  return ret;
}

int ObLogRestoreSourceServiceConfigParser::update_inner_config_table(common::ObISQLClient &trans)
{
  int ret = OB_SUCCESS;
  ObLogRestoreSourceMgr restore_source_mgr;
  ObAllTenantInfo tenant_info;

  if (!type_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid type", KPC(this));
  } else if (OB_FAIL(restore_source_mgr.init(tenant_id_, &trans))) {
    LOG_WARN("failed to init restore_source_mgr", KPC(this));
  } else if (is_empty_) {
    if (OB_FAIL(restore_source_mgr.delete_source())) {
      LOG_WARN("failed to delete restore source", KPC(this));
    }
  } else if (!service_attr_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid restore source", KPC(this));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "log_restore_source");
  } else if (OB_FAIL(service_attr_.gen_config_items(config_items_))) {
    LOG_WARN("fail to gen restore source service config items", KPC(this));
  } else {
    /*
    eg: 开源版本 "ip_list=127.0.0.1:1001;127.0.0.1:1002,USER=restore_user@primary_tenant,PASSWORD=xxxxxxx(密码),TENANT_ID=1002,CLUSTER_ID=10001,COMPATIBILITY_MODE=MYSQL,IS_ENCRYPTED=false"
      非开源版本 "ip_list=127.0.0.1:1001;127.0.0.1:1002,USER=restore_user@primary_tenant,PASSWORD=xxxxxxx(加密后密码),TENANT_ID=1002,CLUSTER_ID=10001,COMPATIBILITY_MODE=MYSQL,IS_ENCRYPTED=true"
    */
    char value_string[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };

    if (config_items_.empty() || OB_MAX_RESTORE_SOURCE_SERVICE_CONFIG_LEN != config_items_.count()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid restore source", KPC(this));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "log_restore_source");
    } else if (OB_FAIL(service_attr_.gen_service_attr_str(value_string, OB_MAX_BACKUP_DEST_LENGTH))) {
      LOG_WARN("failed gen service attr str", K_(tenant_id));
    } else if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(tenant_id_, &trans,
                                                            true /* for update */, tenant_info))) {
      LOG_WARN("failed to load tenant info", K_(tenant_id));
    } else if (OB_FAIL(restore_source_mgr.add_service_source(tenant_info.get_recovery_until_scn(),
                                                            value_string))) {
      LOG_WARN("failed to add log restore source", K(tenant_info), K(value_string), KPC(this));
    }
  }
  return ret;
}

int ObLogRestoreSourceServiceConfigParser::check_before_update_inner_config(obrpc::ObSrvRpcProxy &rpc_proxy, common::ObISQLClient &trans)
{
  int ret = OB_SUCCESS;
  ObCompatibilityMode compat_mode = ObCompatibilityMode::OCEANBASE_MODE;
  if (is_empty_) {
  } else if (!type_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid parser", KR(ret), KPC(this));
  } else if (OB_FAIL(check_before_update_inner_config(false /* for_verify */, compat_mode))) {
    LOG_WARN("fail to check before update inner config");
  }
  return ret;
}

int ObLogRestoreSourceServiceConfigParser::check_before_update_inner_config(
    const bool for_verify,
    ObCompatibilityMode &compat_mode)
{
  int ret = OB_SUCCESS;
  char passwd[OB_MAX_PASSWORD_LENGTH + 1] = { 0 }; //unencrypted password
  ObSqlString user_and_tenant;
  compat_mode = ObCompatibilityMode::OCEANBASE_MODE;

  SMART_VAR(ObLogRestoreProxyUtil, proxy) {
    if (is_empty_) {
    } else if (!type_.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid parser", KPC(this));
    } else if (0 == STRLEN(service_attr_.encrypt_passwd_)
        || 0 == STRLEN(service_attr_.user_.user_name_)
        || 0 == STRLEN(service_attr_.user_.tenant_name_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("fail to parse log restore source config, please check the config parameters");
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "parse log restore source config, please check the config parameters");
    } else if (OB_FAIL(service_attr_.get_password(passwd, sizeof(passwd)))) {
      LOG_WARN("get servcie attr password failed");
    } else if (OB_FAIL(service_attr_.get_user_str_(user_and_tenant))) {
      LOG_WARN("get user str failed", K(service_attr_.user_.user_name_), K(service_attr_.user_.tenant_name_));
    } else if (OB_FAIL(proxy.try_init(tenant_id_/*standby*/, service_attr_.addr_, user_and_tenant.ptr(), passwd))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("proxy connect to primary db failed", K(service_attr_.addr_), K(user_and_tenant));
    } else if (OB_FAIL(proxy.get_tenant_id(service_attr_.user_.tenant_name_, service_attr_.user_.tenant_id_))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("get primary tenant id failed", K(tenant_id_), K(service_attr_.user_));
    } else if (OB_FAIL(proxy.get_cluster_id(service_attr_.user_.tenant_id_, service_attr_.user_.cluster_id_))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("get primary cluster id failed", K(tenant_id_), K(service_attr_.user_.tenant_id_));
    } else if (OB_FAIL(proxy.get_compatibility_mode(service_attr_.user_.tenant_id_, service_attr_.user_.mode_))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("get primary compatibility mode failed", K(tenant_id_), K(service_attr_.user_.tenant_id_));
    } else if (for_verify && OB_FAIL(proxy.check_begin_lsn(service_attr_.user_.tenant_id_))) {
      LOG_WARN("check_begin_lsn failed", K(tenant_id_), K(service_attr_.user_.tenant_id_));
    } else {
      compat_mode = service_attr_.user_.mode_;
      LOG_INFO("check_before_update_inner_config success", K(tenant_id_), K(service_attr_), K(compat_mode));
    }
  }
  return ret;
}

int ObLogRestoreSourceServiceConfigParser::get_compatibility_mode(common::ObCompatibilityMode &compatibility_mode)
{
  int ret = OB_SUCCESS;
  compatibility_mode = OCEANBASE_MODE;
  if (is_empty_) {
  } else if (!type_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid parser", KPC(this));
  } else if (!service_attr_.user_.is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KPC(this));
  } else {
    compatibility_mode = service_attr_.user_.mode_;
  }
  return ret;
}

int ObLogRestoreSourceServiceConfigParser::do_parse_sub_config_(const common::ObString &config_str)
{
  int ret = OB_SUCCESS;
  if (config_str.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty log restore source sub config is not allowed", K(config_str));
  } else {
    char tmp_str[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
    char *token = nullptr;
    char *saveptr = nullptr;
    if (OB_FAIL(databuff_printf(tmp_str, sizeof(tmp_str), "%.*s", config_str.length(), config_str.ptr()))) {
      LOG_WARN("fail to set config value", K(config_str));
    } else if (OB_ISNULL(token = ::STRTOK_R(tmp_str, "=", &saveptr))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to split config str", KP(token));
    } else if (OB_FALSE_IT(str_tolower(token, strlen(token)))) {
    } else if (0 == STRCASECMP(token, OB_STR_SERVICE)) {
      if (OB_FAIL(do_parse_restore_service_host_(token, saveptr))) {
        LOG_WARN("fail to do parse restore service host", K(token), K(saveptr));
      }
    } else if (0 == STRCASECMP(token, OB_STR_USER)) {
      if (OB_FAIL(do_parse_restore_service_user_(token, saveptr))) {
        LOG_WARN("fail to do parse restore service user", K(token), K(saveptr));
      }
    } else if (0 == STRCASECMP(token, OB_STR_PASSWORD)) {
      if (OB_FAIL(do_parse_restore_service_passwd_(token, saveptr))) {
        LOG_WARN("fail to do parse restore service passwd", K(token), K(saveptr));
      }
    } else {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("log restore source does not has this config", K(token));
    }
  }

  return ret;
}

int ObLogRestoreSourceServiceConfigParser::do_parse_restore_service_host_(const common::ObString &name, const
common::ObString &value)
{
  int ret = OB_SUCCESS;
  if (name.empty() || value.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid log restore source service host config", K(name), K(value));
  } else if (OB_FAIL(service_attr_.parse_ip_port_from_str(value.ptr(), ";" /*delimiter*/))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parse restore source service host failed", K(name), K(value));
  }
  if (OB_FAIL(ret)) {
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "set ip list config, please check the length and format of ip list");
  }
  return ret;
}

int ObLogRestoreSourceServiceConfigParser::do_parse_restore_service_user_(const common::ObString &name, const
common::ObString &value)
{
  int ret = OB_SUCCESS;
  if (name.empty() || value.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid log restore source service user config", K(name), K(value));
  } else {
    char tmp_str[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
    char *user_token = nullptr;
    char *tenant_token = nullptr;
    if (OB_FAIL(databuff_printf(tmp_str, sizeof(tmp_str), "%.*s", value.length(), value.ptr()))) {
      LOG_WARN("fail to set user config value", K(value));
    } else if (OB_FAIL(service_attr_.set_service_user_config(tmp_str))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("parse restore source service user failed", K(name), K(value));
    }
  }
  if (OB_FAIL(ret)) {
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "set user config, please check the length and format of username, tenant name");
  }
  return ret;
}

int ObLogRestoreSourceServiceConfigParser::do_parse_restore_service_passwd_(const common::ObString &name, const
common::ObString &value)
{
  int ret = OB_SUCCESS;
  if (name.empty() || value.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid log restore source service password config", K(name), K(value));
  } else if (OB_FAIL(service_attr_.set_service_passwd_to_encrypt(value.ptr()))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parse restore source service password failed", K(name), K(value));
  }
  if (OB_FAIL(ret)) {
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "set password config, please check the length and format of password");
  }
  return ret;
}
