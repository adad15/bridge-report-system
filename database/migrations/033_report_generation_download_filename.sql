-- 033: 生成任务记住交付文件名。
--
-- 文件名由业务字段拼出来（报告编号、行政区、路线、桥名、综合等级，设计 §20），
-- 但必须在生成那一刻定下来存住：这些字段之后可能被改，而已经生成的那份文件不该
-- 跟着改名。任务行本来就是一次生成的快照，文件名属于这份快照。
--
-- 与 temporary_file_path 一样，到期清理时一并清空——过期后连文件都没有了，留着
-- 一个文件名只会让界面显示出一份实际不存在的报告。

alter table report_generation_jobs
  add column if not exists download_filename text;

comment on column report_generation_jobs.download_filename is
  '交付给用户的文件名，生成时按 §20 的规则拼定；到期随临时文件一起清空。';

-- ready 必须同时有文件和文件名：下载接口两样都要用，缺一样就给不出正确的下载。
alter table report_generation_jobs
  drop constraint if exists report_generation_jobs_ready_has_file;

alter table report_generation_jobs
  add constraint report_generation_jobs_ready_has_file
  check (
    status <> 'ready'
    or (temporary_file_path is not null and download_filename is not null)
  );

-- 到期后路径和文件名一起清空。
alter table report_generation_jobs
  drop constraint if exists report_generation_jobs_expired_has_no_file;

alter table report_generation_jobs
  add constraint report_generation_jobs_expired_has_no_file
  check (
    status <> 'expired'
    or (temporary_file_path is null and download_filename is null)
  );
