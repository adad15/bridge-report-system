-- 接口同步导入不上传离线库本体。那是桌面程序的本机库，一份就含用户打开过的所有桥、
-- 实测三个年度后已 347 MB，整份上传既浪费也无意义。临时目录里放的是一份指向它的
-- 引用文件（路径 + taskId），离线库自始至终只被只读打开、从不复制。
--
-- 这样做的收益是导入记录、状态流转、过期、清理队列这些既有链路一行都不用改：
-- 引用文件是真实存在的小文件，生命周期与 Word 完全一样。

alter table import_source_files
  drop constraint if exists import_source_files_file_extension_check;
alter table import_source_files
  add constraint import_source_files_file_extension_check
  check (lower(file_extension) in ('.docx', '.srcref'));

alter table import_source_files
  drop constraint if exists import_source_files_relative_path_check;
alter table import_source_files
  add constraint import_source_files_relative_path_check check (
    storage_relative_path ~ '^[0-9a-fA-F-]+[.](docx|srcref)$'
    and storage_relative_path !~ '^[A-Za-z]:[\\/]'
    and storage_relative_path !~ '^[\\/]'
    and storage_relative_path not like '%..%'
  );
