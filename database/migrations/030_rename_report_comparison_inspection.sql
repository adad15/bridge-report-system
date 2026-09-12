-- 030：把年度检查的 previous_inspection_id 改名为 report_comparison_inspection_id。
-- 依赖 002；幂等可复跑。
--
-- 该列自 002 建表起从未被任何 C++ / TypeScript 代码读写，是一个纯占位字段。
-- 报告设计（2026-09-04 §12.1）要把"用户为本次报告手动选择的历史对比检查"存进这一列，
-- 而 §12.1 明确允许用户选择并非时间上最近的一次检查——与"上一次检查"的字面含义不符。
-- 趁还没有任何读写方先改名，避免后续代码把它当成"上一次检查"这项业务事实来用。
--
-- 外键的 on delete set null 语义保持不变：被引用的年度删除后本列自动清空，
-- 因此年度删除预览需要提示有多少份报告配置的对比选择会被清掉（设计 §17.4）。

-- 按 regclass 查而不是查 information_schema：后者不认 search_path，在隔离测试 schema
-- 里会看到 public 的同名表，把"这个 schema 还没改名"误判成"已经改过"。
do $$
begin
  if exists (
    select 1
    from pg_attribute
    where attrelid = 'inspection_years'::regclass
      and attname = 'previous_inspection_id'
      and not attisdropped
  ) and not exists (
    select 1
    from pg_attribute
    where attrelid = 'inspection_years'::regclass
      and attname = 'report_comparison_inspection_id'
      and not attisdropped
  ) then
    alter table inspection_years
      rename column previous_inspection_id to report_comparison_inspection_id;
  end if;
end
$$;

-- 改列名不会改自动生成的约束名。按列反查而不是写死旧名字：inspection_years 上还有
-- revision_source_inspection_id 这条自引用外键，必须靠列名把两者区分开。
do $$
declare
  v_constraint_name text;
begin
  select con.conname
    into v_constraint_name
  from pg_constraint con
  join pg_attribute att
    on att.attrelid = con.conrelid
   and att.attnum = con.conkey[1]
  where con.conrelid = 'inspection_years'::regclass
    and con.contype = 'f'
    and array_length(con.conkey, 1) = 1
    and att.attname = 'report_comparison_inspection_id';

  if v_constraint_name is not null
     and v_constraint_name <> 'inspection_years_report_comparison_inspection_id_fkey' then
    execute format(
      'alter table inspection_years rename constraint %I to %I',
      v_constraint_name,
      'inspection_years_report_comparison_inspection_id_fkey'
    );
  end if;
end
$$;

comment on column inspection_years.report_comparison_inspection_id is
  '用户为本年度报告手动选择的历史对比检查（报告设计 §12.1）。可以不是时间上最近的一次；'
  '候选须同时满足：同一桥梁、is_current、status in (''已确认'', ''已归档'')、'
  '年度早于本年度、且不是本记录自身。保存和生成时各校验一次。';
